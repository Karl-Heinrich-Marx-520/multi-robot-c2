#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

#include "io/api/io.hpp"

namespace {
using namespace std::chrono_literals;

std::uint16_t parse_port(const std::string& text) {
    std::size_t position = 0;
    const auto value = std::stoul(text, &position);
    if (position != text.size() || value == 0 || value > 65535) throw std::runtime_error("Invalid port: " + text);
    return static_cast<std::uint16_t>(value);
}

int run_guide(const c2::Ports& ports) {
    c2::Io io(ports);
    const auto session = c2::make_id();
    const auto task_id = c2::make_id();
    const auto task_msg_id = c2::make_id();
    c2::Envelope envelope{c2::protocol_version, task_msg_id, session, std::nullopt,
                          c2::center_id, c2::car_id, task_id, c2::monotonic_time_ms()};
    c2::Task task{task_id, "GUIDE", {{"channel", c2::JsonValue{std::string("A")}},
                                  {"target", c2::JsonValue{std::string("A_exit")}},
                                  {"speed_mps", c2::JsonValue{0.5}}},
                  c2::unix_time_ms() + 10000, 0, 10000};
    io.send(c2::Message{envelope, task});
    std::cout << "Sent GUIDE task_id=" << task_id << std::endl;

    bool accepted = false;
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline) {
        std::optional<c2::Message> response;
        try { response = io.receive(500ms); }
        catch (const std::runtime_error& error) { std::cerr << "Ignored packet: " << error.what() << std::endl; continue; }
        if (!response || response->envelope.src_id != c2::car_id ||
            response->envelope.trace_id != envelope.trace_id ||
            response->envelope.dst_session_id != session) continue;
        if (const auto* ack = std::get_if<c2::Ack>(&response->body)) {
            if (ack->task_id != task_id || ack->ack_for_msg_id != task_msg_id) continue;
            std::cout << "ACK accepted=" << (ack->accepted ? "true" : "false");
            if (ack->reject_reason) std::cout << " reason=" << *ack->reject_reason;
            std::cout << std::endl;
            if (!ack->accepted) return 2;
            accepted = true;
        } else if (const auto* result = std::get_if<c2::Result>(&response->body)) {
            if (result->task_id != task_id) continue;
            c2::Envelope receipt{c2::protocol_version, c2::make_id(), session,
                response->envelope.session_id, c2::center_id, c2::car_id,
                envelope.trace_id, c2::monotonic_time_ms()};
            io.send(c2::Message{receipt, c2::Receipt{response->envelope.msg_id}});
            std::cout << "RESULT status=" << (result->status == c2::ResultStatus::success ? "SUCCESS" :
                result->status == c2::ResultStatus::failed ? "FAILED" : "ABORTED")
                      << (accepted ? "" : " (ACK not received)") << std::endl;
            return result->status == c2::ResultStatus::success ? 0 : 3;
        }
    }
    std::cerr << "Timed out waiting for car; delivery or result is unknown." << std::endl;
    return 4;
}
}

int main(int argc, char** argv) {
    try {
        c2::Ports ports;
        bool guide = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--guide") { guide = true; continue; }
            if (arg == "--help") {
                std::cout << "Usage: c2_center --guide [--center-port N] [--car-port N] [--uav-port N] [--dog-port N]\n";
                return 0;
            }
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
            const auto value = parse_port(argv[++i]);
            if (arg == "--center-port") ports.center = value;
            else if (arg == "--car-port") ports.car = value;
            else if (arg == "--uav-port") ports.uav = value;
            else if (arg == "--dog-port") ports.dog = value;
            else throw std::runtime_error("Unknown option: " + arg);
        }
        if (!guide) { std::cout << "Run with --guide for a single loopback GUIDE test; use --help for ports.\n"; return 0; }
        return run_guide(ports);
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
