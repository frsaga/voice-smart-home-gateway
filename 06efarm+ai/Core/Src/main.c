/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Smart farm demo
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "E53_IA1.h"
#include "ai_uart.h"
#include "lcd_spi2_drv.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define HEADER_TIME_Y   0U
#define HEADER_WEATHER_Y 16U
#define HEADER_H        32U
#define AI_INFO_Y       32U
#define AI_INFO_H       112U
#define STATUS_Y        144U
#define STATUS_H        96U
#define VOICE_Y         144U
#define LCD_TEXT_X      5U
#define LCD_TEXT_W      230U
#define DEVICE_UI_Y     160U
#define DEVICE_UI_H     32U
#define DEVICE_UI_W     80U
#define SENSOR_UI_Y     192U
#define SENSOR_UI_H     48U
#define SENSOR_UI_W     80U
#define FAN_LEVEL1_DUTY 40U
#define FAN_LEVEL2_DUTY 70U
#define FAN_LEVEL3_DUTY 100U
#define LAMP_LEVEL1_DUTY 30U
#define LAMP_LEVEL2_DUTY 60U
#define LAMP_LEVEL3_DUTY 100U
#define ENV_UPDATE_MS   2000UL
#define AI_IDLE_ANIM_MS 2000UL
#define CURTAIN_CLOSE_PULSE_US 1300U
#define CURTAIN_STOP_PULSE_US  1500U
#define CURTAIN_OPEN_PULSE_US  1700U
#define CURTAIN_SERVO_FRAMES   75U
#define CURTAIN_STOP_FRAMES    8U
#define CURTAIN_SERVO_PERIOD_US 20000U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static char display_lux[40];
static char display_humi[40];
static char display_temp[40];
static E53_IA1_Data sensor_data;
static uint8_t led_state;
static uint8_t motor_state;
static uint8_t fan_level;
static uint8_t fan_duty;
static uint8_t fan_mode_natural;
static uint8_t fan_output_duty;
static uint8_t lamp_level;
static uint8_t lamp_duty;
static uint8_t curtain_state_valid;
static uint8_t curtain_is_open;
static uint8_t curtain_percent_valid;
static uint8_t curtain_percent;
static uint32_t last_env_send_tick;
static uint32_t last_ai_anim_tick;
static uint8_t ai_anim_state;
static volatile uint8_t f1_pressed;
static volatile uint32_t f1_irq_tick;
static uint8_t voice_recording_confirmed;
static uint8_t fan_manual_control;
static uint8_t lamp_manual_control;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void SmartFarm_Display(const E53_IA1_Data *data);
static void SmartFarm_Control(const E53_IA1_Data *data);
static void SmartFarm_DisplayAiPlan(void);
static void SmartFarm_DisplayAiIdle(void);
static void SmartFarm_HandleVoiceButton(void);
static void SmartFarm_DisplayVoiceStatus(const VoiceStatus *status);
static void SmartFarm_DisplayHeaderInfo(const HeaderInfo *info);
static uint8_t SmartFarm_IsVoiceBusy(void);
static void SmartHome_DrawDeviceUi(void);
static void SmartFarm_DrawSensorUi(const E53_IA1_Data *data);
static void LCD_DrawFastHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
static void LCD_DrawFastVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
static void LCD_DrawBox(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
static void LCD_ShowCenteredText(uint16_t x, uint16_t y, uint16_t w, const char *str,
                                 uint16_t color, uint16_t back_color);
static void LCD_DrawDeviceTile(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               const char *label, const char *detail,
                               uint8_t active, uint8_t valid);
static void LCD_DrawThermometerIcon(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                    const char *value);
static void LCD_DrawDropIcon(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             const char *value);
static void LCD_DrawSunIcon(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            const char *value);
static void SmartHome_CurtainMove(uint8_t open, uint8_t percent, uint8_t percent_valid);
static void SmartHome_UpdateFanPwm(void);
static uint8_t SmartHome_DutyToLevel(uint8_t duty, uint8_t is_lamp);
static uint8_t SmartHome_LevelToDuty(uint8_t level, uint8_t is_lamp);
static void Servo_DelayUs(uint32_t us);
static void Servo_SendPulse(uint16_t high_us);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  const AiPlan *new_plan;

  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_SPI2_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_TIM16_Init();
  MX_USART2_UART_Init();

  LCD_Init();
  LCD_Clear(BLACK);

  E53_IA1_Init();
  AiUart_Init();
  SmartFarm_DisplayHeaderInfo(AiUart_GetHeaderInfo());
  SmartFarm_DisplayAiIdle();
  E53_IA1_Read_Data(&sensor_data);
  SmartFarm_Control(&sensor_data);
  SmartFarm_Display(&sensor_data);
  SmartFarm_DisplayVoiceStatus(AiUart_GetVoiceStatus());

  while (1)
  {
    AiUart_Poll();
    SmartFarm_HandleVoiceButton();
    SmartHome_UpdateFanPwm();

    if ((SmartFarm_IsVoiceBusy() == 0U) &&
        ((HAL_GetTick() - last_env_send_tick) >= ENV_UPDATE_MS))
    {
      E53_IA1_Read_Data(&sensor_data);
      SmartFarm_Control(&sensor_data);
      SmartFarm_Display(&sensor_data);
      AiUart_SendEnv(&sensor_data, motor_state, led_state);
      last_env_send_tick = HAL_GetTick();
    }

    if (AiUart_HasNewPlan() != 0U)
    {
      new_plan = AiUart_GetPlan();
      if (new_plan->curtain_valid != 0U)
      {
        SmartHome_CurtainMove(new_plan->curtain_open,
                              new_plan->curtain_percent,
                              new_plan->curtain_percent_valid);
      }
      SmartFarm_Control(&sensor_data);
      SmartFarm_Display(&sensor_data);
      SmartFarm_DisplayAiPlan();
    }
    if (AiUart_HasVoiceAck() != 0U)
    {
      SmartFarm_DisplayVoiceStatus(AiUart_GetVoiceStatus());
    }
    else if ((AiUart_IsPlanActive() == 0U) && (SmartFarm_IsVoiceBusy() == 0U))
    {
      SmartFarm_DisplayAiIdle();
    }
    if (AiUart_HasHeaderInfo() != 0U)
    {
      SmartFarm_DisplayHeaderInfo(AiUart_GetHeaderInfo());
    }

    HAL_Delay(20);
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 20;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
static void SmartFarm_Display(const E53_IA1_Data *data)
{
  static char last_lux[40];
  static char last_humi[40];
  static char last_temp[40];
  static uint8_t first_draw = 1U;
  uint8_t sensor_changed;

  memset(display_lux, 0x00, sizeof(display_lux));
  snprintf(display_lux, sizeof(display_lux), "%.0flx", data->Lux);

  memset(display_humi, 0x00, sizeof(display_humi));
  snprintf(display_humi, sizeof(display_humi), "%.0f%%", data->humi);

  memset(display_temp, 0x00, sizeof(display_temp));
  snprintf(display_temp, sizeof(display_temp), "%.1fC", data->temp);

  SmartHome_DrawDeviceUi();

  sensor_changed = (uint8_t)((first_draw != 0U) ||
                             (strcmp(last_lux, display_lux) != 0) ||
                             (strcmp(last_humi, display_humi) != 0) ||
                             (strcmp(last_temp, display_temp) != 0));
  if (sensor_changed != 0U)
  {
    SmartFarm_DrawSensorUi(data);
    (void)strncpy(last_lux, display_lux, sizeof(last_lux) - 1U);
    (void)strncpy(last_humi, display_humi, sizeof(last_humi) - 1U);
    (void)strncpy(last_temp, display_temp, sizeof(last_temp) - 1U);
  }
  first_draw = 0U;
}

static void SmartFarm_HandleVoiceButton(void)
{
  uint8_t send_start;

  if (f1_pressed == 0U)
  {
    return;
  }

  __disable_irq();
  f1_pressed = 0U;
  __enable_irq();

  send_start = (voice_recording_confirmed == 0U) ? 1U : 0U;
  AiUart_SendVoiceCmd(send_start);
}

static void SmartFarm_DisplayVoiceStatus(const VoiceStatus *status)
{
  char line[32];
  uint16_t color = WHITE;

  if (status == NULL)
  {
    return;
  }

  if (strcmp(status->state, "recording") == 0)
  {
    voice_recording_confirmed = 1U;
    color = RED;
    snprintf(line, sizeof(line), "VOICE: REC");
  }
  else if (strcmp(status->state, "processing") == 0)
  {
    color = YELLOW;
    if ((strstr(status->text, "busy") != NULL) || (strstr(status->text, "Busy") != NULL))
    {
      snprintf(line, sizeof(line), "VOICE: BUSY");
    }
    else
    {
      snprintf(line, sizeof(line), "VOICE: ASR");
    }
  }
  else if (strcmp(status->state, "error") == 0)
  {
    voice_recording_confirmed = 0U;
    color = RED;
    snprintf(line, sizeof(line), "VOICE: ERR");
  }
  else if (strcmp(status->state, "idle") == 0)
  {
    voice_recording_confirmed = 0U;
    color = GREEN;
    if (strstr(status->text, "ASR OK") != NULL)
    {
      snprintf(line, sizeof(line), "VOICE: OK");
    }
    else
    {
      snprintf(line, sizeof(line), "VOICE: IDLE");
    }
  }
  else
  {
    voice_recording_confirmed = 0U;
    color = WHITE;
    snprintf(line, sizeof(line), "VOICE: %.20s", status->state);
  }
  LCD_Fill(0, VOICE_Y, 239, VOICE_Y + 15U, BLACK);
  LCD_ShowMixedString(LCD_TEXT_X, VOICE_Y, LCD_TEXT_W, 16, line, color, BLACK);
}

static void SmartFarm_DisplayHeaderInfo(const HeaderInfo *info)
{
  if (info == NULL)
  {
    return;
  }

  LCD_Fill(0, HEADER_TIME_Y, 239, HEADER_TIME_Y + HEADER_H - 1U, BLACK);
  LCD_ShowMixedString(LCD_TEXT_X, HEADER_TIME_Y, LCD_TEXT_W, 16,
                      info->time, GREEN, BLACK);
  LCD_ShowMixedString(LCD_TEXT_X, HEADER_WEATHER_Y, LCD_TEXT_W, 16,
                      info->weather, BLUE, BLACK);
}

static uint8_t SmartFarm_IsVoiceBusy(void)
{
  const VoiceStatus *status = AiUart_GetVoiceStatus();

  if (status == NULL)
  {
    return 0U;
  }

  if ((strcmp(status->state, "recording") == 0) ||
      (strcmp(status->state, "processing") == 0))
  {
    return 1U;
  }
  return 0U;
}

static void LCD_DrawFastHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
  if (w == 0U)
  {
    return;
  }
  LCD_Fill(x, y, x + w - 1U, y, color);
}

static void LCD_DrawFastVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
  if (h == 0U)
  {
    return;
  }
  LCD_Fill(x, y, x, y + h - 1U, color);
}

