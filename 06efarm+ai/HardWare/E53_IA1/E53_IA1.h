#ifndef E53_IA1_H
#define E53_IA1_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum {
  OFF = 0,
  ON
} E53_IA1_SW_status;

typedef struct {
  float Lux;
  float temp;
  float humi;
} E53_IA1_Data;

void E53_IA1_LED_Set(E53_IA1_SW_status status);
void E53_IA1_LED_SetDuty(uint8_t duty_percent);
void E53_IA1_Motor_Set(E53_IA1_SW_status status);
void E53_IA1_Motor_SetDuty(uint8_t duty_percent);
void E53_IA1_Init(void);
void E53_IA1_Read_Data(E53_IA1_Data *ReadData);

#ifdef __cplusplus
}
#endif

#endif /* E53_IA1_H */
