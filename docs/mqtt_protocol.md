# AI Hear MQTT Protocol v1

The competition build uses tenant `demo` and lowercase device IDs derived from
each ESP8266 MAC suffix, such as `aihear_03cb03` and `aihear_876f07`.

| Direction | Topic | Payload |
|---|---|---|
| Device to App | `aihear/v1/demo/{deviceId}/alert` | JSON alert event |
| Device to App | `aihear/v1/demo/{deviceId}/status` | JSON device status |
| Device to App | `aihear/v1/demo/{deviceId}/state` | STM32 control state and command acknowledgement |
| Device to App | `aihear/v1/demo/{deviceId}/env` | Environment sample |
| Device to App | `aihear/v1/demo/{deviceId}/snapshot` | JPEG chunk or capture failure |
| App to Device | `aihear/cmd` | Device-targeted control command |

Alert payload:

```json
{"eventId":"aihear_03cb03-1a2b3c4d-42","class":"baby_cry","score":0.95,"uptimeMs":123456}
```

Status payload:

```json
{"deviceId":"aihear_03cb03","online":true,"uptimeMs":123456,"seq":42,"fw":"bridge-v5"}
```

Environment payload:

```json
{"deviceId":"aihear_03cb03","temp":25.0,"humi":55.0,"uptimeMs":123456}
```

Control state payload:

```json
{"deviceId":"aihear_03cb03","armed":true,"music":false,"uptimeMs":123456}
```

Targeted command payload:

```json
{"cmd":"disarm","device":"aihear_03cb03"}
```

The `device` field is mandatory and must match the target ESP ID exactly;
commands without it are rejected. Supported commands are `arm`, `disarm`,
`play_music`, `stop_music`, and `capture`. “All devices” control is implemented by the App
publishing one targeted command per bound device. The App updates its controls
only after receiving the corresponding device state.

Snapshot chunks use a compact text payload:

```text
captureId|seq|total|crc32|base64
```

`seq` is zero-based. Each chunk contains at most 240 decoded bytes, `total` is at
most 256, and the complete JPEG is at most 60KB. Android accepts a photo only
after all chunks arrive, CRC32 matches, and JPEG SOI/EOI markers are valid. A
capture failure uses `!|captureId|reason`, for example
`!|h12ab34cd|unavailable`. Snapshot assembly is isolated by device ID.

During migration, older ESP8266 firmware publishes both `aihear/alert` and
`aihear/{macSuffix}/alert`. The new App subscribes to the device-specific legacy
filter `aihear/+/alert`, converts a suffix such as `03CB03` to
`aihear_03cb03`, and deliberately ignores the global alert to prevent duplicate
or unattributable events. New ESP firmware publishes v1 plus the global topic
for old App compatibility. MQTT remains plaintext in the competition build;
TLS and per-device ACLs remain tracked under P0-11.

The ESP8266 bridge uses PubSubClient MQTT QoS 0. It retries once when the local
publish call fails, but there is no broker PUBACK; documentation and demos must
not describe this path as QoS 1.
