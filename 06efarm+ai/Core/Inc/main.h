/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l4xx_hal.h"

void Error_Handler(void);

#define LED_SW_Pin GPIO_PIN_0
#define LED_SW_GPIO_Port GPIOA
#define CURTAIN_SERVO_Pin GPIO_PIN_11
#define CURTAIN_SERVO_GPIO_Port GPIOA
#define KEY_F1_Pin GPIO_PIN_2
#define KEY_F1_GPIO_Port GPIOB
#define KEY_F1_EXTI_IRQn EXTI2_IRQn
#define LCD_PWR_Pin GPIO_PIN_15
#define LCD_PWR_GPIO_Port GPIOB
#define DATA_IR_Pin GPIO_PIN_9
#define DATA_IR_GPIO_Port GPIOB
#define DATA_IR_EXTI_IRQn EXTI9_5_IRQn
#define LCD_WR_RS_Pin GPIO_PIN_6
#define LCD_WR_RS_GPIO_Port GPIOC
#define LCD_RST_Pin GPIO_PIN_7
#define LCD_RST_GPIO_Port GPIOC

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
