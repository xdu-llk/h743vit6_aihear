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
void     WifiIoT_Publish(const char *topic, const char *payload);   /* link down → enqueue */
void     WifiIoT_FlushPending(void);                                 /* call every loop */
void     WifiIoT_ResetEsp(void);                                      /* PA2 low 10ms → hard reset */
void     WifiIoT_QueryStatus(void);
uint8_t  WifiIoT_GetWifiState(void);
uint8_t  WifiIoT_GetMqttState(void);
uint8_t  WifiIoT_IsReady(void);
uint8_t  WifiIoT_StatusHealthy(void);
uint32_t WifiIoT_GetLastPubackMs(void);
uint32_t WifiIoT_GetLastPubackSeq(void);
uint16_t WifiIoT_GetRxOverflow(void);
uint8_t  WifiIoT_GetPendingCount(void);
const char* WifiIoT_GetDeviceId(void);

/* Remote commands from App via MQTT → ESP → UART */
bool        WifiIoT_HasCommand(void);
const char* WifiIoT_PopCommand(void);  /* returns NULL if no command pending */

#endif
