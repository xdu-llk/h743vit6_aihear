#ifndef __WIFI_IOT_H__
#define __WIFI_IOT_H__

#include "main.h"
#include <stdint.h>

/* ESP32 connection states */
#define WIFI_STATE_OFF         0
#define WIFI_STATE_CONNECTING  1
#define WIFI_STATE_CONNECTED   2

#define MQTT_STATE_OFF         0
#define MQTT_STATE_CONNECTING  1
#define MQTT_STATE_CONNECTED   2

void     WifiIoT_Init(void);
void     WifiIoT_Process(void);
void     WifiIoT_Publish(const char *topic, const char *payload);
void     WifiIoT_QueryStatus(void);
uint8_t  WifiIoT_GetWifiState(void);
uint8_t  WifiIoT_GetMqttState(void);
uint8_t  WifiIoT_IsReady(void);

#endif
