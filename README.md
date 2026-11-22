# multi-robot-c2

多机器人中控。首版在一台电脑上运行中控和 3 个模拟设备：机器狗上报堵塞，中控给无人机派观察任务、给四驱车派引导任务，最后显示回执和执行结果。

目前已实现中控本机 UDP、JSON 编解码和单次 GUIDE 联调入口；自动派单、实机执行、心跳与断线恢复尚未实现。

## 目录与工作区

```text
multi-robot-c2/
├── io/                     丞翰：中控通信
│   ├── api/io.hpp          提供给调度调用的通信接口
│   └── src/                UDP 收发与 JSON 编解码
├── proto/                  丞翰主维护：大家共用的消息定义
├── logic/                  子延：派单规则、任务状态与重试决策
├── agents/                 各设备负责人：连接真实设备
│   ├── car/                四驱车：接收引导任务，调用 ROS 导航
│   ├── uav/                无人机：接收观察任务，调用飞控接口
│   └── dog/                机器狗：调用 SDK，上报堵塞事件
├── sim/                    各设备负责人：单机联调用的模拟设备
│   ├── sim_car/            四驱车模拟器
│   ├── sim_uav/            无人机模拟器
│   ├── sim_dog/            机器狗模拟器
│   └── fault/              丞翰：丢包、延迟、断联等故障模拟
├── app/                    丞翰、子延：组装并启动中控
├── configs/                各模块的运行配置示例
├── scripts/                构建、检查等辅助脚本
├── tests/                  各模块的测试
├── docs/                   全组共用的协议与接入说明
├── materials/              设备资料下载入口与文件清单
├── .github/                自动构建检查和 PR 模板
├── CMakeLists.txt          工程构建入口
└── CMakePresets.json        共享构建参数
```

**丞翰主要改 `io/`、`proto/`；子延主要改 `logic/`；设备负责人主要改自己设备的 `agents/` 和 `sim/` 子目录。** `app/` 由中控两人一起接起来；配置、脚本和测试随对应模块维护。公共消息或通信接口需要修改时，先和受影响的同学说清楚，再一起改。

`agents/` 连接真机，`sim/` 模拟设备行为，两者使用相同消息格式；先跑通模拟链路，再接真机。设备负责人尚未确定姓名，按设备角色列出。

## 怎么配合

子延通过 `io/api/io.hpp` 调用通信，丞翰负责消息收发。设备负责人接收任务后返回 ACK（接受或拒绝），执行结束再返回 RESULT（最终结果）。消息格式统一放在 `proto/`，详细字段见[接口协议](docs/01-接口协议.md)。

没有设备时，可用 `scripts/sim_car.py` 模拟小车。各设备的完整模拟器写在 `sim/`，真机接口写在 `agents/`。[模块关系图](docs/00-架构与目录.md)说明这些部分怎样连接。

## 构建

需要 CMake ≥3.21、Ninja、C++17 编译器；Windows 使用 MSVC 时，在 Developer PowerShell 中执行。

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

程序输出在 `build/dev/bin/`。小车联调：在第 1 个终端运行 `python scripts/sim_car.py`，在第 2 个终端运行 `build/dev/bin/c2_center --guide`（Windows 加 `.exe`）。中控会发送一条示例 GUIDE，输出 ACK 与 RESULT。真实小车接入方式、端口与 JSON 示例见[接口协议](docs/01-接口协议.md)。

运行 `ctest --preset dev` 时需要 Python 3 才会执行跨进程 UDP 集成测试。`sim_car.py` 是演示脚本，不连接实机，也不包含重试、去重或安全保护。

## 提交代码

仓库已公开，任何人都能下载。没有仓库写权限的同学先在 GitHub 点击 **Fork**，将仓库复制到自己账号，再克隆自己的副本；已有写权限的同学可直接克隆本仓库。

```bash
git clone https://github.com/<你的GitHub用户名>/multi-robot-c2.git
cd multi-robot-c2
```

每项任务建一个分支。以下以四驱车模拟器为例，其他同学替换分支名和文件路径：

```bash
git switch main
git pull --ff-only origin main
git switch -c feat/sim-car
# 修改代码并验证
git add sim/sim_car/
git commit -m "feat(car): 添加引导任务模拟"
git push -u origin feat/sim-car
```

推送后在 GitHub 点 **Compare & pull request**，目标仓库选 `Karl-Heinrich-Marx-520/multi-robot-c2`，目标分支选 `main`，写清改动和验证结果，请相关同学看过后合并。Fork 使用者开始下一项任务前先在自己副本页面点 **Sync fork → Update branch**，再拉取 main。后续继续修改同一分支，提交后直接 `git push`。构建产物、日志和密码不提交。

## 文档与资料

- [模块关系图](docs/00-架构与目录.md)
- [接口协议](docs/01-接口协议.md)
- [设备接入说明](docs/02-设备资料核查.md)
- [设备参考资料下载](materials/README.md)：协议手册、ROS 源码和 R680 厂商资料，位于 Release 附件。
