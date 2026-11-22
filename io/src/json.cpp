#include "io/api/io.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace c2 {
namespace {
using Json = nlohmann::json;

Json to_json_value(const JsonValue& input);
JsonValue from_json_value(const Json& input);

Json object_json(const JsonObject& object) {
    Json result = Json::object();
    for (const auto& [key, value] : object) result[key] = to_json_value(value);
    return result;
}
Json to_json_value(const JsonValue& input) {
    return std::visit([](const auto& value) -> Json {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, JsonObject>) return object_json(value);
        else if constexpr (std::is_same_v<T, JsonValue::Array>) {
            auto result = Json::array();
            for (const auto& item : value) result.push_back(to_json_value(item));
            return result;
        } else if constexpr (std::is_same_v<T, double>) {
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite JSON number");
            return Json(value);
        } else return Json(value);
    }, input.value);
}
JsonObject read_object(const Json& input) {
    if (!input.is_object()) throw std::runtime_error("Expected JSON object");
    JsonObject result;
    for (auto it = input.begin(); it != input.end(); ++it) result.emplace(it.key(), from_json_value(it.value()));
    return result;
}
JsonValue from_json_value(const Json& input) {
    if (input.is_null()) return JsonValue{nullptr};
    if (input.is_boolean()) return JsonValue{input.get<bool>()};
    if (input.is_number_unsigned()) return JsonValue{input.get<std::uint64_t>()};
    if (input.is_number_integer()) return JsonValue{input.get<std::int64_t>()};
    if (input.is_number_float()) return JsonValue{input.get<double>()};
    if (input.is_string()) return JsonValue{input.get<std::string>()};
    if (input.is_object()) return JsonValue{read_object(input)};
    JsonValue::Array result;
    for (const auto& item : input) result.push_back(from_json_value(item));
    return JsonValue{result};
}
std::uint64_t unsigned_value(const Json& value, std::uint64_t limit) {
    if (!value.is_number_unsigned()) throw std::runtime_error("Expected non-negative integer");
    const auto result = value.get<std::uint64_t>();
    if (result > limit) throw std::runtime_error("Integer out of range");
    return result;
}
std::uint32_t u32(const Json& body, const char* key) {
    return static_cast<std::uint32_t>(unsigned_value(body.at(key), std::numeric_limits<std::uint32_t>::max()));
}
std::uint64_t u64(const Json& body, const char* key) {
    return unsigned_value(body.at(key), std::numeric_limits<std::uint64_t>::max());
}
std::optional<std::string> optional_string(const Json& value) {
    return value.is_null() ? std::nullopt : std::optional<std::string>(value.get<std::string>());
}
bool valid_id(const std::string& id) {
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}
void require_id(const std::string& id) {
    if (!valid_id(id)) throw std::runtime_error("ID must be 32 lowercase hexadecimal characters");
}
void validate_envelope(const Envelope& e, bool is_link) {
    if (e.version != protocol_version) throw std::runtime_error("Unsupported protocol version");
    require_id(e.msg_id);
    require_id(e.session_id);
    if (e.dst_session_id) require_id(*e.dst_session_id);
    if (e.trace_id) require_id(*e.trace_id);
    if (!is_link && !e.trace_id) throw std::runtime_error("Business message requires trace_id");
    if (e.src_id.empty() || e.dst_id.empty()) throw std::runtime_error("Device ID cannot be empty");
}
std::string result_string(ResultStatus status) {
    switch (status) {
        case ResultStatus::success: return "SUCCESS";
        case ResultStatus::failed: return "FAILED";
        case ResultStatus::aborted: return "ABORTED";
    }
    throw std::runtime_error("Unknown result status");
}
ResultStatus read_status(const std::string& value) {
    if (value == "SUCCESS") return ResultStatus::success;
    if (value == "FAILED") return ResultStatus::failed;
    if (value == "ABORTED") return ResultStatus::aborted;
    throw std::runtime_error("Unknown result status");
}
void validate_ack(const Ack& ack) {
    require_id(ack.ack_for_msg_id);
    require_id(ack.task_id);
    if (ack.accepted == ack.reject_reason.has_value() ||
        (ack.reject_reason && ack.reject_reason->empty()))
        throw std::runtime_error("ACK reject_reason does not match accepted");
}
Json ack_json(const Ack& ack) {
    validate_ack(ack);
    return {{"ack_for_msg_id", ack.ack_for_msg_id}, {"task_id", ack.task_id},
            {"accepted", ack.accepted}, {"reject_reason", ack.reject_reason ? Json(*ack.reject_reason) : Json(nullptr)}};
}
Json result_json(const Result& result) {
    require_id(result.task_id);
    return {{"task_id", result.task_id}, {"status", result_string(result.status)}, {"detail", object_json(result.detail)}};
}
Ack read_ack(const Json& body) {
    Ack a{body.at("ack_for_msg_id").get<std::string>(), body.at("task_id").get<std::string>(),
          body.at("accepted").get<bool>(), optional_string(body.at("reject_reason"))};
    validate_ack(a);
    return a;
}
Result read_result(const Json& body) {
    Result r{body.at("task_id").get<std::string>(), read_status(body.at("status").get<std::string>()),
             read_object(body.at("detail"))};
    require_id(r.task_id);
    return r;
}
std::string state_string(DeviceTaskState state) {
    switch (state) {
        case DeviceTaskState::unknown: return "UNKNOWN";
        case DeviceTaskState::rejected: return "REJECTED";
        case DeviceTaskState::running: return "RUNNING";
        case DeviceTaskState::terminal: return "TERMINAL";
    }
    throw std::runtime_error("Unknown device task state");
}
DeviceTaskState read_state(const std::string& s) {
    if (s == "UNKNOWN") return DeviceTaskState::unknown;
    if (s == "REJECTED") return DeviceTaskState::rejected;
    if (s == "RUNNING") return DeviceTaskState::running;
    if (s == "TERMINAL") return DeviceTaskState::terminal;
    throw std::runtime_error("Unknown device task state");
}
}

