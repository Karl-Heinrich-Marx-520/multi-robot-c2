# 小车模拟演示

从仓库根目录运行：

```bash
cmake -S . -B build/local-make -G "Unix Makefiles"
cmake --build build/local-make -j2
ctest --test-dir build/local-make --output-on-failure
./build/local-make/bin/sim_car
```

演示用进程内的 `c2::Message` 跑通 GUIDE → ACK → 模拟完成 → RESULT。发送相同 TASK 消息时重放原 ACK，重复派发同一任务时保持原有结果。演示用虚拟 UTC 和单调时间推进。

`simulator.hpp` 提供接入入口。`receive()` 接收通信层已经解码和校验的 TASK，`tick()` 返回新增的执行结果。回复带有原任务标识、事件追踪标识和收发双方的会话标识。小车测试直接传入 `c2::Message`，验证回复字段和任务去重。

当前跨进程联调可用 `scripts/sim_car.py` 与 `c2_center --guide`。C++ 模拟器接入时可复用 `c2_io` 的 UDP 与 JSON 编解码，见[通信层说明](../../io/README.md)。任务处理模块见[小车任务处理模块](../../agents/car/README.md)。
