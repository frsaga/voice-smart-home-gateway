/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration of GPIO pins.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "gpio.h"

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_SYSCFG_CLK_ENABLE();

  HAL_GPIO_WritePin(LED_SW_GPIO_Port, LED_SW_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CURTAIN_SERVO_GPIO_Port, CURTAIN_SERVO_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_PWR_GPIO_Port, LCD_PWR_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, LCD_WR_RS_Pin | LCD_RST_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = LED_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_SW_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = CURTAIN_SERVO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CURTAIN_SERVO_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LCD_PWR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_PWR_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = KEY_F1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(KEY_F1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = DATA_IR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DATA_IR_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LCD_WR_RS_Pin | LCD_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}