static void LCD_DrawBox(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  LCD_DrawFastHLine(x, y, w, color);
  LCD_DrawFastHLine(x, y + h - 1U, w, color);
  LCD_DrawFastVLine(x, y, h, color);
  LCD_DrawFastVLine(x + w - 1U, y, h, color);
}

static void LCD_ShowCenteredText(uint16_t x, uint16_t y, uint16_t w, const char *str,
                                 uint16_t color, uint16_t back_color)
{
  uint16_t len;
  uint16_t text_w;
  uint16_t text_x;

  if (str == NULL)
  {
    return;
  }

  len = (uint16_t)strlen(str);
  text_w = len * 8U;
  text_x = x;
  if (text_w < w)
  {
    text_x = x + ((w - text_w) / 2U);
  }

  LCD_ShowMixedString(text_x, y, w, 16, str, color, back_color);
}

static void LCD_DrawDeviceTile(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               const char *label, const char *detail,
                               uint8_t active, uint8_t valid)
{
  uint16_t color = WHITE;

  if (valid == 0U)
  {
    color = YELLOW;
  }
  else if (active != 0U)
  {
    color = GREEN;
  }

  LCD_Fill(x, y, x + w - 1U, y + h - 1U, BLACK);
  LCD_DrawBox(x + 2U, y, w - 4U, h, color);
  LCD_ShowCenteredText(x, y + 1U, w, label, color, BLACK);
  LCD_ShowCenteredText(x, y + 16U, w, detail, WHITE, BLACK);
}

