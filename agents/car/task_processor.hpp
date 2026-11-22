#pragma once

#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include "proto/messages.hpp"

namespace c2::car {

// Transport validates envelopes/sessions and wraps these bodies in reply messages.
// Single-threaded. tick() advances simulated execution only; no hardware is driven.
class TaskProcessor {
public:
    using Clock = std::chrono::steady_clock;
    struct Config {
        std::map<std::string, std::string> targets{{"A_exit", "A"},
                                                {"B_exit", "B"}, {"C_exit", "C"}};
        double max_speed_mps{0.5}; // Simulation limit, not a verified chassis limit.
        std::chrono::milliseconds duration{1000};
    };
    struct Reply {
        Ack ack;
        std::optional<Result> result;
    };
    struct Completion {
        std::string controller_id;
        Result result;
    };
    struct Snapshot {
        DeviceTaskState state{DeviceTaskState::unknown};
        std::optional<Ack> ack;
        std::optional<Result> result;
    };

    TaskProcessor() : TaskProcessor(Config{}) {}
    explicit TaskProcessor(Config config) : config_(std::move(config)) {
        if (!std::isfinite(config_.max_speed_mps) || config_.max_speed_mps <= 0 ||
            config_.duration.count() < 0) throw std::invalid_argument("Invalid simulator configuration");
    }

    Reply task(const std::string& controller, const std::string& msg_id,
               const Task& task, std::uint64_t unix_ms, Clock::time_point now,
               std::optional<std::uint32_t> clock_error_ms = std::nullopt) {
        const Key message_key{controller, msg_id};
        if (const auto i = replies_.find(message_key); i != replies_.end()) return i->second;
        const Key key{controller, task.task_id};
        if (const auto i = tasks_.find(key); i != tasks_.end()) {
            const auto& record = i->second;
            if (!same_task(record.task, task)) return remember(message_key, reject(msg_id, task.task_id, "ID_CONFLICT"));
            auto ack = record.ack;
            ack.ack_for_msg_id = msg_id;
            return remember(message_key, {ack, record.result});
        }
        std::string reason;
        if (cancelled_.count(key)) reason = "CANCELLED";
        else if (const auto error = deadline(task.expire_at_unix_ms, task.max_clock_skew_ms,
                                             unix_ms, clock_error_ms); !error.empty()) reason = error;
        else if (task.action != "GUIDE") reason = "CAPABILITY";
        else if (!valid(task)) reason = "BAD_PARAM";
        else if (busy()) reason = "BUSY";
        Ack ack{msg_id, task.task_id, reason.empty(), std::nullopt};
        if (!reason.empty()) ack.reject_reason = reason;
        tasks_.emplace(key, Record{task, ack, now, std::nullopt});
        if (ack.accepted) ++execution_count_;
        return remember(message_key, {ack, std::nullopt});
    }

    Reply cancel(const std::string& controller, const std::string& msg_id,
                 const Cancel& cancel, std::uint64_t unix_ms,
                 std::optional<std::uint32_t> clock_error_ms = std::nullopt) {
        const Key message_key{controller, msg_id};
        if (const auto i = replies_.find(message_key); i != replies_.end()) return i->second;
        if (const auto error = deadline(cancel.expire_at_unix_ms, cancel.max_clock_skew_ms,
                                        unix_ms, clock_error_ms); !error.empty())
            return remember(message_key, reject(msg_id, cancel.task_id, error));
        const Key key{controller, cancel.task_id};
        const auto i = tasks_.find(key);
        if (i == tasks_.end()) {
            cancelled_.insert(key);
            return remember(message_key, reject(msg_id, cancel.task_id, "UNKNOWN_TASK"));
        }
        auto& record = i->second;
        if (record.result) return remember(message_key,
            {Ack{msg_id, cancel.task_id, false, "ALREADY_FINISHED"}, record.result});
        if (!record.ack.accepted) return remember(message_key, reject(msg_id, cancel.task_id, "UNKNOWN_TASK"));
        // Simulation stops immediately. A real backend must wait for confirmed stop.
        record.result = make_result(cancel.task_id, ResultStatus::aborted, "CANCELLED");
        return remember(message_key, {Ack{msg_id, cancel.task_id, true, std::nullopt}, record.result});
    }

