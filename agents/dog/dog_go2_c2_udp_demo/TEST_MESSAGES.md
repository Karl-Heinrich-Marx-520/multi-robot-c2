# Go2 通信逐项手工测试

这里只测试人工消息，不执行运动。以下代码块均在普通 shell 中执行；不需要在滚动日志终端输入。

## 准备（更新后重启一次）

原有进程不会自动加载新代码。完成当前测试后，在旧中控、狗端终端分别 Ctrl+C，再按下面启动；重启会清空内存中的任务和旧 last。

电脑日志终端：

```bash
cd /home/jgy/FIRE/Go2/go2_c2_udp_demo
./start_center.sh
```

Jetson 日志终端：

```bash
cd /home/jetson/go2_c2_udp_demo
./start_dog.sh
```

另外开一个 Jetson SSH 终端：

```bash
cd /home/jetson/go2_c2_udp_demo
./dog_send.sh status
```

显示 `link_up: true` 后测试。电脑另开一个 shell，进入 `/home/jgy/FIRE/Go2/go2_c2_udp_demo`，使用 `./center_send.sh`。

也可 `./dog_send.sh` 进入持续交互界面，再输入 `event A 0.8 1`、`accept last` 等。`exit` 只退出输入界面，不停止狗端进程。

脚本显示 QUEUED / OK 仅代表本地消息进程接管或完成操作。实际对端收到要查看对端日志，EVENT/RESULT 的 RECEIPT 在狗端日志查看。

## 已实现的线上消息

| 类型 | 方向 | 如何触发 | 预期 |
|---|---|---|---|
| EVENT | 狗 → 中控 | dog_send 的 event | 中控 EVENT，狗端 RECEIPT |
| TASK | 中控 → 狗 | center_send 的 task | 狗端 TASK_WAIT_OPERATOR，等待人工决定 |
| ACK | 狗 → 中控 | dog_send 的 accept / reject；也有过期等自动拒绝 | 中控 ACK、RUNNING 或 REJECTED |
| RESULT | 狗 → 中控 | dog_send 的 result；执行超时也自动产生 FAILED | 中控 RESULT，狗端 RECEIPT |
| CANCEL | 中控 → 狗 | center_send 的 cancel | 狗端 CANCEL_WAIT_OPERATOR，再接受或拒绝 |
| RECEIPT | 中控 → 狗 | 自动确认有效 EVENT / RESULT | 狗端 RECEIPT；停止对应报告重传 |
| LINK | 中控查询、狗端回复 | 每秒空任务查询；center_send 的 query | 自动建立会话或核对任务状态 |

status 是本地状态查询，不是线上遥测消息。通信公共字段由 agent 生成。没有新增运动、相机、位置遥测或载荷消息。

## 1. 六种 EVENT（Jetson 执行）

```bash
./dog_send.sh event A 0.8 1
./dog_send.sh event B 0.5 1
./dog_send.sh event C 1.0 2
./dog_send.sh event '{"event_code":"LOW_BATTERY","severity":1,"payload":{}}'
./dog_send.sh event '{"event_code":"ARRIVED","severity":0,"payload":{}}'
./dog_send.sh event '{"event_code":"FAULT","severity":2,"payload":{}}'
```

A/B/C 对应 A_BLOCKED/B_BLOCKED/C_BLOCKED；block_ratio 为 0～1，severity 为 0=普通、1=警告、2=严重。LOW_BATTERY/ARRIVED/FAULT 的设备专属 payload 尚未约定，以上使用空对象进行通信测试，不能据此认为已接入电量/到达/故障传感器。

## 2. TASK + 接受 ACK + SUCCESS

电脑：

```bash
./center_send.sh task '{"action":"GUIDE","params":{"channel":"A","target":"A_exit","speed_mps":0.1},"execution_timeout_ms":300000,"expire_in_ms":120000,"max_clock_skew_ms":1000}'
```

Jetson：

