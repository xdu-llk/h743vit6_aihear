# AI Hear MQTT Protocol v1

The competition build uses tenant `demo` and lowercase device IDs such as
`aihear_03cb03`.

| Direction | Topic | Payload |
|---|---|---|
| Device to App | `aihear/v1/demo/{deviceId}/alert` | JSON alert event |
| Device to App | `aihear/v1/demo/{deviceId}/status` | JSON device status |
| Device to App | `aihear/v1/demo/{deviceId}/state` | STM32 control state and command acknowledgement |
| Device to App | `aihear/env` | DHT11 environment sample (competition compatibility topic) |
| App to Device | `aihear/cmd` | Device-targeted or broadcast control command |

Alert payload:

```json
{"eventId":"aihear_03cb03-1a2b3c4d-42","class":"baby_cry","score":0.95,"uptimeMs":123456}
```

Status payload:

```json
{"deviceId":"aihear_03cb03","online":true,"uptimeMs":123456,"seq":42,"fw":"bridge-v4"}
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

Omit `device` for a broadcast command. Supported commands are `arm`, `disarm`,
`play_music`, and `stop_music`. The App updates its controls only after receiving
the corresponding device state.

During migration, older ESP8266 firmware publishes both `aihear/alert` and
`aihear/{macSuffix}/alert`. The new App subscribes to the device-specific legacy
filter `aihear/+/alert`, converts a suffix such as `03CB03` to
`aihear_03cb03`, and deliberately ignores the global alert to prevent duplicate
or unattributable events. New ESP firmware publishes v1 plus the global topic
for old App compatibility. MQTT remains plaintext in the competition build;
TLS and per-device ACLs remain tracked under P0-11.