std::string encode_json(const Message& message) {
    try {
        const auto& e = message.envelope;
        const bool is_link = std::holds_alternative<Heartbeat>(message.body) ||
            std::holds_alternative<ResyncRequest>(message.body) || std::holds_alternative<ResyncState>(message.body);
        validate_envelope(e, is_link);
        Json body;
        std::string type;
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Event>) {
                if (value.severity > 2) throw std::runtime_error("Invalid event severity");
                type = "EVENT"; body = {{"event_code", value.event_code}, {"severity", value.severity}, {"payload", object_json(value.payload)}};
            } else if constexpr (std::is_same_v<T, Task>) {
                require_id(value.task_id); type = "TASK";
                body = {{"task_id", value.task_id}, {"action", value.action}, {"params", object_json(value.params)},
                        {"expire_at_unix_ms", value.expire_at_unix_ms}, {"max_clock_skew_ms", value.max_clock_skew_ms}, {"execution_timeout_ms", value.execution_timeout_ms}};
            } else if constexpr (std::is_same_v<T, Cancel>) {
                require_id(value.task_id); type = "CANCEL";
                body = {{"task_id", value.task_id}, {"reason", value.reason}, {"expire_at_unix_ms", value.expire_at_unix_ms}, {"max_clock_skew_ms", value.max_clock_skew_ms}};
            } else if constexpr (std::is_same_v<T, Ack>) { type = "ACK"; body = ack_json(value);
            } else if constexpr (std::is_same_v<T, Result>) { type = "RESULT"; body = result_json(value);
            } else if constexpr (std::is_same_v<T, Receipt>) { require_id(value.ack_for_msg_id); type = "RECEIPT"; body = {{"ack_for_msg_id", value.ack_for_msg_id}};
            } else if constexpr (std::is_same_v<T, Heartbeat>) { type = "LINK"; body = {{"kind", "HEARTBEAT"}, {"hb_seq", value.hb_seq}};
            } else if constexpr (std::is_same_v<T, ResyncRequest>) { type = "LINK"; body = {{"kind", "RESYNC_REQUEST"}, {"request_id", value.request_id}, {"task_id", value.task_id}};
            } else {
                type = "LINK"; body = {{"kind", "RESYNC_STATE"}, {"request_id", value.request_id}, {"task_id", value.task_id},
                    {"task_state", state_string(value.task_state)}, {"task_ack", value.task_ack ? ack_json(*value.task_ack) : Json(nullptr)},
                    {"result", value.result ? result_json(*value.result) : Json(nullptr)}};
            }
        }, message.body);
        Json result = {{"version", e.version}, {"type", type}, {"msg_id", e.msg_id}, {"session_id", e.session_id},
            {"dst_session_id", e.dst_session_id ? Json(*e.dst_session_id) : Json(nullptr)}, {"src_id", e.src_id},
            {"dst_id", e.dst_id}, {"trace_id", e.trace_id ? Json(*e.trace_id) : Json(nullptr)},
            {"ts_mono_ms", e.ts_mono_ms}, {"body", body}};
        const auto data = result.dump();
        if (data.size() > max_datagram_bytes) throw std::runtime_error("JSON exceeds 1200 bytes");
        return data;
    } catch (const nlohmann::json::exception& ex) { throw std::runtime_error(std::string("JSON encoding: ") + ex.what()); }
}

