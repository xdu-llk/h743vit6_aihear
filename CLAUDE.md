# AI Hear 项目说明入口

> 本文件不重复维护技术参数。开始工作前请完整阅读仓库根目录的 [AGENTS.md](AGENTS.md)，并以其中的当前进展、架构、内存布局和构建命令为准。

## 当前状态

- STM32H743最终固件已编译、烧录，并通过音频、DHT11、MQ-135、GP2Y1010和告警通信链路基础实板验证。
- 原厂FPC ST7789与OV2640的软件流程已经合入，硬件到货后仍需完成方向、颜色、抓拍和真实照片上传验证。
- ESP bridge-v5已烧录到`aihear_03cb03`；`aihear_876f07`已通过v4多设备隔离验证，仍需升级v5。
- Android APK构建、v2/v3签名、多设备控制和WebView照片桥接回归通过。
- 国赛版本保持`baby_cry / other`二分类，历史验证集`99.67%`不作为独立现场实测准确率。

## 权威文档

- [README.md](README.md)：项目简介、当前验证状态和快速开始
- [AGENTS.md](AGENTS.md)：开发协作说明与当前技术事实
- [docs/final_system_overview.md](docs/final_system_overview.md)：最终产品功能、演示主线与验证边界
- [docs/hardware_pin_plan.md](docs/hardware_pin_plan.md)：唯一最终BOM、引脚和载板约束
- [docs/mqtt_protocol.md](docs/mqtt_protocol.md)：ESP8266、Android与H7的通信协议

早期路线记录保存在`suggestion.md`，仅用于复盘，不得据此接线或修改当前软件。
