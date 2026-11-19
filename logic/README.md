# 调度层：子延

此目录实现派单策略与任务状态机，当前只有 CMake 接口目标 `c2_logic`，没有派单或状态机实现。后续源文件加入此目标，调度通过 `io/api/ilink.hpp` 使用通信，通过 `proto/messages.hpp` 使用公共消息。

首先基于 MockLink 开发 A_BLOCKED → OBSERVE/GUIDE 两项任务，再处理 ACK、RESULT、送达未知、取消与会话恢复事实。设备连接、消息重传和去重由通信层负责；业务重试或放弃由调度决定。
