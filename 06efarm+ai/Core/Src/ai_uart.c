#include "ai_uart.h"

#include "usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AI_RX_LINE_SIZE 2048U
#define AI_RX_RING_SIZE 3072U
#define AI_TX_LINE_SIZE 192U
#define AI_DEFAULT_PLAN_MS 300000UL

static uint8_t rx_ring[AI_RX_RING_SIZE];
static char rx_parse_line[AI_RX_LINE_SIZE];
static uint16_t rx_parse_len;
static uint8_t rx_byte;
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint8_t rx_overflow;
static uint8_t rx_line_dropping;
static AiPlan current_plan;
static VoiceStatus voice_status;
static HeaderInfo header_info;
static uint8_t new_plan;
static uint8_t new_voice_ack;
static uint8_t new_header_info;

static uint8_t ClampU8(uint32_t value, uint8_t max_value)
{
  if (value > (uint32_t)max_value)
  {
    return max_value;
  }
  return (uint8_t)value;
}

static const char *Json_FindKey(const char *json, const char *key)
{
  char pattern[32];
  (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  return strstr(json, pattern);
}

static uint8_t Json_GetString(const char *json, const char *key, char *out, uint16_t out_size)
{
  const char *p;
  const char *end;
  uint16_t i = 0U;

  if ((json == NULL) || (key == NULL) || (out == NULL) || (out_size == 0U))
  {
    return 0U;
  }

  p = Json_FindKey(json, key);
  if (p == NULL)
  {
    return 0U;
  }

  p = strchr(p, ':');
  if (p == NULL)
  {
    return 0U;
  }
  p++;
  while ((*p == ' ') || (*p == '\t'))
  {
    p++;
  }
  if (*p != '"')
  {
    return 0U;
  }
  p++;
  end = p;

  while ((*end != '\0') && (*end != '"') && (i + 1U < out_size))
  {
    if ((*end == '\\') && (end[1] != '\0'))
    {
      end++;
    }
    out[i++] = *end++;
  }
  out[i] = '\0';
  return 1U;
}

static uint8_t Json_GetFloat(const char *json, const char *key, float *out)
{
  const char *p;

  if ((json == NULL) || (key == NULL) || (out == NULL))
  {
    return 0U;
  }

  p = Json_FindKey(json, key);
  if (p == NULL)
  {
    return 0U;
  }

  p = strchr(p, ':');
  if (p == NULL)
  {
    return 0U;
  }
  p++;
  *out = (float)strtod(p, NULL);
  return 1U;
}

static uint8_t Json_GetUint32(const char *json, const char *key, uint32_t *out)
{
  const char *p;

  if ((json == NULL) || (key == NULL) || (out == NULL))
  {
    return 0U;
  }

  p = Json_FindKey(json, key);
  if (p == NULL)
  {
    return 0U;
  }

  p = strchr(p, ':');
  if (p == NULL)
  {
    return 0U;
  }
  p++;
  *out = (uint32_t)strtoul(p, NULL, 10);
  return 1U;
}

static uint8_t Json_CopyNextObject(const char **cursor, char *out, uint16_t out_size)
{
  const char *p;
  const char *start;
  int depth = 0;
  uint16_t len;

  if ((cursor == NULL) || (*cursor == NULL) || (out == NULL) || (out_size == 0U))
  {
    return 0U;
  }

  p = *cursor;
  while ((*p != '\0') && (*p != ']') && (*p != '{'))
  {
    p++;
  }
  if ((*p == '\0') || (*p == ']'))
  {
    *cursor = p;
    return 0U;
  }

  start = p;
  while (*p != '\0')
  {
    if (*p == '{')
    {
      depth++;
    }
    else if (*p == '}')
    {
      depth--;
      if (depth == 0)
      {
        p++;
        len = (uint16_t)(p - start);
        if (len >= out_size)
        {
          len = out_size - 1U;
        }
        memcpy(out, start, len);
        out[len] = '\0';
        *cursor = p;
        return 1U;
      }
    }
    p++;
  }

  *cursor = p;
  return 0U;
}

static uint8_t AiUart_DeviceCode(const char *device)
{
  if (strcmp(device, "fan") == 0)
  {
    return 1U;
  }
  if (strcmp(device, "lamp") == 0)
  {
    return 2U;
  }
  if (strcmp(device, "curtain") == 0)
  {
    return 3U;
  }
  if (strcmp(device, "pump") == 0)
  {
    return 4U;
  }
  return 0U;
}

static uint8_t AiUart_ActionIsOn(const char *action, uint8_t device)
{
  if ((strcmp(action, "on") == 0) || (strcmp(action, "open") == 0))
  {
    return 1U;
  }
  if ((strcmp(action, "off") == 0) || (strcmp(action, "close") == 0))
  {
    return 0U;
  }
  if (device == 3U)
  {
    return 1U;
  }
  return 0U;
}

static void AiUart_ApplyActionToPlan(AiPlan *plan, const AiAction *action)
{
  if ((plan == NULL) || (action == NULL) || (action->valid == 0U))
  {
    return;
  }

  if (action->device == 1U)
  {
    plan->fan_valid = 1U;
    plan->fan_on = action->on;
    plan->fan_mode_natural = action->fan_mode_natural;
    if (action->on != 0U)
    {
      plan->fan_level = (action->level != 0U) ? action->level : 2U;
      plan->fan_duty = (action->duty != 0U) ? action->duty : 70U;
    }
    else
    {
      plan->fan_level = 0U;
      plan->fan_duty = 0U;
    }
  }
  else if (action->device == 2U)
  {
    plan->lamp_valid = 1U;
    plan->lamp_on = action->on;
    if (action->on != 0U)
    {
      plan->lamp_level = (action->level != 0U) ? action->level : 2U;
      plan->lamp_duty = (action->duty != 0U) ? action->duty : 60U;
    }
    else
    {
      plan->lamp_level = 0U;
      plan->lamp_duty = 0U;
    }
  }
  else if (action->device == 3U)
  {
    plan->curtain_valid = 1U;
    plan->curtain_open = action->on;
    plan->curtain_percent_valid = 1U;
    if (action->percent_valid != 0U)
    {
      plan->curtain_percent = action->percent;
    }
    else
    {
      plan->curtain_percent = (action->on != 0U) ? 100U : 0U;
    }
  }
}

static void AiUart_AddAction(AiPlan *plan, const AiAction *action)
{
  if ((plan == NULL) || (action == NULL) || (action->valid == 0U))
  {
    return;
  }

  if (plan->action_count < AI_MAX_ACTIONS)
  {
    plan->actions[plan->action_count] = *action;
    plan->action_count++;
  }
  AiUart_ApplyActionToPlan(plan, action);
}

static void AiUart_ParseActions(const char *json, AiPlan *plan)
{
  const char *p;
  char object[256];

  if ((json == NULL) || (plan == NULL))
  {
    return;
  }

  p = Json_FindKey(json, "actions");
  if (p == NULL)
  {
    return;
  }
  p = strchr(p, '[');
  if (p == NULL)
  {
    return;
  }
  p++;

  while ((plan->action_count < AI_MAX_ACTIONS) &&
         (Json_CopyNextObject(&p, object, sizeof(object)) != 0U))
  {
    AiAction action;
    char device[16];
    char action_text[16];
    char mode[16];
    uint32_t value;

    memset(&action, 0, sizeof(action));
    memset(device, 0, sizeof(device));
    memset(action_text, 0, sizeof(action_text));
    memset(mode, 0, sizeof(mode));

    (void)Json_GetString(object, "device", device, sizeof(device));
    (void)Json_GetString(object, "action", action_text, sizeof(action_text));
    if (action_text[0] == '\0')
    {
      (void)Json_GetString(object, "suggested_state", action_text, sizeof(action_text));
    }
    action.device = AiUart_DeviceCode(device);
    if (action.device == 0U)
    {
      continue;
    }
    action.valid = 1U;
    action.on = AiUart_ActionIsOn(action_text, action.device);

    if (Json_GetString(object, "mode", mode, sizeof(mode)) != 0U)
    {
      action.fan_mode_natural = (strcmp(mode, "natural") == 0) ? 1U : 0U;
    }
    if (Json_GetUint32(object, "level", &value) != 0U)
    {
      action.level = ClampU8(value, 3U);
    }
    if (Json_GetUint32(object, "duty", &value) != 0U)
    {
      action.duty = ClampU8(value, 100U);
    }
    if (Json_GetUint32(object, "percent", &value) != 0U)
    {
      action.percent_valid = 1U;
      action.percent = ClampU8(value, 100U);
    }
    if (Json_GetUint32(object, "duration_sec", &value) != 0U)
    {
      action.duration_ms = value * 1000UL;
    }
    AiUart_AddAction(plan, &action);
  }
}

static void AiUart_SetFallbackDisplay(AiPlan *plan)
{
  if (plan->title[0] == '\0')
  {
    (void)strncpy(plan->title, "AI", sizeof(plan->title) - 1U);
  }
  if (plan->line1[0] == '\0')
  {
    (void)strncpy(plan->line1, plan->cmd, sizeof(plan->line1) - 1U);
  }
  if (plan->line2[0] == '\0')
  {
    if (plan->temp_target_valid != 0U)
    {
      (void)snprintf(plan->line2, sizeof(plan->line2), "Target T %.1fC", plan->temp_target);
    }
    else if (plan->humi_target_valid != 0U)
    {
      (void)snprintf(plan->line2, sizeof(plan->line2), "Target H %.1f%%", plan->humi_target);
    }
    else
    {
      (void)strncpy(plan->line2, "Plan received", sizeof(plan->line2) - 1U);
    }
  }
  if (plan->level[0] == '\0')
  {
    (void)strncpy(plan->level, "info", sizeof(plan->level) - 1U);
  }
  if (plan->advice[0] == '\0')
  {
    (void)snprintf(plan->advice, sizeof(plan->advice), "%s. %s", plan->line1, plan->line2);
  }
}

static void AiUart_ParsePlan(const char *json)
{
  AiPlan plan;
  uint32_t duration_sec = 0UL;
  uint32_t value;
  char fan_mode[16];

  if ((json == NULL) || (strstr(json, "\"type\":\"ai_plan\"") == NULL))
  {
    return;
  }

  memset(&plan, 0, sizeof(plan));
  plan.valid = 1U;
  plan.duration_ms = AI_DEFAULT_PLAN_MS;
  plan.received_tick = HAL_GetTick();

  (void)Json_GetString(json, "reply", plan.advice, sizeof(plan.advice));
  (void)Json_GetString(json, "cmd", plan.cmd, sizeof(plan.cmd));
  (void)Json_GetString(json, "title", plan.title, sizeof(plan.title));
  (void)Json_GetString(json, "line1", plan.line1, sizeof(plan.line1));
  (void)Json_GetString(json, "line2", plan.line2, sizeof(plan.line2));
  (void)Json_GetString(json, "reason", plan.reason, sizeof(plan.reason));
  (void)Json_GetString(json, "level", plan.level, sizeof(plan.level));
  memset(fan_mode, 0, sizeof(fan_mode));
  if (Json_GetString(json, "fan_mode", fan_mode, sizeof(fan_mode)) != 0U)
  {
    if (strcmp(fan_mode, "natural") == 0)
    {
      plan.fan_mode_natural = 1U;
    }
  }

  if (Json_GetUint32(json, "fan_level", &value) != 0U)
  {
    plan.fan_level = ClampU8(value, 3U);
  }
  if (Json_GetUint32(json, "fan_duty", &value) != 0U)
  {
    plan.fan_duty = ClampU8(value, 100U);
  }
  if (Json_GetUint32(json, "lamp_level", &value) != 0U)
  {
    plan.lamp_level = ClampU8(value, 3U);
  }
  if (Json_GetUint32(json, "lamp_duty", &value) != 0U)
  {
    plan.lamp_duty = ClampU8(value, 100U);
  }
  if (Json_GetUint32(json, "curtain_percent", &value) != 0U)
  {
    plan.curtain_percent_valid = 1U;
    plan.curtain_percent = ClampU8(value, 100U);
  }

  if (Json_GetUint32(json, "duration_sec", &duration_sec) != 0U)
  {
    if (duration_sec > 0UL)
    {
      plan.duration_ms = duration_sec * 1000UL;
    }
  }

  if (strcmp(plan.cmd, "fan_on") == 0)
  {
    plan.fan_valid = 1U;
    plan.fan_on = 1U;
    if (plan.fan_level == 0U)
    {
      plan.fan_level = 2U;
    }
    if (plan.fan_duty == 0U)
    {
      plan.fan_duty = 70U;
    }
  }
  else if (strcmp(plan.cmd, "fan_off") == 0)
  {
    plan.fan_valid = 1U;
    plan.fan_on = 0U;
    plan.fan_level = 0U;
    plan.fan_duty = 0U;
  }
  else if (strcmp(plan.cmd, "lamp_on") == 0)
  {
    plan.lamp_valid = 1U;
    plan.lamp_on = 1U;
    if (plan.lamp_level == 0U)
    {
      plan.lamp_level = 2U;
    }
    if (plan.lamp_duty == 0U)
    {
      plan.lamp_duty = 60U;
    }
  }
  else if (strcmp(plan.cmd, "lamp_off") == 0)
  {
    plan.lamp_valid = 1U;
    plan.lamp_on = 0U;
    plan.lamp_level = 0U;
    plan.lamp_duty = 0U;
  }
  else if (strcmp(plan.cmd, "curtain_open") == 0)
  {
    plan.curtain_valid = 1U;
    plan.curtain_open = 1U;
    if (plan.curtain_percent_valid == 0U)
    {
      plan.curtain_percent_valid = 1U;
      plan.curtain_percent = 100U;
    }
  }
  else if (strcmp(plan.cmd, "curtain_close") == 0)
  {
    plan.curtain_valid = 1U;
    plan.curtain_open = 0U;
    if (plan.curtain_percent_valid == 0U)
    {
      plan.curtain_percent_valid = 1U;
      plan.curtain_percent = 0U;
    }
  }
  AiUart_ParseActions(json, &plan);

  plan.temp_target_valid = Json_GetFloat(json, "temp", &plan.temp_target);
  plan.humi_target_valid = Json_GetFloat(json, "humi", &plan.humi_target);
  plan.light_min_valid = Json_GetFloat(json, "light_min", &plan.light_min);

  AiUart_SetFallbackDisplay(&plan);

  current_plan = plan;
  new_plan = 1U;
}

static void AiUart_ParseVoiceAck(const char *json)
{
  VoiceStatus status;

  if ((json == NULL) || (strstr(json, "\"type\":\"voice_ack\"") == NULL))
  {
    return;
  }

  memset(&status, 0, sizeof(status));
  (void)Json_GetString(json, "state", status.state, sizeof(status.state));
  (void)Json_GetString(json, "text", status.text, sizeof(status.text));
  if (status.state[0] == '\0')
  {
    (void)strncpy(status.state, "unknown", sizeof(status.state) - 1U);
  }
  if (status.text[0] == '\0')
  {
    (void)strncpy(status.text, status.state, sizeof(status.text) - 1U);
  }

  voice_status = status;
  new_voice_ack = 1U;
}

static void AiUart_ParseHeaderInfo(const char *json)
{
  HeaderInfo info;

  if ((json == NULL) || (strstr(json, "\"type\":\"header_info\"") == NULL))
  {
    return;
  }

  memset(&info, 0, sizeof(info));
  (void)Json_GetString(json, "time", info.time, sizeof(info.time));
  (void)Json_GetString(json, "weather", info.weather, sizeof(info.weather));
  if (info.time[0] == '\0')
  {
    (void)strncpy(info.time, "--:--:--", sizeof(info.time) - 1U);
  }
  if (info.weather[0] == '\0')
  {
    (void)strncpy(info.weather, "Nanchang weather --", sizeof(info.weather) - 1U);
  }

  header_info = info;
  new_header_info = 1U;
}

static void AiUart_ParseLine(const char *json)
{
  if (json == NULL)
  {
    return;
  }

  if (strstr(json, "\"type\":\"ai_plan\"") != NULL)
  {
    AiUart_ParsePlan(json);
  }
  else if (strstr(json, "\"type\":\"voice_ack\"") != NULL)
  {
    AiUart_ParseVoiceAck(json);
  }
  else if (strstr(json, "\"type\":\"header_info\"") != NULL)
  {
    AiUart_ParseHeaderInfo(json);
  }
}

void AiUart_Init(void)
{
  rx_parse_len = 0U;
  rx_byte = 0U;
  rx_head = 0U;
  rx_tail = 0U;
  rx_overflow = 0U;
  rx_line_dropping = 0U;
  new_plan = 0U;
  new_voice_ack = 0U;
  new_header_info = 0U;
  memset(rx_ring, 0, sizeof(rx_ring));
  memset(rx_parse_line, 0, sizeof(rx_parse_line));
  memset(&current_plan, 0, sizeof(current_plan));
  memset(&voice_status, 0, sizeof(voice_status));
  memset(&header_info, 0, sizeof(header_info));
  (void)strncpy(voice_status.state, "idle", sizeof(voice_status.state) - 1U);
  (void)strncpy(voice_status.text, "Voice idle", sizeof(voice_status.text) - 1U);
  (void)strncpy(header_info.time, "Time --:--:--", sizeof(header_info.time) - 1U);
  (void)strncpy(header_info.weather, "Nanchang weather --", sizeof(header_info.weather) - 1U);
  (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1U);
}

void AiUart_Poll(void)
{
  uint8_t byte;

  while (1)
  {
    __disable_irq();
    if (rx_tail == rx_head)
    {
      if (rx_overflow != 0U)
      {
        rx_overflow = 0U;
      }
      __enable_irq();
      break;
    }

    byte = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % AI_RX_RING_SIZE);
    __enable_irq();

    if (byte == '\n')
    {
      if ((rx_line_dropping == 0U) && (rx_parse_len > 0U))
      {
        rx_parse_line[rx_parse_len] = '\0';
        AiUart_ParseLine(rx_parse_line);
      }
      rx_parse_len = 0U;
      rx_line_dropping = 0U;
    }
    else if (byte != '\r')
    {
      if (rx_line_dropping != 0U)
      {
        continue;
      }

      if (rx_parse_len + 1U < AI_RX_LINE_SIZE)
      {
        rx_parse_line[rx_parse_len++] = (char)byte;
      }
      else
      {
        rx_parse_len = 0U;
        rx_line_dropping = 1U;
        rx_overflow = 1U;
      }
    }
  }
}

