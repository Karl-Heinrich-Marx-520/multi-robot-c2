#include <iostream>
#include <limits>
#include <stdexcept>
#include "sim/sim_car/simulator.hpp"

using namespace c2;
using Processor = car::TaskProcessor;
using namespace std::chrono_literals;

void check(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
Task guide(std::string id = "task1", std::uint32_t timeout = 3000) {
    return {std::move(id), "GUIDE", {{"channel", JsonValue{std::string("A")}},
            {"target", JsonValue{std::string("A_exit")}}, {"speed_mps", JsonValue{0.5}}},
            10000, 0, timeout};
}

void check_simulator_messages() {
    const std::string car_session(32, 'a');
    const std::string center_session(32, 'b');
    const std::string trace_id(32, 'c');
    const std::string task_id(32, 'd');
    char next_id = '1';
    car::Simulator simulator("car_01", car_session, [&next_id] { return std::string(32, next_id++); });
    const auto start = Processor::Clock::time_point{};
    Message task{{protocol_version, std::string(32, 'e'), center_session, car_session,
                  "center", "car_01", trace_id, 0}, guide(task_id)};

    auto wrong_destination = task;
    wrong_destination.envelope.dst_session_id = center_session;
    check(simulator.receive(wrong_destination, 100, start).empty(), "ignore tasks for another session");
    wrong_destination = task;
    wrong_destination.envelope.dst_id = "uav_01";
    check(simulator.receive(wrong_destination, 100, start).empty(), "ignore tasks for another device");

    const auto replies = simulator.receive(task, 100, start);
    check(replies.size() == 1, "simulator returns one ACK");
    const auto& ack = std::get<Ack>(replies[0].body);
    const auto& ack_envelope = replies[0].envelope;
    check(ack.accepted && ack.task_id == task_id && ack.ack_for_msg_id == task.envelope.msg_id,
          "ACK identifies the original TASK");
    check(ack_envelope.src_id == "car_01" && ack_envelope.dst_id == "center" &&
          ack_envelope.session_id == car_session && ack_envelope.dst_session_id == center_session &&
          ack_envelope.trace_id == trace_id, "ACK carries routing, sessions and trace");

    const auto replay = simulator.receive(task, 101, start + 1ms);
    check(replay.size() == 1 && replay[0].envelope.msg_id == ack_envelope.msg_id,
          "repeated TASK message keeps the original ACK ID");
    check(simulator.tick(start + 999ms).empty(), "no RESULT before simulated completion");
    const auto completed = simulator.tick(start + 1000ms);
    check(completed.size() == 1, "simulator returns one RESULT");
    const auto& result = std::get<Result>(completed[0].body);
    const auto& result_envelope = completed[0].envelope;
    check(result.status == ResultStatus::success && result.task_id == task_id &&
          result_envelope.trace_id == trace_id && result_envelope.dst_session_id == center_session &&
          result_envelope.msg_id != ack_envelope.msg_id, "RESULT is correlated and has its own ID");
    check(simulator.tick(start + 2s).empty(), "simulation produces one terminal message");

    task.envelope.msg_id = std::string(32, 'f');
    const auto terminal_replay = simulator.receive(task, 2200, start + 2100ms);
    check(terminal_replay.size() == 2 &&
          std::get<Ack>(terminal_replay[0].body).ack_for_msg_id == task.envelope.msg_id &&
          terminal_replay[1].envelope.msg_id == result_envelope.msg_id,
          "new message for a completed task receives a new ACK and the original RESULT");
    check(simulator.tick(start + 4s).empty(), "completed task is not executed again");
}

int main() {
    try {
        const auto start = Processor::Clock::time_point{};
        Processor car;
        const auto task = guide();
        const auto first = car.task("c2", "m1", task, 100, start, 0);
        check(first.ack.accepted && !first.ack.reject_reason && !first.result, "accept GUIDE");
        check(car.snapshot("c2", "task1").state == DeviceTaskState::running, "running snapshot");
        check(car.task("c2", "m1", task, 20000, start + 500ms, 0).ack.ack_for_msg_id == "m1", "same message replay");
        check(car.task("c2", "m2", task, 20000, start + 500ms, 0).ack.ack_for_msg_id == "m2", "new message ACK");
        check(car.execution_count() == 1, "dedup execution");
        auto changed = task;
        changed.params["speed_mps"] = JsonValue{0.2};
        check(car.task("c2", "conflict", changed, 100, start, 0).ack.reject_reason == "ID_CONFLICT", "parameter conflict");
        changed = task;
        changed.execution_timeout_ms++;
        check(car.task("c2", "conflict2", changed, 100, start, 0).ack.reject_reason == "ID_CONFLICT", "timeout conflict");
        check(car.task("c2", "busy", guide("task2"), 100, start, 0).ack.reject_reason == "BUSY", "busy rejection");
        check(car.tick(start + 999ms).empty(), "not finished early");
        const auto done = car.tick(start + 1000ms);
        check(done.size() == 1 && done[0].result.status == ResultStatus::success, "completion uses original start");
        check(car.tick(start + 5000ms).empty(), "single terminal result");
        check(!car.task("c2", "busy-again", guide("task2"), 100, start, 0).ack.accepted, "rejection stays rejected");
        check(car.task("c2", "terminal", task, 100, start, 0).result.has_value(), "terminal replay");
        check(car.cancel("c2", "cancel-finished", {"task1", "stop", 10000, 0}, 100, 0).ack.reject_reason == "ALREADY_FINISHED", "cancel terminal");
        check(car.task("other", "m1", task, 100, start, 0).ack.accepted, "controller scoped IDs");

        Processor invalid;
        auto bad = guide();
        bad.params.erase("target");
        check(invalid.task("c2", "bad", bad, 100, start, 0).ack.reject_reason == "BAD_PARAM", "missing parameter");
        bad = guide("nan"); bad.params["speed_mps"] = JsonValue{std::numeric_limits<double>::quiet_NaN()};
        check(invalid.task("c2", "nan", bad, 100, start, 0).ack.reject_reason == "BAD_PARAM", "NaN speed");
        bad = guide("wrong-channel"); bad.params["channel"] = JsonValue{std::string("B")};
        check(invalid.task("c2", "channel", bad, 100, start, 0).ack.reject_reason == "BAD_PARAM", "target channel mismatch");
        bad = guide("unsupported"); bad.action = "OBSERVE";
        check(invalid.task("c2", "action", bad, 100, start, 0).ack.reject_reason == "CAPABILITY", "unsupported action");
        check(invalid.task("c2", "expired", guide("expired"), 10000, start, 0).ack.reject_reason == "EXPIRED", "expiry boundary");
        check(invalid.task("c2", "clock", guide("clock"), 100, start).ack.reject_reason == "CLOCK_UNSYNCED", "unknown clock rejected");

        Processor cancelled;
        check(cancelled.task("c2", "t", task, 100, start, 0).ack.accepted, "start cancellable task");
        auto cancel = cancelled.cancel("c2", "c", {"task1", "stop", 10000, 0}, 100, 0);
        check(cancel.ack.accepted && cancel.result->status == ResultStatus::aborted, "cancel stops simulator");
        check(cancelled.tick(start + 5000ms).empty(), "cancel cannot later succeed");
        check(cancelled.cancel("c2", "c", {"task1", "stop", 10000, 0}, 100, 0).ack.accepted, "cancel ACK replay");
        check(cancelled.cancel("c2", "early", {"later", "stop", 10000, 0}, 100, 0).ack.reject_reason == "UNKNOWN_TASK", "cancel before task");
        check(cancelled.task("c2", "later", guide("later"), 100, start, 0).ack.reject_reason == "CANCELLED", "tombstone rejects late task");
        check(cancelled.cancel("c2", "old", {"fresh", "stop", 100, 0}, 100, 0).ack.reject_reason == "EXPIRED", "expired cancel");
        check(cancelled.task("c2", "fresh", guide("fresh"), 100, start, 0).ack.accepted, "expired cancel has no tombstone");

        Processor timeout;
        timeout.task("c2", "t", guide("timeout", 500), 100, start, 0);
        const auto failed = timeout.tick(start + 2000ms);
        check(failed.size() == 1 && failed[0].result.status == ResultStatus::failed &&
              std::get<std::string>(failed[0].result.detail.at("reason").value) == "EXECUTION_TIMEOUT", "execution timeout");
        check(timeout.snapshot("c2", "timeout").state == DeviceTaskState::terminal, "terminal snapshot");
        Processor delayed;
        delayed.task("c2", "t", guide(), 100, start, 0);
        check(delayed.tick(start + 5000ms).at(0).result.status == ResultStatus::success, "delayed tick preserves earlier success");
        check_simulator_messages();
        std::cout << "Car task checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
