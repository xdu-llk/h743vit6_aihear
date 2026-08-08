# AI Hear 项目说明入口

> 本文件不重复维护技术参数。开始工作前请完整阅读仓库根目录的 [AGENTS.md](AGENTS.md)，并以其中的当前进展、架构、内存布局和构建命令为准。

## 当前状态

- 2026-08-08 STM32H743阶段2.4抓拍看门狗修正版已烧录；焊接整机连续5次抓拍与Broker端JPEG/CRC校验通过，无异常复位。
- 原厂FPC ST7789与OV2640已完成状态显示、抓拍预览和真实照片上传验证。
- 两台ESP均已烧录bridge-v5并联网；设备ID隔离、UART/MQTT、断线恢复和照片分片链路均已验证。
- Android APK构建、v2/v3签名、多设备控制、通知和真实照片显示通过。
- 国赛版本保持`baby_cry / other`二分类，历史验证集`99.67%`不作为独立现场实测准确率。

## 权威文档

- [README.md](README.md)：项目简介、当前验证状态和快速开始
- [AGENTS.md](AGENTS.md)：开发协作说明与当前技术事实
- [docs/final_system_overview.md](docs/final_system_overview.md)：最终产品功能、演示主线与验证边界
- [docs/hardware_pin_plan.md](docs/hardware_pin_plan.md)：唯一最终BOM、引脚和载板约束
- [docs/mqtt_protocol.md](docs/mqtt_protocol.md)：ESP8266、Android与H7的通信协议

早期路线记录保存在`suggestion.md`，仅用于复盘，不得据此接线或修改当前软件。
