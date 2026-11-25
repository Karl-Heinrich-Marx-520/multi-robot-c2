#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "io/api/io.hpp"
#include "io/src/udp.hpp"
#include "sim/sim_car/simulator.hpp"

int main() {
    using namespace std::chrono_literals;
    constexpr std::uint32_t loopback_address = 0x7f000001u;
    const c2::Ports ports;
    try {
        c2::detail::UdpSocket socket(ports.car);
        c2::car::Simulator car(c2::car_id, c2::make_id(), c2::make_id);
        const auto send_replies = [&](const auto& replies) {
            for (const auto& reply : replies) {
                socket.send(ports.center, c2::encode_json(reply));
            }
        };

        std::cout << c2::car_id << " listening on 127.0.0.1:" << ports.car << std::endl;
        for (;;) {
            try {
                const auto packet = socket.receive(50ms);
                const auto now = c2::car::TaskProcessor::Clock::now();
                send_replies(car.tick(now));
                if (!packet || packet->address != loopback_address || packet->port != ports.center) {
                    continue;
                }
                const auto request = c2::decode_json(packet->data);
                if (request.envelope.src_id == c2::center_id) {
                    send_replies(car.receive(request, c2::unix_time_ms(), now));
                }
            } catch (const std::runtime_error& error) {
                std::cerr << "Car I/O: " << error.what() << '\n';
            }
        }
    } catch (const std::runtime_error& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
