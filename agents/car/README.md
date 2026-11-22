# 小车任务处理模块

`task_processor.hpp` 提供单线程、进程内的任务状态处理和模拟执行；目前不驱动实车。

- `task(controller, msg_id, task, unix_ms, steady_now, clock_error_ms)`：校验 GUIDE、期限、时钟和忙碌状态，返回 ACK；已完成任务可附带缓存 RESULT。
- `cancel(...)`：模拟立即停止并返回 ACK 和 ABORTED；真实导航必须等待确认停止，不能直接照搬模拟行为。
- `tick(steady_now)`：推进模拟执行，返回本次新增终态；超时使用单调时钟。
- `snapshot(controller, task_id)`：查询受理决定、状态和结果，供以后接恢复接口。

任务按 `(controller, task_id)` 保存至对象销毁；消息按 `(controller, msg_id)` 缓存回复。相同任务的新消息返回指向新 msg_id 的 ACK；修改动作、参数或执行时限返回 ID_CONFLICT。拒绝不会自动改为接受。有效取消先到时保存取消记录，迟到任务拒绝为 CANCELLED。

默认目标为 A_exit/B_exit/C_exit，分别属于 A/B/C 通道。默认模拟耗时 1000ms，速度上限 0.5m/s；均为演示配置，不代表已核实的真实车限制。speed_mps 必须为有限正数且不超过配置上限。执行时限必须大于 0。参数比较保留 JsonValue 数值类型，接入 JSON 时须统一解码规则。

`clock_error_ms` 默认未知并拒绝新任务；同机模拟显式传 0。线上由通信/时钟管理提供已知误差上界；时钟跳变后必须重新核对。

本模块消费的是已经通过验证的消息体。上层仍需实现 JSON、UDP、合法 ID/报文大小校验、地址和 session 校验、消息 envelope/trace 关联、RESULT 的稳定消息 ID、RECEIPT 与结果重传、心跳与恢复。它不是完整协议端点，不可直接用于实车控制。

实车接入时应提取执行后端：将模拟定时完成替换为 ROS 导航结果，将立即取消替换为确认停止后的回调；继续复用受理与去重规则。
