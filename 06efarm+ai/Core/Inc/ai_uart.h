#ifndef __AI_UART_H__
#define __AI_UART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "E53_IA1.h"
#include <stdint.h>

#define AI_MAX_ACTIONS 4U

typedef struct {
  uint8_t valid;
  uint8_t device; /* 1 fan, 2 lamp, 3 curtain, 4 pump */
  uint8_t on;
  uint8_t fan_mode_natural;
  uint8_t level;
  uint8_t duty;
  uint8_t percent_valid;
  uint8_t percent;
  uint32_t duration_ms;
} AiAction;

typedef struct {
  uint8_t valid;
  uint8_t action_count;
  uint8_t fan_valid;
  uint8_t fan_on;
  uint8_t fan_mode_natural;
  uint8_t fan_level;
  uint8_t fan_duty;
  uint8_t lamp_valid;
  uint8_t lamp_on;
  uint8_t lamp_level;
  uint8_t lamp_duty;
  uint8_t curtain_valid;
  uint8_t curtain_open;
  uint8_t curtain_percent_valid;
  uint8_t curtain_percent;
  uint8_t temp_target_valid;
  uint8_t humi_target_valid;
  uint8_t light_min_valid;
  float temp_target;
  float humi_target;
  float light_min;
  uint32_t duration_ms;
  uint32_t received_tick;
  char cmd[16];
  char title[24];
  char line1[64];
  char line2[64];
  char advice[160];
  char reason[64];
  char level[16];
  AiAction actions[AI_MAX_ACTIONS];
} AiPlan;

typedef struct {
  char state[16];
  char text[64];
} VoiceStatus;

typedef struct {
  char time[32];
  char weather[48];
} HeaderInfo;

void AiUart_Init(void);
void AiUart_Poll(void);
void AiUart_SendEnv(const E53_IA1_Data *data, uint8_t motor_on, uint8_t led_on);
void AiUart_SendVoiceCmd(uint8_t start);
uint8_t AiUart_HasNewPlan(void);
const AiPlan *AiUart_GetPlan(void);
uint8_t AiUart_IsPlanActive(void);
uint8_t AiUart_HasVoiceAck(void);
const VoiceStatus *AiUart_GetVoiceStatus(void);
uint8_t AiUart_HasHeaderInfo(void);
const HeaderInfo *AiUart_GetHeaderInfo(void);

#ifdef __cplusplus
}
#endif

#endif /* __AI_UART_H__ */
