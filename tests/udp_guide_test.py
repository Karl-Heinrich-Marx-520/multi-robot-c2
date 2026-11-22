"""Exercise the center against an independent UDP peer with non-default ports."""
import json
import socket
import subprocess
import sys
import time
import uuid


def peer_message(kind, incoming, session, body):
    return {"version": 1, "type": kind, "msg_id": uuid.uuid4().hex,
            "session_id": session, "dst_session_id": incoming["session_id"],
            "src_id": "car_01", "dst_id": "c2", "trace_id": incoming["trace_id"],
            "ts_mono_ms": int(time.monotonic() * 1000), "body": body}


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as car:
        car.bind(("127.0.0.1", 0))
        car.settimeout(8)
        car_port = car.getsockname()[1]
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
            reserve.bind(("127.0.0.1", 0))
            center_port = reserve.getsockname()[1]
        used = {car_port, center_port}
        other_ports = [p for p in range(23000, 24000) if p not in used][:2]
        process = subprocess.Popen([sys.argv[1], "--guide", "--center-port", str(center_port),
                                    "--car-port", str(car_port), "--uav-port", str(other_ports[0]),
                                    "--dog-port", str(other_ports[1])],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            raw, source = car.recvfrom(1201)
            task = json.loads(raw.decode())
            assert source == ("127.0.0.1", center_port)
            assert task["type"] == "TASK" and task["body"]["action"] == "GUIDE"
            assert task["src_id"] == "c2" and task["dst_id"] == "car_01"
            assert task["body"]["params"] == {"channel": "A", "target": "A_exit", "speed_mps": 0.5}
            session = uuid.uuid4().hex
            task_id = task["body"]["task_id"]
            # An unconfigured source port must not be treated as the car.
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as stranger:
                bad = peer_message("RESULT", task, session,
                        {"task_id": task_id, "status": "FAILED", "detail": {}})
                stranger.sendto(json.dumps(bad).encode(), source)
            ack = peer_message("ACK", task, session, {"ack_for_msg_id": task["msg_id"],
                    "task_id": task_id, "accepted": True, "reject_reason": None})
            result = peer_message("RESULT", task, session,
                    {"task_id": task_id, "status": "SUCCESS", "detail": {"arrived": True}})
            for reply in (ack, result):
                car.sendto(json.dumps(reply).encode(), source)
            receipt, _ = car.recvfrom(1201)
            receipt = json.loads(receipt.decode())
            assert receipt["type"] == "RECEIPT" and receipt["body"]["ack_for_msg_id"] == result["msg_id"]
            output, error = process.communicate(timeout=5)
            assert process.returncode == 0, (output, error)
            assert "ACK accepted=true" in output and "RESULT status=SUCCESS" in output, output
            print(output, end="")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


if __name__ == "__main__":
    main()
