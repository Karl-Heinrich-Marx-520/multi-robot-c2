#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "agents/car/task_processor.hpp"

namespace c2::car {

class Simulator {
public:
    using MessageIdGenerator = std::function<std::string()>;

    Simulator(std::string device_id, std::string session_id, MessageIdGenerator next_message_id)
        : device_id_(std::move(device_id)), session_id_(std::move(session_id)),
          next_message_id_(std::move(next_message_id)) {}

    // The transport supplies decoded, validated messages from an admitted controller.
    std::vector<Message> receive(const Message& message, std::uint64_t unix_ms,
                                 TaskProcessor::Clock::time_point now) {
        const auto& request = message.envelope;
        const auto* task = std::get_if<Task>(&message.body);
        if (!task || request.version != protocol_version || request.dst_id != device_id_ ||
            request.dst_session_id != session_id_) {
            return {};
        }

        const Key message_key{request.src_id, request.msg_id};
        if (const auto previous = acknowledgements_.find(message_key); previous != acknowledgements_.end()) {
            auto ack = previous->second;
            ack.envelope.dst_session_id = request.session_id;
            return {std::move(ack)};
        }

        const auto decision = processor_.task(request.src_id, request.msg_id, *task, unix_ms, now, 0);
        auto ack = reply(request, decision.ack, now);
        acknowledgements_.emplace(message_key, ack);
        std::vector<Message> replies{std::move(ack)};

        const Key task_key{request.src_id, task->task_id};
        if (decision.ack.accepted) {
            routes_.try_emplace(task_key, TaskRoute{request, std::nullopt});
        }
        if (decision.result) {
            auto result = result_message(routes_.at(task_key), *decision.result, now);
            result.envelope.dst_session_id = request.session_id;
            replies.push_back(std::move(result));
        }
        return replies;
    }

    std::vector<Message> tick(TaskProcessor::Clock::time_point now) {
        std::vector<Message> replies;
        for (const auto& completion : processor_.tick(now)) {
            auto& route = routes_.at({completion.controller_id, completion.result.task_id});
            replies.push_back(result_message(route, completion.result, now));
        }
        return replies;
    }

private:
    using Key = std::pair<std::string, std::string>;

    struct TaskRoute {
        Envelope request;
        std::optional<Message> result;
    };

    Message reply(const Envelope& request, MessageBody body, TaskProcessor::Clock::time_point now) {
        const auto monotonic_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        const auto timestamp = monotonic_ms.count() > 0 ? static_cast<std::uint64_t>(monotonic_ms.count()) : 0;
        Envelope envelope{protocol_version, next_message_id_(), session_id_, request.session_id,
                          device_id_, request.src_id, request.trace_id, timestamp};
        return {std::move(envelope), std::move(body)};
    }

    const Message& result_message(TaskRoute& route, const Result& result, TaskProcessor::Clock::time_point now) {
        if (!route.result) {
            route.result = reply(route.request, result, now);
        }
        return *route.result;
    }

    std::string device_id_;
    std::string session_id_;
    MessageIdGenerator next_message_id_;
    TaskProcessor processor_;
    std::map<Key, Message> acknowledgements_;
    std::map<Key, TaskRoute> routes_;
};

}  // namespace c2::car
