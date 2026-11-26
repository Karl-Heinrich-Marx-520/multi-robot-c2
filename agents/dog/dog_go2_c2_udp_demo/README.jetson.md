# Go2 / Jetson 与中控人工消息联调

从本机 `Go2/c2_udp_demo` 的 Lite3 联调程序迁移。只实现消息与人工受理，不调用运动 SDK。

2026-11-22 核对仓库 `docs/01-接口协议.md` v0.4、`docs/03-业务消息约定.md`。03 当前无已确认的设备业务；GUIDE 只是四驱车填写示例，不代表 Go2 已约定或具备该动作。

## 运行

电脑（USB 网卡当前为 192.168.55.100）：

```bash
cd /home/jgy/FIRE/Go2/go2_c2_udp_demo
./start_center.sh
```

Jetson（SSH 用户 jetson，USB 地址 192.168.55.1）：

```bash
cd /home/jetson/go2_c2_udp_demo
./start_dog.sh
```

C2 使用 `c2` / `dog_01` 和 UDP 19000 / 19003。电脑与 Jetson 使用真实 USB IP；`--bind` / `--peer` 可覆盖。单机模拟才给两端传 `--bind 127.0.0.1 --peer 127.0.0.1`，同机狗端可传 `--clock-bound-ms 0`。

连接 Go2 的本机 DDS IP 是另一个配置：父目录的 `go2_keyboard_control.py --address` 默认值和 `setup_go2_ros2.sh` 的 NetworkInterface 均按用户要求改成 `127.0.0.1`。尚未用此地址验证实机 DDS，不以消息联调证明 SDK 可用。

## 时钟

中控同时在 USB 地址的 UDP 19123 提供只读 NTP 应答，使用主机 chrony 当前同步状态、root delay/dispersion；主机未同步则返回未同步标志。狗端每 10 秒测量时钟偏差上界；未知或超限时拒绝任务。需要主机已安装并同步 chrony。

Jetson 初次检查时日期落后约 11 天。已通过下面的显式单次同步校正。重启后如果日期不正确，先保持电脑中控运行，再在 Jetson 执行：

```bash
cd /home/jetson/go2_c2_udp_demo
sudo python3 sync_clock.py
./start_dog.sh
```

`sync_clock.py` 验证 NTP 来源同步状态及不确定度，随后设置 Jetson 系统时间，需要 sudo。未新增自启动服务；Jetson 原有 timesyncd 配置保持原状。本次尝试的高端口 timesyncd 配置不被该版本识别，已移除。时钟跳变后测量会重新进行，不通过扩大允许误差绕过时钟检查。

## 独立命令入口

更新后需要手动重启中控和狗端各一次以加载本地命令接口；旧进程不会自动升级，重启会清空内存任务。

狗端日志终端继续运行 `./start_dog.sh`。在另一个 Jetson SSH shell 中：

```bash
cd /home/jetson/go2_c2_udp_demo
./dog_send.sh event A 0.8 1
./dog_send.sh status
```

执行 `./dog_send.sh` 不带参数会进入干净的交互提示符。中控使用相同方式的 `./center_send.sh`。两个入口仅通过本机回环地址把命令交给原有 agent，公共字段、会话、重传仍由原 agent 管理；退出输入脚本不影响 agent。狗端本地接口 UDP 45992，中控 UDP 45993，只绑定 127.0.0.1。

完整消息清单和逐项测试命令见 [TEST_MESSAGES.md](TEST_MESSAGES.md)。

## 协议与边界

每数据报一个 UTF-8 JSON，最多 1200 字节，线上 version=1。保留原程序的重传、去重、时钟校验、不可变终态、取消与会话恢复，日志在 `logs/wire-*.jsonl`；状态只存内存，重启不恢复任务数据库。

中控每秒发送空 task_id 的 RESYNC_REQUEST，狗端被动回复，不再主动 HEARTBEAT。会话发现使用 `dst_session_id=null` 的空查询，这是本迁移版对文档未确定的会话发现细节所作的实现选择；仓库当前 C++ 编解码允许 null，但其设备查询处理尚未实现，不能据此宣称已与仓库完整中控联调。也支持 v0.4 首轮中控直接发送 null 目标 session 的 TASK，无需先握手。已绑定后不同中控 session 必须先查询建立会话，旧 session 拒收。

目前没有自动业务决策、真实导航/运动、载荷/遥测通道，不编造 03 文档尚未确认的机器狗业务字段。

验证：`python3 -m unittest discover -p 'test_*.py' -v`。


## 本次验证（2026-11-22）

本机与 Jetson 均通过 22 项测试。两机 USB 实测通过 EVENT/RECEIPT、TASK/ACK/SUCCESS/RECEIPT、CAPABILITY 拒绝、CANCEL/ACK/ABORTED/RECEIPT；狗端测得时钟偏差上界 34 ms。验证没有驱动机器人动作，结束后两个联调进程均退出。

本机详细日志 `logs/center-go2-live.log`、`logs/dog-go2-live.log`，完整报文为 `logs/wire-*.jsonl`；`live-verification.json` 记录报文审计结果。


## 独立输入入口验证

26 项测试在本机和 Jetson 通过。另用独立端口、关闭两端 agent 标准输入的方式实测 `dog_send.sh` / `center_send.sh`：六种 EVENT 及其 RECEIPT、接受、拒绝、SUCCESS/FAILED/ABORTED、取消、状态查询均通过。测试没有重启用户正在运行的进程；正式使用新入口前需自行重启旧中控/狗端以加载更新。
