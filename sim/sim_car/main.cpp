#include <iostream>

#include "sim/sim_car/simulator.hpp"

int main() {
    const std::string car_session(32, 'a');
    unsigned int sequence = 0;
    c2::car::Simulator car("car_01", car_session, [&sequence] {
        const auto suffix = std::to_string(++sequence);
        return std::string(32 - suffix.size(), '0') + suffix;
    });
    const auto now = c2::car::TaskProcessor::Clock::time_point{};
    const c2::Task guide{std::string(32, 'b'), "GUIDE",
        {{"channel", c2::JsonValue{std::string("A")}},
         {"target", c2::JsonValue{std::string("A_exit")}},
         {"speed_mps", c2::JsonValue{0.5}}}, 10000, 0, 3000};
    const c2::Message request{{c2::protocol_version, std::string(32, 'c'), std::string(32, 'd'),
                               car_session, "center", "car_01", std::string(32, 'e'), 0}, guide};

    const auto replies = car.receive(request, 100, now);
    const auto& ack = std::get<c2::Ack>(replies.at(0).body);
    std::cout << "[in-memory demo] GUIDE -> ACK accepted=" << std::boolalpha
              << ack.accepted << " ack_for_msg_id=" << ack.ack_for_msg_id << '\n';
    const auto repeated = car.receive(request, 100, now);
    std::cout << "Repeated GUIDE: same ACK msg_id="
              << (repeated.at(0).envelope.msg_id == replies.at(0).envelope.msg_id) << '\n';

    for (const auto& completion : car.tick(now + std::chrono::seconds(1))) {
        const auto& result = std::get<c2::Result>(completion.body);
        std::cout << "RESULT task=" << result.task_id << " trace=" << completion.envelope.trace_id.value()
                  << " status=" << (result.status == c2::ResultStatus::success ? "SUCCESS" : "FAILED") << '\n';
    }
    std::cout << "UDP, JSON, heartbeat and RESULT delivery are not connected yet.\n";
}