static void SmartHome_DrawDeviceUi(void)
{
  static char last_fan_label[12];
  static char last_fan_detail[12];
  static char last_lamp_label[12];
  static char last_lamp_detail[12];
  static char last_curtain_label[12];
  static char last_curtain_detail[12];
  static uint8_t last_motor_state = 2U;
  static uint8_t last_led_state = 2U;
  static uint8_t last_curtain_state_valid = 2U;
  static uint8_t first_draw = 1U;
  char fan_label[12];
  char fan_detail[12];
  char lamp_label[12];
  char lamp_detail[12];
  char curtain_label[12];
  char curtain_detail[12];

  snprintf(fan_label, sizeof(fan_label), "FAN %s", (motor_state != 0U) ? "ON" : "OFF");
  if (motor_state != 0U)
  {
    snprintf(fan_detail, sizeof(fan_detail), "%c%u %u%%",
             (fan_mode_natural != 0U) ? 'N' : 'D', fan_level, fan_duty);
  }
  else
  {
    snprintf(fan_detail, sizeof(fan_detail), "L0 0%%");
  }

  snprintf(lamp_label, sizeof(lamp_label), "LAMP %s", (led_state != 0U) ? "ON" : "OFF");
  snprintf(lamp_detail, sizeof(lamp_detail), "L%u %u%%", lamp_level, lamp_duty);

  if (curtain_state_valid != 0U)
  {
    snprintf(curtain_label, sizeof(curtain_label), "CUR %u%%",
             (curtain_percent_valid != 0U) ? curtain_percent : ((curtain_is_open != 0U) ? 100U : 0U));
    snprintf(curtain_detail, sizeof(curtain_detail), "%s",
             (curtain_is_open != 0U) ? "OPEN" : "CLOSE");
  }
  else
  {
    snprintf(curtain_label, sizeof(curtain_label), "CUR --");
    snprintf(curtain_detail, sizeof(curtain_detail), "--");
  }

  if ((first_draw != 0U) || (last_motor_state != motor_state) ||
      (strcmp(last_fan_label, fan_label) != 0) ||
      (strcmp(last_fan_detail, fan_detail) != 0))
  {
    LCD_DrawDeviceTile(0U, DEVICE_UI_Y, DEVICE_UI_W, DEVICE_UI_H,
                       fan_label, fan_detail, motor_state, 1U);
    (void)strncpy(last_fan_label, fan_label, sizeof(last_fan_label) - 1U);
    (void)strncpy(last_fan_detail, fan_detail, sizeof(last_fan_detail) - 1U);
    last_motor_state = motor_state;
  }

  if ((first_draw != 0U) || (last_led_state != led_state) ||
      (strcmp(last_lamp_label, lamp_label) != 0) ||
      (strcmp(last_lamp_detail, lamp_detail) != 0))
  {
    LCD_DrawDeviceTile(DEVICE_UI_W, DEVICE_UI_Y, DEVICE_UI_W, DEVICE_UI_H,
                       lamp_label, lamp_detail, led_state, 1U);
    (void)strncpy(last_lamp_label, lamp_label, sizeof(last_lamp_label) - 1U);
    (void)strncpy(last_lamp_detail, lamp_detail, sizeof(last_lamp_detail) - 1U);
    last_led_state = led_state;
  }

  if ((first_draw != 0U) || (last_curtain_state_valid != curtain_state_valid) ||
      (strcmp(last_curtain_label, curtain_label) != 0) ||
      (strcmp(last_curtain_detail, curtain_detail) != 0))
  {
    LCD_DrawDeviceTile(DEVICE_UI_W * 2U, DEVICE_UI_Y, DEVICE_UI_W, DEVICE_UI_H,
                       curtain_label, curtain_detail, curtain_is_open, curtain_state_valid);
    (void)strncpy(last_curtain_label, curtain_label, sizeof(last_curtain_label) - 1U);
    (void)strncpy(last_curtain_detail, curtain_detail, sizeof(last_curtain_detail) - 1U);
    last_curtain_state_valid = curtain_state_valid;
  }

  first_draw = 0U;
}

