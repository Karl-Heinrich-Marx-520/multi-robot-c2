#include <iostream>
#include "agents/car/task_processor.hpp"

int main() {
    c2::car::TaskProcessor car;
    const auto now = c2::car::TaskProcessor::Clock::now();
    const c2::Task guide{"demo-task", "GUIDE",
        {{"channel", c2::JsonValue{std::string("A")}},
         {"target", c2::JsonValue{std::string("A_exit")}},
         {"speed_mps", c2::JsonValue{0.5}}}, 10000, 0, 3000};
    const auto reply = car.task("center", "demo-message", guide, 100, now, 0);
    std::cout << "[in-memory demo] GUIDE -> ACK accepted=" << std::boolalpha
              << reply.ack.accepted << '\n';
    car.task("center", "retry-message", guide, 100, now, 0);
    std::cout << "Repeated GUIDE: execution_count=" << car.execution_count() << '\n';
    for (const auto& completion : car.tick(now + std::chrono::seconds(1))) {
        std::cout << "RESULT task=" << completion.result.task_id << " status="
                  << (completion.result.status == c2::ResultStatus::success ? "SUCCESS" : "FAILED") << '\n';
    }
    std::cout << "UDP, JSON, heartbeat and RESULT delivery are not connected yet.\n";
}
