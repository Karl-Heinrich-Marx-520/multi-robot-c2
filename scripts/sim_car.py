"""Minimal UDP car peer for manual integration, not a robot controller."""
import argparse
import json
import socket
import time
import uuid


def message(kind, source, destination, trace, session, destination_session, body):
    return {"version": 1, "type": kind, "msg_id": uuid.uuid4().hex,
            "session_id": session, "dst_session_id": destination_session,
            "src_id": source, "dst_id": destination, "trace_id": trace,
            "ts_mono_ms": int(time.monotonic() * 1000), "body": body}


def run(car_port, center_port):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", car_port))
        print(f"Simulated car listening on 127.0.0.1:{car_port}", flush=True)
        session = uuid.uuid4().hex
        while True:
            data, sender = sock.recvfrom(1201)
            if sender != ("127.0.0.1", center_port) or len(data) > 1200:
                continue
            try:
                task = json.loads(data.decode("utf-8"))
                if task["type"] != "TASK" or task["src_id"] != "c2" or task["dst_id"] != "car_01":
                    continue
                body = task["body"]
                if body["action"] != "GUIDE":
                    continue
                if int(time.time() * 1000) + body["max_clock_skew_ms"] >= body["expire_at_unix_ms"]:
                    continue
                if body["execution_timeout_ms"] <= 0:
                    continue
                trace = task["trace_id"]
                dst_session = task["session_id"]
                ack = message("ACK", "car_01", "c2", trace, session, dst_session,
                              {"ack_for_msg_id": task["msg_id"], "task_id": body["task_id"],
                               "accepted": True, "reject_reason": None})
                result = message("RESULT", "car_01", "c2", trace, session, dst_session,
                                 {"task_id": body["task_id"], "status": "SUCCESS",
                                  "detail": {"simulated": True}})
                for reply in (ack, result):
                    sock.sendto(json.dumps(reply, separators=(",", ":")).encode(), sender)
                print(f"Simulated GUIDE {body['task_id']}: ACK + SUCCESS", flush=True)
            except (ValueError, KeyError, UnicodeError) as error:
                print(f"Ignored invalid message: {error}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--car-port", type=int, default=19001)
    parser.add_argument("--center-port", type=int, default=19000)
    args = parser.parse_args()
    try:
        run(args.car_port, args.center_port)
    except KeyboardInterrupt:
        pass
