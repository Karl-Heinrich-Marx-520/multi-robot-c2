#pragma once

#include <memory>
#include <optional>

#include "io/api/ilink.hpp"

namespace c2 {

// Single-threaded manual mock. No transport, timers or automatic success replies.
class MockLink final : public ILink {
public:
    MockLink();
    ~MockLink() override;
    MockLink(const MockLink&) = delete;
    MockLink& operator=(const MockLink&) = delete;

    SendStatus send(const Message& message, const SendOptions& options) override;
    std::unique_ptr<Subscription> subscribe(Handler handler) override;
    LinkStats stats(const std::string& dev_id) const override;

    // Test driver observes outbound messages and injects device/failure events.
    std::optional<Message> take_sent();
    void inject(const IoEvent& event);

private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace c2
