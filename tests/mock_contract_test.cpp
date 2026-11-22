#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <variant>

#include "io/mock/mock_link.hpp"

int main() {
    c2::MockLink link;
    c2::Message request;
    request.envelope.msg_id = "11111111111111111111111111111111";
    request.body = c2::Task{};
    int callbacks = 0;
    auto subscription = link.subscribe([&callbacks](const c2::IoEvent&) { ++callbacks; });

    const auto future = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    if (link.send(request, {future}) != c2::SendStatus::queued || callbacks != 0) {
        std::cerr << "Queue acceptance must not imply delivery.\n";
        return 1;
    }
    const auto sent = link.take_sent();
    if (!sent || sent->envelope.msg_id != request.envelope.msg_id || link.take_sent()) {
        std::cerr << "Outbound message was not preserved exactly once.\n";
        return 1;
    }
    if (link.send(request, {std::chrono::steady_clock::now() - std::chrono::seconds(1)})
            != c2::SendStatus::expired || link.take_sent()) {
        std::cerr << "Expired messages must not enter the outbound queue.\n";
        return 1;
    }
    const c2::IoFact failure{c2::IoFactKind::delivery_unknown, "car_01",
                            request.envelope.msg_id, std::nullopt};
    link.inject(failure);
    if (callbacks != 1) {
        return 1;
    }
    subscription.reset();
    link.inject(failure);
    if (callbacks != 1 || link.stats("car_01").frame_age) {
        std::cerr << "Subscription lifetime or missing-sample semantics failed.\n";
        return 1;
    }
    std::unique_ptr<c2::Subscription> late_subscription;
    {
        c2::MockLink temporary;
        late_subscription = temporary.subscribe([](const c2::IoEvent&) {});
    }
    late_subscription.reset();
    std::cout << "Mock contract checks passed.\n";
}