void AiUart_SendEnv(const E53_IA1_Data *data, uint8_t motor_on, uint8_t led_on)
{
  char tx[AI_TX_LINE_SIZE];
  int len;

  if (data == NULL)
  {
    return;
  }

  len = snprintf(tx, sizeof(tx),
                 "{\"type\":\"env\",\"temp\":%.1f,\"humi\":%.1f,\"light\":%.1f,"
                 "\"fan\":%u,\"lamp\":%u,\"pump\":0}\n",
                 data->temp, data->humi, data->Lux, motor_on, led_on);
  if ((len > 0) && (len < (int)sizeof(tx)))
  {
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)tx, (uint16_t)len, 50U);
  }
}

void AiUart_SendVoiceCmd(uint8_t start)
{
  char tx[64];
  int len;

  len = snprintf(tx, sizeof(tx),
                 "{\"type\":\"voice_cmd\",\"cmd\":\"%s\"}\n",
                 (start != 0U) ? "start" : "stop");
  if ((len > 0) && (len < (int)sizeof(tx)))
  {
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)tx, (uint16_t)len, 50U);
  }
}

uint8_t AiUart_HasNewPlan(void)
{
  uint8_t ret = new_plan;
  new_plan = 0U;
  return ret;
}

const AiPlan *AiUart_GetPlan(void)
{
  return &current_plan;
}

uint8_t AiUart_IsPlanActive(void)
{
  if (current_plan.valid == 0U)
  {
    return 0U;
  }

  if ((HAL_GetTick() - current_plan.received_tick) > current_plan.duration_ms)
  {
    return 0U;
  }

  return 1U;
}

uint8_t AiUart_HasVoiceAck(void)
{
  uint8_t ret = new_voice_ack;
  new_voice_ack = 0U;
  return ret;
}

const VoiceStatus *AiUart_GetVoiceStatus(void)
{
  return &voice_status;
}

uint8_t AiUart_HasHeaderInfo(void)
{
  uint8_t ret = new_header_info;
  new_header_info = 0U;
  return ret;
}

const HeaderInfo *AiUart_GetHeaderInfo(void)
{
  return &header_info;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  uint16_t next_head;

  if (huart->Instance != USART2)
  {
    return;
  }

  next_head = (uint16_t)((rx_head + 1U) % AI_RX_RING_SIZE);
  if (next_head != rx_tail)
  {
    rx_ring[rx_head] = rx_byte;
    rx_head = next_head;
  }
  else
  {
    rx_overflow = 1U;
  }

  (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    __HAL_UART_CLEAR_OREFLAG(huart);
    (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1U);
  }
}