```bash
./dog_send.sh status
./dog_send.sh accept last
./dog_send.sh result last SUCCESS '{"note":"manual success test"}'
```

动作和参数人工传入；GUIDE 是协议模拟，Go2 不会移动。OBSERVE 也可测试封装：

```bash
./center_send.sh task '{"action":"OBSERVE","params":{"area":"A","alt_m":15,"loiter_s":0},"execution_timeout_ms":300000,"expire_in_ms":120000,"max_clock_skew_ms":1000}'
```

OBSERVE 示例仍在电脑执行，不代表 Go2 具备飞行能力。

## 3. 拒绝 ACK

电脑重新发一项 TASK，然后 Jetson：

```bash
./dog_send.sh reject last CAPABILITY
```

预期中控 REJECTED，`accepted=false`、`reject_reason=CAPABILITY`；此任务不会产生 RESULT。

支持原因码：BUSY、MODE_MISMATCH、CAPABILITY、BAD_PARAM、EXPIRED、CLOCK_UNSYNCED、UNKNOWN_TASK、CANCELLED、ALREADY_FINISHED、ID_CONFLICT。可手工选择拒绝码测试封装；这不等于实际触发了相应能力/时钟/冲突检测条件。真正的过期、时钟不确定等也会自动拒绝。

## 4. FAILED 结果

电脑重新发一项 TASK，然后 Jetson：

```bash
./dog_send.sh accept last
./dog_send.sh result last FAILED '{"reason":"manual failure test"}'
```

一个任务只能有一个终态，已经 SUCCESS 的任务不能再改报 FAILED。

## 5. CANCEL + 接受 ACK + ABORTED

电脑重新发一项 TASK，Jetson 先：

```bash
./dog_send.sh accept last
```

电脑：

```bash
./center_send.sh cancel last '{"reason":"operator test","expire_in_ms":30000,"max_clock_skew_ms":1000}'
```

Jetson：

```bash
./dog_send.sh status
./dog_send.sh accept last
./dog_send.sh result last ABORTED '{"reason":"manual simulation stop confirmed"}'
```

第二次 accept 是接受 CANCEL。取消 ACK 不等于已停止；这里手工 ABORTED 仅表示模拟结束。

## 6. 查询与恢复消息

电脑：

```bash
./center_send.sh query last
./center_send.sh status
```

狗端自动返回 LINK/RESYNC_STATE，带 UNKNOWN、REJECTED、RUNNING 或 TERMINAL 及对应 ACK/RESULT。根据查询结果，中控日志显示 TASK_STATE；本地 QUEUED 只代表查询已排队，不是同步返回的远端状态。未知任务的边界测试在自动化协议测试中覆盖。

## 7. 自动执行超时与投递过期

执行超时：电脑发 3 秒时限的任务，Jetson 在投递截止前 accept，之后不发 result。约 3 秒后自动模拟 FAILED / EXECUTION_TIMEOUT。

```bash
./center_send.sh task '{"action":"GUIDE","params":{},"execution_timeout_ms":3000,"expire_in_ms":120000,"max_clock_skew_ms":1000}'
```

投递过期：电脑发下面任务，狗端不 accept，约 5 秒内按截止保护自动拒绝 EXPIRED。

```bash
./center_send.sh task '{"action":"GUIDE","params":{},"execution_timeout_ms":300000,"expire_in_ms":5000,"max_clock_skew_ms":1000}'
```

## 多任务与日志

逐项测试最方便使用 last；同时测试多个任务，请将 last 换成完整 task_id。TASK 与 CANCEL 都待决定时，accept/reject 使用 status 的 undecided 列表中对应 msg_id，可明确决定哪个请求。

完整报文在两端 `logs/wire-*.jsonl`，包括 tx/rx、原消息 body、msg_id/task_id/trace_id。状态与去重记录只在当前 agent 内存中；重启后不会恢复旧任务。独立发送脚本不会自动重试本地命令，以免不确定的返回造成重复创建事件/任务；如果本地超时，先检查 status 和日志再决定重发。
