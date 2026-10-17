# AI Hear MQTT 协议 v1

竞赛版本使用租户 `demo`。设备 ID 由 ESP8266 MAC 后缀生成，使用小写，例如 `aihear_03cb03`。以下 ID 为消息格式示例，实际接收和控制应使用对应终端的 ID。

当前 Broker 为 `broker-cn.emqx.io:1883`，使用明文 MQTT、QoS 0。

## Topic

| 方向 | Topic | 载荷 |
|---|---|---|
| 设备 → App | `aihear/v1/demo/{deviceId}/alert` | JSON 告警事件 |
| 设备 → App | `aihear/v1/demo/{deviceId}/status` | ESP 状态 JSON 或 H7 心跳设备 ID |
| 设备 → App | `aihear/v1/demo/{deviceId}/state` | STM32 控制状态与命令回执 |
| 设备 → App | `aihear/v1/demo/{deviceId}/env` | 环境样本 |
| 设备 → App | `aihear/v1/demo/{deviceId}/snapshot` | JPEG 分片或抓拍失败状态 |
| App → 设备 | `aihear/cmd` | 指定设备的控制命令 |

## 消息示例

### 告警

```json
{"eventId":"aihear_03cb03-1a2b3c4d-42","class":"baby_cry","score":0.95,"uptimeMs":123456}
```

`eventId` 用于区分事件，`class` 表示告警类型，`score` 为哭声置信度，`uptimeMs` 为设备运行时间。

### 在线状态

```json
{"deviceId":"aihear_03cb03","online":true,"uptimeMs":123456,"seq":42,"fw":"bridge-v5"}
```

### 环境数据

```json
{"deviceId":"aihear_03cb03","temp":25.0,"humi":55.0,"uptimeMs":123456}
```

### 控制状态

```json
{"deviceId":"aihear_03cb03","armed":true,"music":false,"uptimeMs":123456}
```

### 定向命令

```json
{"cmd":"disarm","device":"aihear_03cb03"}
```

`device` 为必填项，必须与目标 ESP ID 完全一致；缺失时拒绝处理。支持 `arm`、`disarm`、`play_music`、`stop_music` 和 `capture`。

App 的“全部设备”操作通过逐台发送定向命令实现。控制界面在收到对应设备状态后更新。

## 照片分片

照片使用紧凑文本载荷：

```text
captureId|seq|total|crc32|base64
```

- `seq` 从 0 开始，`total` 最多为 256。
- 每片最多包含 240 字节的原始 JPEG 数据，再进行 Base64 编码。
- 完整 JPEG 不超过 60 KiB，重组状态按设备 ID 隔离。
- Android 只有在分片齐全、CRC32 一致、JPEG SOI / EOI 标记有效后才显示照片。

抓拍失败使用 `!|captureId|reason`，例如：

```text
!|h12ab34cd|unavailable
```

## 兼容与传输边界

旧版 ESP 可能发布 `aihear/alert` 或 `aihear/{macSuffix}/alert`。App 只兼容可归属到设备的 `aihear/+/alert`，忽略全局告警 topic；bridge-v5 只发布设备级 v1 topic。

PubSubClient 的 QoS 0 发布在本地失败时重试一次，没有 Broker PUBACK。本地返回成功不能表述为 Broker 确认收到，也不能将该链路称为 QoS 1。

现有协议用于竞赛原型。明文传输和公开 Broker 不具备正式部署所需的传输保密与设备访问控制；更换 Broker 或扩展认证需要同时核对 ESP、Android 与调试工具。