static void LCD_DrawThermometerIcon(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                    const char *value)
{
  const uint16_t cx = x + (w / 2U);
  const uint16_t tube_top = y + 7U;
  const uint16_t tube_bottom = y + h - 13U;

  LCD_Fill(x, y, x + w - 1U, y + h - 1U, BLACK);
  LCD_DrawBox(x + 2U, y + 1U, w - 4U, h - 2U, BRRED);
  LCD_Fill(cx - 5U, tube_top, cx + 5U, tube_bottom, BRRED);
  LCD_Fill(cx - 2U, tube_top + 3U, cx + 2U, tube_bottom + 2U, BLACK);
  LCD_FillCircle(cx, y + h - 13U, 10U, BRRED);
  LCD_FillCircle(cx, y + h - 13U, 6U, BLACK);
  LCD_ShowCenteredText(x, y + 16U, w, value, WHITE, BLACK);
}

static void LCD_DrawDropIcon(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             const char *value)
{
  const uint16_t cx = x + (w / 2U);
  uint16_t row;

  LCD_Fill(x, y, x + w - 1U, y + h - 1U, BLACK);
  LCD_DrawBox(x + 2U, y + 1U, w - 4U, h - 2U, GBLUE);
  LCD_FillCircle(cx, y + 29U, 16U, GBLUE);
  for (row = 0U; row < 16U; row++)
  {
    LCD_DrawFastHLine(cx - row, y + 6U + row, (uint16_t)(row * 2U + 1U), GBLUE);
  }
  LCD_FillCircle(cx, y + 29U, 10U, BLACK);
  LCD_ShowCenteredText(x, y + 16U, w, value, WHITE, BLACK);
}

