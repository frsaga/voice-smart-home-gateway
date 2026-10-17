#include "E53_IA1.h"

#include "i2c.h"
#include "tim.h"

#define BH1750_ADD     (0x23 << 1)
#define BH1750_ON      0x01
#define BH1750_CON     0x10

#define SHT30_ADD      (0x44 << 1)
#define SHT30_POLYNOMIAL 0x31

#define SENSOR_TIMEOUT 0x10
#define E53_PWM_PERIOD_COUNTS 1000U

static uint32_t DutyToCompare(uint8_t duty_percent)
{
  if (duty_percent > 100U)
  {
    duty_percent = 100U;
  }
  return ((uint32_t)duty_percent * E53_PWM_PERIOD_COUNTS) / 100U;
}

static void E53_IA1_LED_PinAsGpio(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitStruct.Pin = LED_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_SW_GPIO_Port, &GPIO_InitStruct);
}

void E53_IA1_LED_Set(E53_IA1_SW_status status)
{
  E53_IA1_LED_SetDuty((status == ON) ? 100U : 0U);
}

void E53_IA1_LED_SetDuty(uint8_t duty_percent)
{
  (void)HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
  E53_IA1_LED_PinAsGpio();
  HAL_GPIO_WritePin(LED_SW_GPIO_Port, LED_SW_Pin,
                    (duty_percent == 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void E53_IA1_Motor_Set(E53_IA1_SW_status status)
{
  E53_IA1_Motor_SetDuty((status == ON) ? 100U : 0U);
}

void E53_IA1_Motor_SetDuty(uint8_t duty_percent)
{
  __HAL_TIM_SET_COMPARE(&htim16, TIM_CHANNEL_1, DutyToCompare(duty_percent));
  if (duty_percent == 0U)
  {
    HAL_TIM_PWM_Stop(&htim16, TIM_CHANNEL_1);
  }
  else
  {
    HAL_TIM_PWM_Start(&htim16, TIM_CHANNEL_1);
  }
}

static void E53_IA1_BH1750_Init(void)
{
  uint8_t data = BH1750_ON;
  (void)HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADD, &data, 1, SENSOR_TIMEOUT);
}

static void E53_IA1_BH1750_Start(void)
{
  uint8_t data = BH1750_CON;
  (void)HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADD, &data, 1, SENSOR_TIMEOUT);
}

static void E53_IA1_BH1750_Read_Data(E53_IA1_Data *ReadData)
{
  uint8_t buf[2] = {0};
  uint16_t result;

  if (ReadData == NULL)
  {
    return;
  }

  E53_IA1_BH1750_Start();
  HAL_Delay(180);

  if (HAL_I2C_Master_Receive(&hi2c1, BH1750_ADD + 1, buf, 2, SENSOR_TIMEOUT) == HAL_OK)
  {
    result = ((uint16_t)buf[0] << 8) | buf[1];
    ReadData->Lux = (float)result / 1.2f;
  }
}

static void E53_IA1_SHT30_Init(void)
{
  uint8_t mode_command[2] = {0x22, 0x36};
  (void)HAL_I2C_Master_Transmit(&hi2c1, SHT30_ADD, mode_command, 2, SENSOR_TIMEOUT);
}

static void E53_IA1_SHT30_Reset(void)
{
  uint8_t reset_command[2] = {0x30, 0xA2};
  (void)HAL_I2C_Master_Transmit(&hi2c1, SHT30_ADD, reset_command, 2, SENSOR_TIMEOUT);
}

static uint8_t SHT30_CheckCrc(uint8_t data[], uint8_t nbrOfBytes, uint8_t checksum)
{
  uint8_t crc = 0xFF;
  uint8_t byteCtr;
  uint8_t bit;

  for (byteCtr = 0; byteCtr < nbrOfBytes; ++byteCtr)
  {
    crc ^= data[byteCtr];
    for (bit = 8; bit > 0; --bit)
    {
      if ((crc & 0x80) != 0)
      {
        crc = (uint8_t)((crc << 1) ^ SHT30_POLYNOMIAL);
      }
      else
      {
        crc = (uint8_t)(crc << 1);
      }
    }
  }

  return (crc != checksum) ? 1U : 0U;
}

static void SHT30_CalcTemp(uint16_t data, E53_IA1_Data *ReadData)
{
  ReadData->temp = (175.0f * (float)data / 65535.0f) - 45.0f;
}

static void SHT30_CalcHumi(uint16_t data, E53_IA1_Data *ReadData)
{
  ReadData->humi = 100.0f * (float)data / 65535.0f;
}

static void E53_IA1_SHT30_Read_Data(E53_IA1_Data *ReadData)
{
  uint8_t tmp;
  uint8_t data[3];
  uint16_t raw_data;
  uint8_t fetch_command[2] = {0xE0, 0x00};
  uint8_t read_buf[6] = {0};

  if (ReadData == NULL)
  {
    return;
  }

  if (HAL_I2C_Master_Transmit(&hi2c1, SHT30_ADD, fetch_command, 2, SENSOR_TIMEOUT) != HAL_OK)
  {
    E53_IA1_SHT30_Reset();
    return;
  }

  if (HAL_I2C_Master_Receive(&hi2c1, SHT30_ADD + 1, read_buf, 6, SENSOR_TIMEOUT) != HAL_OK)
  {
    E53_IA1_SHT30_Reset();
    return;
  }

  data[0] = read_buf[0];
  data[1] = read_buf[1];
  data[2] = read_buf[2];
  tmp = SHT30_CheckCrc(data, 2, data[2]);
  if (tmp == 0U)
  {
    raw_data = ((uint16_t)data[0] << 8) | data[1];
    SHT30_CalcTemp(raw_data, ReadData);
  }

  data[0] = read_buf[3];
  data[1] = read_buf[4];
  data[2] = read_buf[5];
  tmp = SHT30_CheckCrc(data, 2, data[2]);
  if (tmp == 0U)
  {
    raw_data = ((uint16_t)data[0] << 8) | data[1];
    SHT30_CalcHumi(raw_data, ReadData);
  }
}

void E53_IA1_Init(void)
{
  E53_IA1_LED_Set(OFF);
  E53_IA1_Motor_Set(OFF);
  E53_IA1_BH1750_Init();
  E53_IA1_SHT30_Init();
}

void E53_IA1_Read_Data(E53_IA1_Data *ReadData)
{
  E53_IA1_BH1750_Read_Data(ReadData);
  E53_IA1_SHT30_Read_Data(ReadData);
}
