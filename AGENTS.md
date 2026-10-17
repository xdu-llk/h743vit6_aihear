# AI Hear 开发入口

项目概况、作者与 AI 辅助开发说明见 [README.md](README.md)。

工程资料集中维护在以下文档中：

- [开发与构建说明](docs/development.md)：源码入口、内存布局、依赖现状和构建步骤。
- [硬件引脚表](docs/hardware_pin_plan.md)：接线、FPC 和供电约束。
- [MQTT 协议](docs/mqtt_protocol.md)：消息、控制与照片分片格式。
- [系统设计与历史验证](docs/final_system_overview.md)：功能链路和既有验证记录。

修改采集、DMA、推理或缓冲区前，应核对开发文档中的内存与 Cache 约束。硬件和构建验证结论需附带实际执行日期与条件。
