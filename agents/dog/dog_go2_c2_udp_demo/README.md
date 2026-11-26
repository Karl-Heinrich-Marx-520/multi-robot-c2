# Go2 狗端人工通信模拟工作空间

## 使用前提

**当前机器狗的实际业务功能尚未就绪，本工作空间用于人工通信模拟。** 人工输入事件、决定是否受理任务、填写成功/失败/中止结果，验证中控与狗端的消息交互。程序不调用 Go2 运动 SDK，不执行导航、引导、观察等真实动作，也未接入真实电量、到达或故障检测。

通信经过真实电脑与 Jetson 的 UDP 链路；业务事件和执行过程是人工模拟的。ACK 表示模拟受理或拒绝，RESULT 表示人工填写的模拟终态，均不能作为机器人实际执行或停止的证明。

## 功能

- EVENT 事件上报；TASK/CANCEL 接收；人工 ACK 和 RESULT。
- RECEIPT 自动确认、LINK 会话/任务状态查询，以及重传、去重、超时、时钟校验。
- 独立交互脚本输入命令；完整 JSON 报文保存在 `logs/`。
- 任务状态只保存在进程内，重启后丢失。

## 启动（在狗端 Jetson 上）

消息进程终端：

```bash
cd /home/jetson/go2_c2_udp_demo
./start_dog.sh
```

另一个 SSH 终端：

```bash
cd /home/jetson/go2_c2_udp_demo
./dog_send.sh
```

输入 `event A 0.8 1`、`status`、`accept last`、`reject last CAPABILITY`、`result last SUCCESS {"note":"人工模拟"}` 等命令。PC 需运行配套 `start_center.sh`，另用 `center_send.sh` 下发模拟任务。默认 PC 为 `192.168.55.100:19000`，Jetson 为 `192.168.55.1:19003`，需要链路和时钟检查通过；本机副本的默认狗端 IP 不能直接绑定到普通电脑。

## 文件与来源

本目录完整复制自 `jetson@192.168.55.1:/home/jetson/go2_c2_udp_demo`，包含源码、测试、日志、备份和运行时缓存，是运行中的工作空间快照。原说明保留为 `README.jetson.md`；逐项测试命令见 `TEST_MESSAGES.md`，复制校验信息见 `COPY_MANIFEST.json`。