static void LCD_DrawSunIcon(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            const char *value)
{
  const uint16_t cx = x + (w / 2U);
  const uint16_t cy = y + 24U;

  LCD_Fill(x, y, x + w - 1U, y + h - 1U, BLACK);
  LCD_DrawBox(x + 2U, y + 1U, w - 4U, h - 2U, YELLOW);
  LCD_FillCircle(cx, cy, 16U, YELLOW);
  LCD_DrawFastHLine(cx - 27U, cy, 10U, YELLOW);
  LCD_DrawFastHLine(cx + 18U, cy, 10U, YELLOW);
  LCD_DrawFastVLine(cx, cy - 27U, 10U, YELLOW);
  LCD_DrawFastVLine(cx, cy + 18U, 10U, YELLOW);
  LCD_DrawFastHLine(cx - 21U, cy - 18U, 8U, YELLOW);
  LCD_DrawFastHLine(cx + 14U, cy - 18U, 8U, YELLOW);
  LCD_DrawFastHLine(cx - 21U, cy + 18U, 8U, YELLOW);
  LCD_DrawFastHLine(cx + 14U, cy + 18U, 8U, YELLOW);
  LCD_FillCircle(cx, cy, 11U, BLACK);
  LCD_ShowCenteredText(x, y + 16U, w, value, WHITE, BLACK);
}

static void SmartFarm_DrawSensorUi(const E53_IA1_Data *data)
{
  (void)data;

  LCD_DrawThermometerIcon(0U, SENSOR_UI_Y, SENSOR_UI_W, SENSOR_UI_H, display_temp);
  LCD_DrawDropIcon(SENSOR_UI_W, SENSOR_UI_Y, SENSOR_UI_W, SENSOR_UI_H, display_humi);
  LCD_DrawSunIcon(SENSOR_UI_W * 2U, SENSOR_UI_Y, SENSOR_UI_W, SENSOR_UI_H, display_lux);
}