    std::vector<Completion> tick(Clock::time_point now) {
        std::vector<Completion> completed;
        for (auto& [key, record] : tasks_) {
            if (!record.ack.accepted || record.result) continue;
            const auto elapsed = now - record.started;
            const auto timeout = std::chrono::milliseconds(record.task.execution_timeout_ms);
            // Resolve delayed polling by the first scheduled event; timeout wins a tie.
            if (timeout <= config_.duration && elapsed >= timeout)
                record.result = make_result(record.task.task_id, ResultStatus::failed, "EXECUTION_TIMEOUT");
            else if (config_.duration < timeout && elapsed >= config_.duration)
                record.result = make_result(record.task.task_id, ResultStatus::success, "ARRIVED");
            if (record.result) completed.push_back({key.first, *record.result});
        }
        return completed;
    }

    Snapshot snapshot(const std::string& controller, const std::string& task_id) const {
        const Key key{controller, task_id};
        const auto i = tasks_.find(key);
        if (i == tasks_.end()) return {cancelled_.count(key) ? DeviceTaskState::rejected :
                                     DeviceTaskState::unknown, std::nullopt, std::nullopt};
        const auto& record = i->second;
        return {record.result ? DeviceTaskState::terminal : record.ack.accepted ?
                DeviceTaskState::running : DeviceTaskState::rejected, record.ack, record.result};
    }
    std::size_t execution_count() const { return execution_count_; }

private:
    using Key = std::pair<std::string, std::string>;
    struct Record {
        Task task;
        Ack ack;
        Clock::time_point started;
        std::optional<Result> result;
    };
    Config config_;
    std::map<Key, Record> tasks_;
    std::map<Key, Reply> replies_;
    std::set<Key> cancelled_;
    std::size_t execution_count_{};

    static bool equal(const JsonValue& a, const JsonValue& b) {
        if (a.value.index() != b.value.index()) return false;
        return std::visit([&b](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            const auto& y = std::get<T>(b.value);
            if constexpr (std::is_same_v<T, JsonObject>) {
                if (x.size() != y.size()) return false;
                for (const auto& [k, v] : x) {
                    const auto i = y.find(k);
                    if (i == y.end() || !equal(v, i->second)) return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, JsonValue::Array>) {
                if (x.size() != y.size()) return false;
                for (std::size_t i = 0; i < x.size(); ++i) if (!equal(x[i], y[i])) return false;
                return true;
            } else return x == y;
        }, a.value);
    }
    static bool same_task(const Task& a, const Task& b) {
        return a.action == b.action && a.execution_timeout_ms == b.execution_timeout_ms &&
               equal(JsonValue{a.params}, JsonValue{b.params});
    }
    bool valid(const Task& task) const {
        const auto channel = task.params.find("channel"), target = task.params.find("target"),
                   speed = task.params.find("speed_mps");
        if (task.task_id.empty() || task.execution_timeout_ms == 0 || channel == task.params.end() ||
            target == task.params.end() || speed == task.params.end()) return false;
        const auto c = std::get_if<std::string>(&channel->second.value);
        const auto t = std::get_if<std::string>(&target->second.value);
        if (!c || !t) return false;
        const auto mapping = config_.targets.find(*t);
        if (mapping == config_.targets.end() || mapping->second != *c) return false;
        double v = 0;
        if (const auto d = std::get_if<double>(&speed->second.value)) v = *d;
        else if (const auto s = std::get_if<std::int64_t>(&speed->second.value)) v = static_cast<double>(*s);
        else if (const auto u = std::get_if<std::uint64_t>(&speed->second.value)) v = static_cast<double>(*u);
        else return false;
        return std::isfinite(v) && v > 0 && v <= config_.max_speed_mps;
    }
    bool busy() const {
        for (const auto& [key, record] : tasks_) {
            (void)key;
            if (record.ack.accepted && !record.result) return true;
        }
        return false;
    }
    static std::string deadline(std::uint64_t expiry, std::uint32_t skew,
                                std::uint64_t now, std::optional<std::uint32_t> error) {
        if (!error || *error > skew) return "CLOCK_UNSYNCED";
        if (now >= expiry || expiry - now <= skew) return "EXPIRED";
        return {};
    }
    static Result make_result(const std::string& id, ResultStatus status, const std::string& reason) {
        return {id, status, {{"reason", JsonValue{reason}}}};
    }
    static Reply reject(const std::string& msg, const std::string& task, const std::string& reason) {
        return {Ack{msg, task, false, reason}, std::nullopt};
    }
    Reply remember(const Key& key, Reply reply) {
        replies_.emplace(key, reply);
        return reply;
    }
};
} // namespace c2::car
