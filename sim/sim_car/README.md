# 小车模拟演示

从仓库根目录运行：

```bash
cmake -S . -B build/local-make -G "Unix Makefiles"
cmake --build build/local-make -j2
ctest --test-dir build/local-make --output-on-failure
./build/local-make/bin/sim_car
```

演示进程内 GUIDE → ACK → 模拟完成 → RESULT，并证明重复任务只执行一次。
演示使用虚拟 UTC/单调时间推进，不需要等待或真实小车。
当前没有 UDP、JSON、心跳、RECEIPT 或网络重传，不能与中控联网联调。
处理模块说明见 `agents/car/README.md`。
