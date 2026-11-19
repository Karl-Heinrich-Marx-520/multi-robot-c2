# 无人机 agent

负责人姓名待补。接收统一 OBSERVE/CANCEL，转换为飞控或厂商 SDK 调用，返回 ACK、RESULT、事件和心跳，并提供本地失控保护。当前没有实现，不引入飞控 SDK 依赖。

待确认飞控型号、UDP 上层协议、端口、区域定位和任务完成条件。MAVLink over UDP 只是候选，不能根据模拟器确定实机协议。
