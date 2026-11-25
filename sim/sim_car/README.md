# 小车模拟器

`sim_car` 监听 `127.0.0.1:19001`。收到中控从 `19000` 发来的 GUIDE 后回复 ACK，受理的任务模拟执行约 1 秒后返回 RESULT。

从仓库根目录构建：

```bash
cmake --preset dev
cmake --build --preset dev
```

第一个终端启动小车模拟器，保持运行：

```bash
./build/dev/bin/sim_car
```

第二个终端让中控下发一条 GUIDE：

```bash
./build/dev/bin/c2_center --guide
```

中控显示 `ACK accepted=true` 和 `RESULT status=SUCCESS` 就表示这条模拟链路跑通。