static uint8_t SmartHome_LevelToDuty(uint8_t level, uint8_t is_lamp)
{
  if (level == 0U)
  {
    return 0U;
  }
  if (level == 1U)
  {
    return (is_lamp != 0U) ? LAMP_LEVEL1_DUTY : FAN_LEVEL1_DUTY;
  }
  if (level == 2U)
  {
    return (is_lamp != 0U) ? LAMP_LEVEL2_DUTY : FAN_LEVEL2_DUTY;
  }
  return (is_lamp != 0U) ? LAMP_LEVEL3_DUTY : FAN_LEVEL3_DUTY;
}

static uint8_t SmartHome_DutyToLevel(uint8_t duty, uint8_t is_lamp)
{
  if (duty == 0U)
  {
    return 0U;
  }
  if (is_lamp != 0U)
  {
    if (duty <= LAMP_LEVEL1_DUTY)
    {
      return 1U;
    }
    if (duty <= LAMP_LEVEL2_DUTY)
    {
      return 2U;
    }
    return 3U;
  }

  if (duty <= FAN_LEVEL1_DUTY)
  {
    return 1U;
  }
  if (duty <= FAN_LEVEL2_DUTY)
  {
    return 2U;
  }
  return 3U;
}

static void SmartHome_UpdateFanPwm(void)
{
  uint8_t min_duty;
  uint8_t next_duty;
  uint32_t phase;

  if ((motor_state == 0U) || (fan_duty == 0U))
  {
    if (fan_output_duty != 0U)
    {
      fan_output_duty = 0U;
      E53_IA1_Motor_SetDuty(0U);
    }
    return;
  }

  if (fan_mode_natural == 0U)
  {
    if (fan_output_duty != fan_duty)
    {
      fan_output_duty = fan_duty;
      E53_IA1_Motor_SetDuty(fan_output_duty);
    }
    return;
  }

  min_duty = (fan_duty > 40U) ? 35U : 20U;
  if (min_duty >= fan_duty)
  {
    min_duty = fan_duty / 2U;
  }
  phase = (HAL_GetTick() / 160UL) % 20UL;
  if (phase < 10UL)
  {
    next_duty = (uint8_t)(min_duty + (((uint32_t)(fan_duty - min_duty) * phase) / 9UL));
  }
  else
  {
    next_duty = (uint8_t)(fan_duty - (((uint32_t)(fan_duty - min_duty) * (phase - 10UL)) / 9UL));
  }

  if (next_duty != fan_output_duty)
  {
    fan_output_duty = next_duty;
    E53_IA1_Motor_SetDuty(fan_output_duty);
  }
}

