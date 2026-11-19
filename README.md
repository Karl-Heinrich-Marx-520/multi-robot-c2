# multi-robot-c2

多机器人中控。首版目标：机器狗上报堵塞 → 中控派发无人机观察、四驱车引导 → 收到回执与结果。

当前：C++17 工程骨架与手动 mock；UDP、调度和设备接入待实现。

## 分工

| 负责人 | 工作区 | 职责 |
|---|---|---|
| 丞翰 | `io/`、`proto/` | 通信、公共协议 |
| 子延 | `logic/` | 派单、任务状态机 |
| 四驱车负责人 | `agents/car/`、`sim/sim_car/` | GUIDE、回执与结果 |
| 无人机负责人 | `agents/uav/`、`sim/sim_uav/` | OBSERVE、回执与结果 |
| 机器狗负责人 | `agents/dog/`、`sim/sim_dog/` | 堵塞事件上报 |

`app/` 组装中控；`configs/` 放示例配置；`scripts/` 放脚本；`tests/` 放测试。修改公共协议或跨模块接口，请相关负责人审阅。

## 构建

需要 CMake ≥3.21、Ninja、C++17 编译器；Windows MSVC 使用 Developer PowerShell。

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

输出在 `build/dev/bin/`。`c2_center`、`sim_car`、`sim_uav`、`sim_dog` 当前只打印状态后退出。

## 提交

接受邀请后克隆仓库；每项任务从最新 `main` 建分支：

```bash
git clone https://github.com/Karl-Heinrich-Marx-520/multi-robot-c2.git
cd multi-robot-c2
git switch main
git pull --ff-only origin main
git switch -c feat/your-task
# 修改并验证代码，替换下方路径和提交说明
git add <修改的文件>
git commit -m "feat(模块): 具体改动"
git push -u origin feat/your-task
```

在 GitHub 创建 PR → `main`，写明改动和验证结果，请相关负责人审阅后合并。无需提交构建产物、日志、厂商资料包或密码。

[接口协议](docs/01-接口协议.md) · [设备接入](docs/02-设备资料核查.md)
