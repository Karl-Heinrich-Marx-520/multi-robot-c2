#include <iostream>
#include <limits>
#include <stdexcept>
#include "agents/car/task_processor.hpp"

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
        std::cout << "Car task checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