static void SmartFarm_Control(const E53_IA1_Data *data)
{
  uint8_t auto_lamp_duty = (data->Lux < 50.0f) ? LAMP_LEVEL2_DUTY : 0U;
  uint8_t auto_fan_duty = (data->humi > 90.0f) ? FAN_LEVEL2_DUTY : 0U;
  uint8_t next_lamp_duty = (lamp_manual_control != 0U) ? lamp_duty : auto_lamp_duty;
  uint8_t next_lamp_level = SmartHome_DutyToLevel(next_lamp_duty, 1U);
  uint8_t next_fan_duty = (fan_manual_control != 0U) ? fan_duty : auto_fan_duty;
  uint8_t next_fan_level = SmartHome_DutyToLevel(next_fan_duty, 0U);
  uint8_t next_fan_natural = (fan_manual_control != 0U) ? fan_mode_natural : 0U;
  const AiPlan *plan = AiUart_GetPlan();

  if (AiUart_IsPlanActive() != 0U)
  {
    if (plan->lamp_valid != 0U)
    {
      lamp_manual_control = 1U;
      next_lamp_level = (plan->lamp_on != 0U) ? plan->lamp_level : 0U;
      next_lamp_duty = (plan->lamp_on != 0U) ? plan->lamp_duty : 0U;
      if ((next_lamp_level == 0U) && (next_lamp_duty != 0U))
      {
        next_lamp_level = SmartHome_DutyToLevel(next_lamp_duty, 1U);
      }
      if ((next_lamp_duty == 0U) && (next_lamp_level != 0U))
      {
        next_lamp_duty = SmartHome_LevelToDuty(next_lamp_level, 1U);
      }
    }
    else if ((plan->light_min_valid != 0U) && (lamp_manual_control == 0U))
    {
      if (data->Lux < plan->light_min)
      {
        next_lamp_level = (plan->lamp_level != 0U) ? plan->lamp_level : 2U;
        next_lamp_duty = (plan->lamp_duty != 0U) ? plan->lamp_duty :
                         SmartHome_LevelToDuty(next_lamp_level, 1U);
      }
      else
      {
        next_lamp_level = 0U;
        next_lamp_duty = 0U;
      }
    }

    if (plan->fan_valid != 0U)
    {
      fan_manual_control = 1U;
      next_fan_level = (plan->fan_on != 0U) ? plan->fan_level : 0U;
      next_fan_duty = (plan->fan_on != 0U) ? plan->fan_duty : 0U;
      next_fan_natural = (plan->fan_on != 0U) ? plan->fan_mode_natural : 0U;
      if ((next_fan_level == 0U) && (next_fan_duty != 0U))
      {
        next_fan_level = SmartHome_DutyToLevel(next_fan_duty, 0U);
      }
      if ((next_fan_duty == 0U) && (next_fan_level != 0U))
      {
        next_fan_duty = SmartHome_LevelToDuty(next_fan_level, 0U);
      }
    }
    else
    {
      if ((plan->temp_target_valid != 0U) && (data->temp > plan->temp_target))
      {
        if (fan_manual_control == 0U)
        {
          next_fan_level = (plan->fan_level != 0U) ? plan->fan_level : 2U;
          next_fan_duty = (plan->fan_duty != 0U) ? plan->fan_duty :
                          SmartHome_LevelToDuty(next_fan_level, 0U);
          next_fan_natural = plan->fan_mode_natural;
        }
      }
      if ((plan->humi_target_valid != 0U) && (data->humi > plan->humi_target))
      {
        if (fan_manual_control == 0U)
        {
          next_fan_level = (plan->fan_level != 0U) ? plan->fan_level : 2U;
          next_fan_duty = (plan->fan_duty != 0U) ? plan->fan_duty :
                          SmartHome_LevelToDuty(next_fan_level, 0U);
          next_fan_natural = plan->fan_mode_natural;
        }
      }
      if ((plan->temp_target_valid != 0U) && (plan->humi_target_valid != 0U) &&
          (data->temp <= plan->temp_target) && (data->humi <= plan->humi_target) &&
          (fan_manual_control == 0U))
      {
        next_fan_level = 0U;
        next_fan_duty = 0U;
        next_fan_natural = 0U;
      }
    }
  }

  if (next_lamp_duty > 100U)
  {
    next_lamp_duty = 100U;
  }
  if (next_fan_duty > 100U)
  {
    next_fan_duty = 100U;
  }

  lamp_duty = next_lamp_duty;
  lamp_level = (lamp_duty != 0U) ? ((next_lamp_level != 0U) ? next_lamp_level :
                SmartHome_DutyToLevel(lamp_duty, 1U)) : 0U;
  led_state = (lamp_duty != 0U) ? 1U : 0U;
  E53_IA1_LED_SetDuty(lamp_duty);

  fan_duty = next_fan_duty;
  fan_level = (fan_duty != 0U) ? ((next_fan_level != 0U) ? next_fan_level :
               SmartHome_DutyToLevel(fan_duty, 0U)) : 0U;
  fan_mode_natural = (fan_duty != 0U) ? next_fan_natural : 0U;
  motor_state = (fan_duty != 0U) ? 1U : 0U;
  SmartHome_UpdateFanPwm();
}

