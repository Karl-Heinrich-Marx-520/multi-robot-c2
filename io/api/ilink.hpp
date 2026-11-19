#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "proto/messages.hpp"

namespace c2 {

enum class SendStatus { queued, rejected, expired };
struct SendOptions {
    std::chrono::steady_clock::time_point expires_at;
};
enum class IoFactKind {
    delivered, expired_unsent, delivery_unknown, link_down, link_up, report_unconfirmed
};
struct IoFact {
    IoFactKind kind;
    std::string dev_id;
    std::string msg_id;
    std::optional<std::string> task_id;
};
using IoEvent = std::variant<Message, IoFact>;
struct LinkStats {
    std::optional<double> rtt_p95_ms;
    std::optional<double> loss_rate;
    std::optional<double> jitter_ms;
    std::optional<std::chrono::milliseconds> frame_age;
};

class Subscription {
public:
    virtual ~Subscription() = default;
};

class ILink {
public:
    using Handler = std::function<void(const IoEvent&)>;
    virtual ~ILink() = default;
    // queued means local ownership only; it is not a device acknowledgement.
    virtual SendStatus send(const Message& message, const SendOptions& options) = 0;
    // Keep the returned RAII handle alive to keep receiving callbacks.
    virtual std::unique_ptr<Subscription> subscribe(Handler handler) = 0;
    virtual LinkStats stats(const std::string& dev_id) const = 0;
};

}  // namespace c2
