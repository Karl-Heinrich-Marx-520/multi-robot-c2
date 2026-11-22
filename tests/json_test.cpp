#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

#include "io/api/io.hpp"

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int main() {
    try {
        const auto id = c2::make_id();
        c2::Envelope env{1, c2::make_id(), c2::make_id(), std::nullopt, c2::center_id, c2::car_id, id, 123};
        c2::Task task{id, "GUIDE", {{"channel", c2::JsonValue{std::string("A")}},
                                    {"target", c2::JsonValue{std::string("A_exit")}},
                                    {"speed_mps", c2::JsonValue{0.5}}}, 123456, 0, 1000};
        const auto parsed = c2::decode_json(c2::encode_json({env, task}));
        check(std::get<c2::Task>(parsed.body).task_id == id, "task ID changed");
        check(std::get<double>(std::get<c2::Task>(parsed.body).params.at("speed_mps").value) == 0.5, "speed changed");
        const c2::Ack ack{env.msg_id, id, false, "BUSY"};
        check(std::get<c2::Ack>(c2::decode_json(c2::encode_json({env, ack})).body).reject_reason == "BUSY", "ACK mismatch");
        const c2::Result result{id, c2::ResultStatus::success, {{"arrived", c2::JsonValue{true}}}};
        check(std::get<c2::Result>(c2::decode_json(c2::encode_json({env, result})).body).status == c2::ResultStatus::success, "RESULT mismatch");
        bool invalid = false;
        try { (void)c2::decode_json("{bad"); } catch (const std::runtime_error&) { invalid = true; }
        check(invalid, "Malformed JSON accepted");
        invalid = false;
        try { (void)c2::decode_json(std::string(1201, 'x')); } catch (const std::runtime_error&) { invalid = true; }
        check(invalid, "Oversized JSON accepted");
        invalid = false;
        try { (void)c2::encode_json({env, c2::Ack{env.msg_id, id, true, "BUSY"}}); }
        catch (const std::runtime_error&) { invalid = true; }
        check(invalid, "Inconsistent ACK accepted");
        std::cout << "JSON contract checks passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