static void SmartHome_CurtainMove(uint8_t open, uint8_t percent, uint8_t percent_valid)
{
  uint8_t i;
  uint8_t target_percent;
  const uint16_t pulse = (open != 0U) ? CURTAIN_OPEN_PULSE_US : CURTAIN_CLOSE_PULSE_US;

  if (open != 0U)
  {
    target_percent = (percent_valid != 0U) ? percent : 100U;
  }
  else
  {
    target_percent = 0U;
  }
  if (target_percent > 100U)
  {
    target_percent = 100U;
  }

  curtain_state_valid = 1U;
  curtain_is_open = (target_percent > 0U) ? 1U : 0U;
  curtain_percent_valid = 1U;
  curtain_percent = target_percent;
  for (i = 0U; i < CURTAIN_SERVO_FRAMES; i++)
  {
    Servo_SendPulse(pulse);
  }
  for (i = 0U; i < CURTAIN_STOP_FRAMES; i++)
  {
    Servo_SendPulse(CURTAIN_STOP_PULSE_US);
  }
  HAL_GPIO_WritePin(CURTAIN_SERVO_GPIO_Port, CURTAIN_SERVO_Pin, GPIO_PIN_RESET);
}

static void Servo_DelayUs(uint32_t us)
{
  uint32_t start;
  uint32_t ticks;

  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0UL)
  {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0UL;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  }

  start = DWT->CYCCNT;
  ticks = us * (HAL_RCC_GetHCLKFreq() / 1000000UL);
  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

static void Servo_SendPulse(uint16_t high_us)
{
  HAL_GPIO_WritePin(CURTAIN_SERVO_GPIO_Port, CURTAIN_SERVO_Pin, GPIO_PIN_SET);
  Servo_DelayUs(high_us);
  HAL_GPIO_WritePin(CURTAIN_SERVO_GPIO_Port, CURTAIN_SERVO_Pin, GPIO_PIN_RESET);
  Servo_DelayUs(CURTAIN_SERVO_PERIOD_US - high_us);
}

static void SmartFarm_DisplayAiPlan(void)
{
  const AiPlan *plan = AiUart_GetPlan();
  uint16_t color = GREEN;
  char header[64];
  char target[80];

  if (strcmp(plan->level, "warning") == 0)
  {
    color = YELLOW;
  }
  else if (strcmp(plan->level, "danger") == 0)
  {
    color = RED;
  }

  LCD_Fill(0, AI_INFO_Y, 239, AI_INFO_Y + AI_INFO_H - 1U, BLACK);
  snprintf(header, sizeof(header), "%s %s", plan->title, plan->level);
  snprintf(target, sizeof(target), "%s | %s", plan->line1, plan->line2);
  LCD_ShowMixedString(LCD_TEXT_X, AI_INFO_Y, LCD_TEXT_W, 16, header, color, BLACK);
  LCD_ShowMixedString(LCD_TEXT_X, AI_INFO_Y + 16U, LCD_TEXT_W, 16, target, WHITE, BLACK);
  LCD_ShowMixedString(LCD_TEXT_X, AI_INFO_Y + 32U, LCD_TEXT_W, 64, plan->advice, color, BLACK);
}

static void SmartFarm_DisplayAiIdle(void)
{
  const uint32_t now = HAL_GetTick();

  if ((last_ai_anim_tick != 0UL) && ((now - last_ai_anim_tick) < 700UL))
  {
    return;
  }

  last_ai_anim_tick = now;
  ai_anim_state = (ai_anim_state == 0U) ? 1U : 0U;

  LCD_Fill(0, AI_INFO_Y, 239, AI_INFO_Y + AI_INFO_H - 1U, BLACK);
  if (ai_anim_state == 0U)
  {
    LCD_ShowMixedString(LCD_TEXT_X, AI_INFO_Y, LCD_TEXT_W, AI_INFO_H,
                        "AI: waiting\nSay something\nWaiting ai_plan\nUART2 ready\n\n",
                        YELLOW, BLACK);
  }
  else
  {
    LCD_ShowMixedString(LCD_TEXT_X, AI_INFO_Y, LCD_TEXT_W, AI_INFO_H,
                        "AI: typing...\nESP32 online\nListening USB\nWaiting result\n\n",
                        BLUE, BLACK);
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  const uint32_t now = HAL_GetTick();

  if (GPIO_Pin == KEY_F1_Pin)
  {
    if ((now - f1_irq_tick) >= 250UL)
    {
      f1_irq_tick = now;
      f1_pressed = 1U;
    }
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif /* USE_FULL_ASSERT */
