# 通信层

`api/io.hpp` 提供中控 UDP 收发、JSON 编解码和 ID 生成；`src/` 是实现。`Io::send()` 发出一个数据报，成功只表示操作系统接收，不代表设备受理；`Io::receive(timeout)` 返回收到的消息或超时。首轮仅支持本机回环，自动重传和会话恢复尚未实现。

无设备时可运行 `scripts/sim_car.py`，用另一个 UDP 进程测试；消息格式和端口见[接口协议](../docs/01-接口协议.md)。

## C++ 接口

本仓库的 C++ 模块直接 `#include "io/api/io.hpp"` 并链接 `c2_io`，无需复制通信源码。`json.cpp` 提供 `encode_json()`、`decode_json()`；
`udp.cpp` 实现 UDP `send()`、`receive()`，
中控通过 `Io::send()`、`Io::receive()` 调用。

## 调度调用示例

下面示例发送一个 GUIDE，并等待一条消息。先在另一个终端运行 `python scripts/sim_car.py`；实际调度需要持续接收并关联消息，不能假设第一条一定是本任务的 ACK。完整的单次 GUIDE 处理可参考 `app/main.cpp`。

```cpp
#include <chrono>
#include <iostream>
#include <variant>
#include "io/api/io.hpp"

int main() {
    c2::Io io;  // 默认绑定中控端口 19000
    const auto session_id = c2::make_id();
    const auto task_id = c2::make_id();
    const auto msg_id = c2::make_id();
    c2::Envelope envelope{c2::protocol_version, msg_id, session_id, std::nullopt,
                          c2::center_id, c2::car_id, task_id, c2::monotonic_time_ms()};
    c2::Task task{task_id, "GUIDE",
                  {{"channel", c2::JsonValue{std::string("A")}},
                   {"target", c2::JsonValue{std::string("A_exit")}},
                   {"speed_mps", c2::JsonValue{0.5}}},
                  c2::unix_time_ms() + 10000, 0, 10000};
    io.send(c2::Message{envelope, task});

    auto message = io.receive(std::chrono::milliseconds(2000));
    if (!message) {
        std::cout << "No reply yet; task state is unknown\n";
    } else if (const auto* ack = std::get_if<c2::Ack>(&message->body);
               ack && message->envelope.src_id == c2::car_id &&
               message->envelope.dst_session_id == session_id &&
               ack->task_id == task_id && ack->ack_for_msg_id == msg_id) {
        std::cout << "Accepted: " << ack->accepted << '\n';
    }
}
```

调度程序的 CMake 目标链接 `c2_io` 即可使用该接口。`Io::send()` 和 `Io::receive()` 失败时会抛出 `std::runtime_error`；`receive()` 超时返回空值，不能据此判断设备没有执行。`Io` 只供单线程使用，同一中控端口应由一个 `Io` 实例持有。调度还需继续接收 RESULT 并发送 RECEIPT，示例为突出基本调用省略了这一步。

## 机器人侧同学复用

机器人程序不需要复制 `json.cpp`、`udp.cpp` 或 `io.cpp`，直接包含声明并链接 `c2_io`：

```cpp
#include "io/api/io.hpp"   // Message、encode_json()、decode_json()、make_id() 等
#include "io/src/udp.hpp"  // UdpSocket::send()、UdpSocket::receive()
```

机器人侧使用 `UdpSocket` 绑定自己的设备端口（小车默认 `19001`），向中控端口（默认 `19000`）收发数据。收到数据后调用 `decode_json()`，把得到的 TASK 交给自己的设备逻辑（例如 `TaskProcessor`）处理；再用 `encode_json()` 编码 ACK、RESULT，通过 `UdpSocket::send()` 发回中控。构造回复时可调用 `make_id()`、`unix_time_ms()` 和 `monotonic_time_ms()`。

机器人侧不调用 `io.cpp` 中面向中控的 `Io::send()`、`Io::receive()`；`io.cpp` 已随 `c2_io` 编译，机器人只需链接库即可复用其中需要的公共函数。当前 UDP 地址固定为 `127.0.0.1`，适用于同机联调。
