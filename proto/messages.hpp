#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace c2 {

inline constexpr std::uint16_t protocol_version = 1;
inline constexpr std::size_t max_datagram_bytes = 1200;

// In-memory JSON model only. Encoding, decoding and validation are not implemented.
struct JsonValue {
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double,
                 std::string, Object, Array> value{nullptr};
};
using JsonObject = JsonValue::Object;

struct Envelope {
    std::uint16_t version{protocol_version};
    std::string msg_id;
    std::string session_id;
    std::optional<std::string> dst_session_id;
    std::string src_id;
    std::string dst_id;
    std::optional<std::string> trace_id;
    std::uint64_t ts_mono_ms{};
};

struct Event {
    std::string event_code;
    std::uint8_t severity{};
    JsonObject payload;
};
struct Task {
    std::string task_id;
    std::string action;
    JsonObject params;
    std::uint64_t expire_at_unix_ms{};
    std::uint32_t max_clock_skew_ms{};
    std::uint32_t execution_timeout_ms{};
};
struct Cancel {
    std::string task_id;
    std::string reason;
    std::uint64_t expire_at_unix_ms{};
    std::uint32_t max_clock_skew_ms{};
};
struct Ack {
    std::string ack_for_msg_id;
    std::string task_id;
    bool accepted{};
    std::optional<std::string> reject_reason;
};
enum class ResultStatus { success, failed, aborted };
struct Result {
    std::string task_id;
    ResultStatus status{ResultStatus::failed};
    JsonObject detail;
};
struct Receipt {
    std::string ack_for_msg_id;
};
struct Heartbeat {
    std::uint32_t hb_seq{};
};
struct ResyncRequest {
    std::string request_id;
    std::string task_id;
};
enum class DeviceTaskState { unknown, rejected, running, terminal };
struct ResyncState {
    std::string request_id;
    std::string task_id;
    DeviceTaskState task_state{DeviceTaskState::unknown};
    std::optional<Ack> task_ack;
    std::optional<Result> result;
};

// Heartbeat/ResyncRequest/ResyncState serialize as LINK with different kind values.
using MessageBody = std::variant<Event, Task, Cancel, Ack, Result, Receipt,
                                 Heartbeat, ResyncRequest, ResyncState>;
struct Message {
    Envelope envelope;
    MessageBody body;
};

}  // namespace c2
