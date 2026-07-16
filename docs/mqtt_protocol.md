# AI Hear MQTT Protocol v1

The competition build uses tenant `demo` and lowercase device IDs such as
`aihear_03cb03`.

| Direction | Topic | Payload |
|---|---|---|
| Device to App | `aihear/v1/demo/{deviceId}/alert` | JSON alert event |
| Device to App | `aihear/v1/demo/{deviceId}/status` | JSON device status |

Alert payload:

```json
{"eventId":"aihear_03cb03-1a2b3c4d-42","class":"baby_cry","score":0.95,"uptimeMs":123456}
```

Status payload:

```json
{"deviceId":"aihear_03cb03","online":true,"uptimeMs":123456,"seq":42,"fw":"bridge-v4"}
```

During migration, older ESP8266 firmware publishes both `aihear/alert` and
`aihear/{macSuffix}/alert`. The new App subscribes to the device-specific legacy
filter `aihear/+/alert`, converts a suffix such as `03CB03` to
`aihear_03cb03`, and deliberately ignores the global alert to prevent duplicate
or unattributable events. New ESP firmware publishes v1 plus the global topic
for old App compatibility. MQTT remains plaintext in the competition build;
TLS and per-device ACLs remain tracked under P0-11.
