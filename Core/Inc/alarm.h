#ifndef __ALARM_H__
#define __ALARM_H__

#include "main.h"

typedef enum {
  ALARM_STATE_IDLE,      /* 待机：黄 (off) */
  ALARM_STATE_ARMED,     /* 布防：绿 1Hz 慢闪 */
  ALARM_STATE_DETECTING, /* 检测中：青 4Hz 快闪 */
  ALARM_STATE_ALERT,     /* 婴儿哭报警：红 急闪 + 急促蜂鸣 */
  ALARM_STATE_RECOVERY,  /* 恢复：绿 缓闪 */
  ALARM_STATE_ENV_ALERT  /* 环境报警：红 急闪 (无蜂鸣) */
} AlarmState;

void       Alarm_Init(void);
void       Alarm_SetState(AlarmState state);
AlarmState Alarm_GetState(void);
void       Alarm_Process(void);  /* 非阻塞，主循环中调用 */

#endif