Message decode_json(const std::string& data) {
    if (data.size() > max_datagram_bytes) throw std::runtime_error("JSON exceeds 1200 bytes");
    try {
        const auto input = Json::parse(data);
        const auto& b = input.at("body");
        const auto type = input.at("type").get<std::string>();
        Envelope e{static_cast<std::uint16_t>(unsigned_value(input.at("version"), std::numeric_limits<std::uint16_t>::max())),
            input.at("msg_id").get<std::string>(), input.at("session_id").get<std::string>(), optional_string(input.at("dst_session_id")),
            input.at("src_id").get<std::string>(), input.at("dst_id").get<std::string>(), optional_string(input.at("trace_id")), u64(input, "ts_mono_ms")};
        validate_envelope(e, type == "LINK");
        MessageBody body;
        if (type == "EVENT") body = Event{b.at("event_code").get<std::string>(), static_cast<std::uint8_t>(unsigned_value(b.at("severity"), 2)), read_object(b.at("payload"))};
        else if (type == "TASK") body = Task{b.at("task_id").get<std::string>(), b.at("action").get<std::string>(), read_object(b.at("params")), u64(b, "expire_at_unix_ms"), u32(b, "max_clock_skew_ms"), u32(b, "execution_timeout_ms")};
        else if (type == "CANCEL") body = Cancel{b.at("task_id").get<std::string>(), b.at("reason").get<std::string>(), u64(b, "expire_at_unix_ms"), u32(b, "max_clock_skew_ms")};
        else if (type == "ACK") body = read_ack(b);
        else if (type == "RESULT") body = read_result(b);
        else if (type == "RECEIPT") body = Receipt{b.at("ack_for_msg_id").get<std::string>()};
        else if (type == "LINK") {
            const auto kind = b.at("kind").get<std::string>();
            if (kind == "HEARTBEAT") body = Heartbeat{u32(b, "hb_seq")};
            else if (kind == "RESYNC_REQUEST") body = ResyncRequest{b.at("request_id").get<std::string>(), b.at("task_id").get<std::string>()};
            else if (kind == "RESYNC_STATE") body = ResyncState{b.at("request_id").get<std::string>(), b.at("task_id").get<std::string>(), read_state(b.at("task_state").get<std::string>()),
                b.at("task_ack").is_null() ? std::nullopt : std::optional<Ack>(read_ack(b.at("task_ack"))),
                b.at("result").is_null() ? std::nullopt : std::optional<Result>(read_result(b.at("result")))};
            else throw std::runtime_error("Unknown LINK kind");
        } else throw std::runtime_error("Unknown message type");
        Message message{e, body};
        (void)encode_json(message);
        return message;
    } catch (const nlohmann::json::exception& ex) { throw std::runtime_error(std::string("Invalid JSON message: ") + ex.what()); }
}

}  // namespace c2
