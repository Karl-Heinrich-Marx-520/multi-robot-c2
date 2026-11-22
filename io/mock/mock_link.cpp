#include "io/mock/mock_link.hpp"

#include <cstddef>
#include <deque>
#include <map>
#include <utility>
#include <vector>

namespace c2 {
namespace {
class CallbackSubscription final : public Subscription {
public:
    explicit CallbackSubscription(std::function<void()> cleanup)
        : cleanup_(std::move(cleanup)) {}
    ~CallbackSubscription() override { cleanup_(); }
private:
    std::function<void()> cleanup_;
};
}  // namespace

struct MockLink::State {
    std::deque<Message> sent;
    std::map<std::size_t, Handler> handlers;
    std::size_t next_id{};
};

MockLink::MockLink() : state_(std::make_shared<State>()) {}
MockLink::~MockLink() = default;

SendStatus MockLink::send(const Message& message, const SendOptions& options) {
    if (std::chrono::steady_clock::now() >= options.expires_at) {
        return SendStatus::expired;
    }
    state_->sent.push_back(message);
    return SendStatus::queued;
}

std::unique_ptr<Subscription> MockLink::subscribe(Handler handler) {
    const auto id = state_->next_id++;
    state_->handlers.emplace(id, std::move(handler));
    return std::make_unique<CallbackSubscription>([weak = std::weak_ptr<State>(state_), id] {
        if (const auto state = weak.lock()) {
            state->handlers.erase(id);
        }
    });
}

LinkStats MockLink::stats(const std::string&) const { return {}; }

std::optional<Message> MockLink::take_sent() {
    if (state_->sent.empty()) {
        return std::nullopt;
    }
    auto message = std::move(state_->sent.front());
    state_->sent.pop_front();
    return message;
}

void MockLink::inject(const IoEvent& event) {
    // Snapshot IDs: handlers can unsubscribe themselves or others during a callback.
    std::vector<std::size_t> ids;
    for (const auto& entry : state_->handlers) {
        ids.push_back(entry.first);
    }
    for (const auto id : ids) {
        const auto it = state_->handlers.find(id);
        if (it != state_->handlers.end()) {
            const auto handler = it->second;
            handler(event);
        }
    }
}

}  // namespace c2
