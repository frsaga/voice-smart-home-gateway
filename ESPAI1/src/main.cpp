#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "AiTypes.h"
#include "AppConfig.h"
#include "DisplayService.h"
#include "QwenClient.h"
#include "UartProtocol.h"
#include "VoiceInput.h"
#include "WifiService.h"

static WifiService wifi;
static DisplayService screen;
static UartProtocol stm32;
static QwenClient qwen;
static VoiceInput voice;
static WebServer webServer(80);
static EnvData currentEnv;

struct WebDeviceState {
  bool fanOn = false;
  String fanMode = "direct";
  int fanLevel = 0;
  int fanDuty = 0;
  bool lampOn = false;
  int lampLevel = 0;
  int lampDuty = 0;
  bool curtainKnown = false;
  bool curtainOpen = false;
  int curtainPercent = 0;
};

enum class AiJobType : uint8_t {
  Text,
  VoiceStop,
};

struct AiJob {
  AiJobType type;
  char text[256];
};

static QueueHandle_t aiQueue = nullptr;
static SemaphoreHandle_t envMutex = nullptr;
static SemaphoreHandle_t uiMutex = nullptr;
static SemaphoreHandle_t uartTxMutex = nullptr;
static volatile bool voiceProcessing = false;
static bool webServerStarted = false;
static bool mdnsStarted = false;
static WebDeviceState webDeviceState;
static String currentTimeText = "--:--:--";
static String currentWeatherText = "Nanchang weather --";
static uint32_t lastHeaderSendMs = 0;
static uint32_t lastWeatherFetchMs = 0;
static bool timeConfigured = false;

static void updateWebDeviceStateFromEnv(const EnvData& env);

static void lockMutex(SemaphoreHandle_t mutex) {
  if (mutex != nullptr) {
    xSemaphoreTake(mutex, portMAX_DELAY);
  }
}

static void unlockMutex(SemaphoreHandle_t mutex) {
  if (mutex != nullptr) {
    xSemaphoreGive(mutex);
  }
}

static EnvData getEnvSnapshot() {
  EnvData env;
  lockMutex(envMutex);
  env = currentEnv;
  unlockMutex(envMutex);
  return env;
}

static void setEnvSnapshot(const EnvData& env) {
  lockMutex(envMutex);
  currentEnv = env;
  unlockMutex(envMutex);
}

static void showEnvSafe(const EnvData& env) {
  lockMutex(uiMutex);
  screen.showEnv(env);
  unlockMutex(uiMutex);
}

static void showPlanSafe(const AiPlan& plan) {
  lockMutex(uiMutex);
  screen.showPlan(plan);
  unlockMutex(uiMutex);
}

static void showMessageSafe(const String& title, const String& body) {
  lockMutex(uiMutex);
  screen.showMessage(title, body);
  unlockMutex(uiMutex);
}

static void sendAiPlanSafe(const AiPlan& plan) {
  lockMutex(uartTxMutex);
  stm32.sendAiPlan(plan);
  unlockMutex(uartTxMutex);
}

static void sendVoiceAckSafe(const String& state, const String& text) {
  lockMutex(uartTxMutex);
  stm32.sendVoiceAck(state, text);
  unlockMutex(uartTxMutex);
}

static void sendStatusSafe(const String& status) {
  lockMutex(uartTxMutex);
  stm32.sendStatus(status);
  unlockMutex(uartTxMutex);
}

static void sendHeaderInfoSafe(const String& timeText, const String& weatherText) {
  lockMutex(uartTxMutex);
  stm32.sendHeaderInfo(timeText, weatherText);
  unlockMutex(uartTxMutex);
}

static bool enqueueAiText(const String& text) {
  if (aiQueue == nullptr) {
    return false;
  }
  AiJob job = {};
  job.type = AiJobType::Text;
  text.toCharArray(job.text, sizeof(job.text));
  return xQueueSend(aiQueue, &job, 0) == pdTRUE;
}

static bool enqueueVoiceStop(const String& reason) {
  if (aiQueue == nullptr) {
    return false;
  }
  AiJob job = {};
  job.type = AiJobType::VoiceStop;
  reason.toCharArray(job.text, sizeof(job.text));
  return xQueueSend(aiQueue, &job, 0) == pdTRUE;
}

static const char* weatherCodeText(int code) {
  if (code == 0) {
    return "Clear";
  }
  if (code == 1 || code == 2) {
    return "Partly";
  }
  if (code == 3) {
    return "Cloudy";
  }
  if (code == 45 || code == 48) {
    return "Fog";
  }
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    return "Rain";
  }
  if (code >= 71 && code <= 77) {
    return "Snow";
  }
  if (code >= 95) {
    return "Thunder";
  }
  return "Weather";
}

static void configureTimeIfNeeded() {
  if (timeConfigured) {
    return;
  }
  configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
  timeConfigured = true;
}

static void updateTimeText() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) {
    return;
  }
  char buf[32];
  strftime(buf, sizeof(buf), "%m-%d %H:%M:%S", &timeinfo);
  currentTimeText = String("Time ") + buf;
}

static bool fetchNanchangWeather() {
  if (!wifi.connected()) {
    return false;
  }

  HTTPClient http;
  const char* url =
      "http://api.open-meteo.com/v1/forecast?latitude=28.682&longitude=115.858&current=temperature_2m,relative_humidity_2m,weather_code&timezone=Asia%2FShanghai";
  if (!http.begin(url)) {
    return false;
  }
  http.setTimeout(6000);
  const int code = http.GET();
  const String body = http.getString();
  http.end();
  if (code < 200 || code >= 300 || body.length() == 0) {
    Serial.printf("[WEATHER] failed code=%d\n", code);
    return false;
  }

  StaticJsonDocument<1536> doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("[WEATHER] JSON failed: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonObject current = doc["current"];
  const float temp = current["temperature_2m"] | NAN;
  const int humi = current["relative_humidity_2m"] | -1;
  const int weatherCode = current["weather_code"] | -1;
  if (isnan(temp) || humi < 0) {
    return false;
  }

  currentWeatherText = "Nanchang ";
  currentWeatherText += weatherCodeText(weatherCode);
  currentWeatherText += " ";
  currentWeatherText += String(temp, 1);
  currentWeatherText += "C ";
  currentWeatherText += String(humi);
  currentWeatherText += "%";
  Serial.print("[WEATHER] ");
  Serial.println(currentWeatherText);
  return true;
}

static void updateHeaderInfo() {
  if (!wifi.connected()) {
    return;
  }

  configureTimeIfNeeded();
  updateTimeText();

  const uint32_t now = millis();
  if (lastWeatherFetchMs == 0 || (now - lastWeatherFetchMs) >= 15UL * 60UL * 1000UL) {
    if (fetchNanchangWeather()) {
      lastWeatherFetchMs = now;
    } else if (lastWeatherFetchMs == 0) {
      lastWeatherFetchMs = now - (14UL * 60UL * 1000UL);
    }
  }

  if (lastHeaderSendMs == 0 || (now - lastHeaderSendMs) >= 1000UL) {
    sendHeaderInfoSafe(currentTimeText, currentWeatherText);
    lastHeaderSendMs = now;
  }
}

static void printHelp() {
  Serial.println();
  Serial.println("ESPAI1 commands:");
  Serial.println("  help");
  Serial.println("  demo");
  Serial.println("  ai <user natural language>");
  Serial.println("  ask <user natural language>");
  Serial.println("  <plain text question>");
  Serial.println("  Web: open http://espai1.local/ or http://<esp32-ip>/ after WiFi connects");
  Serial.println("  open curtain / close curtain / 打开窗帘 / 关闭窗帘");
  Serial.println("  env {\"type\":\"env\",\"temp\":30.2,\"humi\":78,\"soil\":42,\"light\":600,\"fan\":0,\"pump\":0,\"lamp\":0}");
  Serial.println();
}

static bool isValidUtf8(const String& text) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(text.c_str());
  size_t i = 0;
  const size_t len = text.length();
  while (i < len) {
    uint8_t c = bytes[i];
    if (c <= 0x7F) {
      i++;
    } else if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= len || (bytes[i + 1] & 0xC0) != 0x80 || c < 0xC2) {
        return false;
      }
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80) {
        return false;
      }
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80 ||
          (bytes[i + 3] & 0xC0) != 0x80) {
        return false;
      }
      i += 4;
    } else {
      return false;
    }
  }
  return true;
}

static void printUsbAiResponse(const String& userText, const AiPlan& plan) {
  StaticJsonDocument<2048> doc;
  doc["type"] = "usb_ai_response";
  doc["user_text"] = userText;
  doc["reply"] = plan.reply;
  doc["intent"] = plan.intent;
  doc["cmd"] = plan.cmd;
  doc["device"] = plan.device;
  doc["action"] = plan.action;
  doc["duration_sec"] = plan.durationSec;
  doc["control_mode"] = plan.controlMode;
  doc["fan_mode"] = plan.fanMode;
  doc["fan_level"] = plan.fanLevel;
  doc["fan_duty"] = plan.fanDuty;
  doc["lamp_level"] = plan.lampLevel;
  doc["lamp_duty"] = plan.lampDuty;
  if (plan.curtainPercent >= 0) {
    doc["curtain_percent"] = plan.curtainPercent;
  }
  if (plan.actionCount > 0) {
    JsonArray actions = doc.createNestedArray("actions");
    for (size_t i = 0; i < plan.actionCount; ++i) {
      JsonObject action = actions.createNestedObject();
      action["device"] = plan.actions[i].device;
      action["action"] = plan.actions[i].action;
      action["duration_sec"] = plan.actions[i].durationSec;
      if (plan.actions[i].device == "fan") {
        action["mode"] = plan.actions[i].mode;
        action["level"] = plan.actions[i].level;
        action["duty"] = plan.actions[i].duty;
      } else if (plan.actions[i].device == "lamp") {
        action["level"] = plan.actions[i].level;
        action["duty"] = plan.actions[i].duty;
      } else if (plan.actions[i].device == "curtain" && plan.actions[i].percent >= 0) {
        action["percent"] = plan.actions[i].percent;
      }
    }
  }
  doc["confidence"] = plan.confidence;
  doc["reason"] = plan.reason;

  JsonObject display = doc.createNestedObject("display");
  display["title"] = plan.displayTitle;
  display["line1"] = plan.displayLine1;
  display["line2"] = plan.displayLine2;
  display["level"] = plan.displayLevel;

  JsonObject targets = doc.createNestedObject("targets");
  if (!isnan(plan.tempTarget)) {
    targets["temp"] = plan.tempTarget;
  }
  if (!isnan(plan.humiTarget)) {
    targets["humi"] = plan.humiTarget;
  }
  if (!isnan(plan.soilMin)) {
    targets["soil_min"] = plan.soilMin;
  }
  if (!isnan(plan.soilMax)) {
    targets["soil_max"] = plan.soilMax;
  }
  if (!isnan(plan.lightMin)) {
    targets["light_min"] = plan.lightMin;
  }
  if (!isnan(plan.lightMax)) {
    targets["light_max"] = plan.lightMax;
  }

  Serial.println("[AI_REPLY]");
  Serial.println(plan.reply);
  Serial.print("[AI_JSON] ");
  serializeJson(doc, Serial);
  Serial.println();
}

static void printUsbAiError(const String& userText, const String& error) {
  StaticJsonDocument<768> doc;
  String shortError = error;
  if (shortError.length() > 320) {
    shortError = shortError.substring(0, 320) + "...";
  }

  doc["type"] = "usb_ai_response";
  doc["user_text"] = userText;
  doc["ok"] = false;
  doc["error"] = shortError;

  Serial.print("[AI_JSON] ");
  serializeJson(doc, Serial);
  Serial.println();
}

static bool parseEnvFromUsb(const String& json) {
  StaticJsonDocument<512> doc;
  const DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("[USB] env JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }

  EnvData env = getEnvSnapshot();
  env.valid = true;
  env.temp = doc["temp"] | env.temp;
  env.humi = doc["humi"] | env.humi;
  env.soil = doc["soil"] | env.soil;
  env.light = doc["light"] | env.light;
  env.fan = doc["fan"] | env.fan;
  env.pump = doc["pump"] | env.pump;
  env.lamp = doc["lamp"] | env.lamp;
  setEnvSnapshot(env);
  updateWebDeviceStateFromEnv(env);

  Serial.printf("[ENV] T=%.1f H=%.0f Soil=%.0f Light=%.0f F%d P%d L%d\n",
                env.temp,
                env.humi,
                env.soil,
                env.light,
                env.fan,
                env.pump,
                env.lamp);
  showEnvSafe(env);
  return true;
}

static int directLevelToFanDuty(int level) {
  if (level <= 0) {
    return 0;
  }
  if (level == 1) {
    return 40;
  }
  if (level == 2) {
    return 70;
  }
  return 100;
}

static int directLevelToLampDuty(int level) {
  if (level <= 0) {
    return 0;
  }
  if (level == 1) {
    return 30;
  }
  if (level == 2) {
    return 60;
  }
  return 100;
}

static String directCmdFromAction(const AiAction& action) {
  if (action.device == "fan" && action.action == "on") {
    return "fan_on";
  }
  if (action.device == "fan" && action.action == "off") {
    return "fan_off";
  }
  if (action.device == "lamp" && action.action == "on") {
    return "lamp_on";
  }
  if (action.device == "lamp" && action.action == "off") {
    return "lamp_off";
  }
  if (action.device == "curtain" && action.action == "open") {
    return "curtain_open";
  }
  if (action.device == "curtain" && action.action == "close") {
    return "curtain_close";
  }
  return "none";
}

static bool directAddAction(AiPlan& plan, AiAction action) {
  if (plan.actionCount >= AiPlan::kMaxActions) {
    return false;
  }

  if (action.durationSec <= 0) {
    action.durationSec = plan.durationSec;
  }
  if (action.device == "fan") {
    if (action.mode.length() == 0) {
      action.mode = "direct";
    }
    if (action.action == "on") {
      if (action.level <= 0) {
        action.level = 2;
      }
      if (action.duty <= 0) {
        action.duty = directLevelToFanDuty(action.level);
      }
    }
    plan.fanMode = action.mode;
    plan.fanLevel = action.level;
    plan.fanDuty = action.duty;
  } else if (action.device == "lamp") {
    if (action.action == "on") {
      if (action.level <= 0) {
        action.level = 2;
      }
      if (action.duty <= 0) {
        action.duty = directLevelToLampDuty(action.level);
      }
    }
    plan.lampLevel = action.level;
    plan.lampDuty = action.duty;
  } else if (action.device == "curtain") {
    if (action.percent < 0) {
      action.percent = (action.action == "open") ? 100 : 0;
    }
    plan.curtainPercent = action.percent;
  }

  plan.actions[plan.actionCount++] = action;
  if (plan.actionCount == 1) {
    plan.cmd = directCmdFromAction(action);
    plan.device = action.device;
    plan.action = action.action;
  } else {
    plan.cmd = "multi";
    plan.device = "multi";
    plan.action = "multi";
  }
  return true;
}

static void addSingleTopLevelAction(AiPlan& plan) {
  if (plan.actionCount != 0 || plan.cmd == "none" || plan.cmd == "multi") {
    return;
  }

  AiAction action;
  action.durationSec = plan.durationSec;
  action.mode = plan.fanMode;
  if (plan.cmd == "fan_on") {
    action.device = "fan";
    action.action = "on";
    action.level = plan.fanLevel;
    action.duty = plan.fanDuty;
  } else if (plan.cmd == "fan_off") {
    action.device = "fan";
    action.action = "off";
  } else if (plan.cmd == "lamp_on") {
    action.device = "lamp";
    action.action = "on";
    action.level = plan.lampLevel;
    action.duty = plan.lampDuty;
  } else if (plan.cmd == "lamp_off") {
    action.device = "lamp";
    action.action = "off";
  } else if (plan.cmd == "curtain_open") {
    action.device = "curtain";
    action.action = "open";
    action.percent = plan.curtainPercent;
  } else if (plan.cmd == "curtain_close") {
    action.device = "curtain";
    action.action = "close";
    action.percent = plan.curtainPercent;
  } else {
    return;
  }
  (void)directAddAction(plan, action);
}

static void updateWebDeviceStateFromEnv(const EnvData& env) {
  webDeviceState.fanOn = env.fan != 0;
  if (!webDeviceState.fanOn) {
    webDeviceState.fanLevel = 0;
    webDeviceState.fanDuty = 0;
  }
  webDeviceState.lampOn = env.lamp != 0;
  if (!webDeviceState.lampOn) {
    webDeviceState.lampLevel = 0;
    webDeviceState.lampDuty = 0;
  }
}

static void updateWebDeviceStateFromAction(const AiAction& action) {
  if (action.device == "fan") {
    webDeviceState.fanOn = action.action == "on";
    webDeviceState.fanMode = action.mode.length() ? action.mode : "direct";
    webDeviceState.fanLevel = webDeviceState.fanOn ? action.level : 0;
    webDeviceState.fanDuty = webDeviceState.fanOn ? action.duty : 0;
  } else if (action.device == "lamp") {
    webDeviceState.lampOn = action.action == "on";
    webDeviceState.lampLevel = webDeviceState.lampOn ? action.level : 0;
    webDeviceState.lampDuty = webDeviceState.lampOn ? action.duty : 0;
  } else if (action.device == "curtain") {
    webDeviceState.curtainKnown = true;
    webDeviceState.curtainOpen = action.action == "open";
    webDeviceState.curtainPercent = constrain(action.percent, 0, 100);
  }
}

static bool makeStructuredActionPlan(const AiAction& action, AiPlan& plan, String& error) {
  if (action.device != "fan" && action.device != "lamp" && action.device != "curtain") {
    error = "unsupported device";
    return false;
  }
  if (action.device == "curtain") {
    if (action.action != "open" && action.action != "close") {
      error = "bad curtain action";
      return false;
    }
  } else if (action.action != "on" && action.action != "off") {
    error = "bad device action";
    return false;
  }

  plan = AiPlan{};
  plan.valid = true;
  plan.intent = "web_control";
  plan.controlMode = "direct";
  plan.confidence = 1.0f;
  plan.reason = "web_structured_action";
  plan.displayTitle = "AI Advice";
  plan.displayLevel = "info";
  plan.durationSec = 300;

  if (!directAddAction(plan, action)) {
    error = "too many actions";
    return false;
  }

  const AiAction& normalized = plan.actions[0];
  if (normalized.device == "fan") {
    plan.displayLine1 = normalized.action == "on" ? "Fan ON" : "Fan OFF";
    plan.displayLine2 = normalized.action == "on"
                            ? String(normalized.mode == "natural" ? "N" : "D") + String(normalized.level) + " " +
                                  String(normalized.duty) + "%"
                            : "L0 0%";
    plan.displayLevel = normalized.action == "on" ? "warning" : "info";
    plan.reply = normalized.action == "on" ? "Web control: fan on." : "Web control: fan off.";
  } else if (normalized.device == "lamp") {
    plan.displayLine1 = normalized.action == "on" ? "Lamp ON" : "Lamp OFF";
    plan.displayLine2 = normalized.action == "on" ? "L" + String(normalized.level) + " " + String(normalized.duty) + "%"
                                                   : "L0 0%";
    plan.reply = normalized.action == "on" ? "Web control: lamp on." : "Web control: lamp off.";
  } else if (normalized.device == "curtain") {
    plan.displayLine1 = normalized.action == "open" ? "Curtain OPEN" : "Curtain CLOSE";
    plan.displayLine2 = "Curtain " + String(normalized.percent) + "%";
    plan.reply = normalized.action == "open" ? "Web control: curtain open." : "Web control: curtain close.";
  }
  return true;
}

static void makeLocalFallbackPlan(const String& userText, const EnvData& env, AiPlan& plan, const String& reason) {
  String lower = userText;
  lower.toLowerCase();

  plan = AiPlan{};
  plan.valid = true;
  plan.controlMode = "closed_loop";
  plan.confidence = 0.62f;
  plan.reason = reason;
  plan.displayTitle = "AI Advice";

  const bool asksWeather = lower.indexOf("weather") >= 0 || lower.indexOf("today") >= 0;
  const bool userReportsHeat = lower.indexOf("hot") >= 0 || lower.indexOf("heat") >= 0 ||
                               lower.indexOf("热") >= 0 || lower.indexOf("闷") >= 0;
  const bool userReportsHumidity = lower.indexOf("humid") >= 0 || lower.indexOf("humidity") >= 0 ||
                                   lower.indexOf("湿") >= 0 || lower.indexOf("潮") >= 0;
  const bool userReportsDark = lower.indexOf("dark") >= 0 || lower.indexOf("dim") >= 0 ||
                               lower.indexOf("low light") >= 0 || lower.indexOf("暗") >= 0 ||
                               lower.indexOf("黑") >= 0;
  const bool userReportsBright = lower.indexOf("bright") >= 0 || lower.indexOf("sun") >= 0 ||
                                 lower.indexOf("glare") >= 0 || lower.indexOf("太亮") >= 0 ||
                                 lower.indexOf("刺眼") >= 0 || lower.indexOf("晒") >= 0;
  const bool hot = userReportsHeat || (asksWeather && env.temp >= 28.0f);
  const bool humid = userReportsHumidity || (asksWeather && env.humi >= 70.0f);
  const bool dark = userReportsDark || (asksWeather && env.light > 0.0f && env.light < 250.0f);
  const bool bright = userReportsBright || (asksWeather && env.light > 1200.0f);

  if (bright) {
    plan.intent = "curtain_adjust";
    plan.cmd = "curtain_close";
    plan.device = "curtain";
    plan.action = "close";
    plan.curtainPercent = 0;
    plan.durationSec = 0;
    plan.displayLine1 = "Curtain CLOSE";
    plan.displayLine2 = "MCU servo";
    plan.displayLevel = "info";
    plan.reply = "Close the curtain to reduce glare and direct sunlight. Recheck room light after a few minutes.";
  } else if (hot || humid) {
    plan.intent = "climate_adjust";
    plan.cmd = "fan_on";
    plan.device = "fan";
    plan.action = "on";
    plan.fanMode = "direct";
    plan.fanLevel = 3;
    plan.fanDuty = 100;
    plan.tempTarget = 26.5f;
    plan.humiTarget = 60.0f;
    plan.durationSec = 300;
    plan.displayLine1 = "Fan ON";
    plan.displayLine2 = "T26.5 H60 fan_on";
    plan.displayLevel = "warning";
    if (asksWeather) {
      plan.reply = "No live weather API. Room sensors show high humidity or heat. Turn fan on and target 26.5C 60%RH.";
    } else {
      plan.reply = "Turn fan on now. Ventilate to lower heat and humidity. Target 26.5C and 60%RH. Check after 5 min.";
    }
  } else if (dark) {
    plan.intent = "light_adjust";
    plan.cmd = "lamp_on";
    plan.device = "lamp";
    plan.action = "on";
    plan.lampLevel = 2;
    plan.lampDuty = 60;
    plan.lightMin = 400.0f;
    plan.durationSec = 300;
    plan.displayLine1 = "Lamp ON";
    plan.displayLine2 = "L400 lamp_on";
    plan.displayLevel = "info";
    plan.reply = "Light is low. Turn lamp on and keep room light above 400 lux. Recheck sensor after a few minutes.";
  } else {
    plan.intent = "observe";
    plan.cmd = "none";
    plan.device = "none";
    plan.action = "none";
    plan.durationSec = 120;
    plan.displayLine1 = "Monitor";
    plan.displayLine2 = "No action";
    plan.displayLevel = "info";
    if (asksWeather) {
      plan.reply = "No live weather API. Use room sensors for local conditions. Current values look acceptable; keep monitoring.";
    } else {
      plan.reply = "Current room values look acceptable. Keep monitoring temperature, humidity and light before changing devices.";
    }
  }
  addSingleTopLevelAction(plan);
}

static bool makeDirectCommandPlan(const String& userText, AiPlan& plan) {
  String lower = userText;
  lower.toLowerCase();

  const bool wantsOff = lower.indexOf("off") >= 0 || lower.indexOf("close") >= 0 ||
                        lower.indexOf("shut") >= 0 || lower.indexOf("stop") >= 0 ||
                        lower.indexOf("turn of") >= 0 || lower.indexOf("ten of") >= 0 ||
                        lower.indexOf("关") >= 0 || lower.indexOf("关闭") >= 0;
  const bool wantsOn = lower.indexOf(" on") >= 0 || lower.startsWith("on ") ||
                       lower.indexOf("open") >= 0 || lower.indexOf("start") >= 0 ||
                       lower.indexOf("turn on") >= 0 ||
                       lower.indexOf("开") >= 0 || lower.indexOf("打开") >= 0;
  const bool mentionsFan = lower.indexOf("fan") >= 0 || lower.indexOf("motor") >= 0 ||
                           lower.indexOf("vent") >= 0 || lower.indexOf("five") >= 0 ||
                           lower.indexOf("风扇") >= 0 || lower.indexOf("通风") >= 0 ||
                           lower.indexOf("电机") >= 0 || lower.indexOf("马达") >= 0;
  const bool mentionsLamp = lower.indexOf("lamp") >= 0 || lower.indexOf("light") >= 0 ||
                            lower.indexOf("led") >= 0 || lower.indexOf("lights") >= 0 ||
                            lower.indexOf("灯") >= 0 || lower.indexOf("补光") >= 0 ||
                            lower.indexOf("光照") >= 0;
  const bool mentionsCurtain = lower.indexOf("curtain") >= 0 || lower.indexOf("blind") >= 0 ||
                               lower.indexOf("shade") >= 0 || lower.indexOf("window") >= 0 ||
                               lower.indexOf("窗帘") >= 0 || lower.indexOf("窗") >= 0 ||
                               lower.indexOf("帘") >= 0;
  const bool asksNaturalFan = lower.indexOf("natural") >= 0 || lower.indexOf("breeze") >= 0 ||
                              lower.indexOf("自然风") >= 0 || lower.indexOf("柔风") >= 0;
  const bool asksDirectFan = lower.indexOf("direct") >= 0 || lower.indexOf("straight") >= 0 ||
                             lower.indexOf("直吹") >= 0 || lower.indexOf("强风") >= 0;
  const bool asksLow = lower.indexOf("low") >= 0 || lower.indexOf("small") >= 0 ||
                       lower.indexOf("低") >= 0 || lower.indexOf("小") >= 0 || lower.indexOf("一点") >= 0;
  const bool asksHigh = lower.indexOf("high") >= 0 || lower.indexOf("strong") >= 0 ||
                        lower.indexOf("max") >= 0 || lower.indexOf("高") >= 0 ||
                        lower.indexOf("大") >= 0 || lower.indexOf("很") >= 0;
  const int requestedLevel = asksHigh ? 3 : (asksLow ? 1 : 2);
  const uint8_t mentionedDevices = (mentionsFan ? 1U : 0U) + (mentionsLamp ? 1U : 0U) + (mentionsCurtain ? 1U : 0U);

  const bool fanSpecificOff = lower.indexOf("fan off") >= 0 || lower.indexOf("turn off fan") >= 0 ||
                              lower.indexOf("turn off the fan") >= 0 || lower.indexOf("stop fan") >= 0 ||
                              lower.indexOf("关风扇") >= 0 || lower.indexOf("关闭风扇") >= 0 ||
                              lower.indexOf("风扇关") >= 0;
  const bool fanSpecificOn = lower.indexOf("fan on") >= 0 || lower.indexOf("turn on fan") >= 0 ||
                             lower.indexOf("turn on the fan") >= 0 || lower.indexOf("start fan") >= 0 ||
                             lower.indexOf("开风扇") >= 0 || lower.indexOf("打开风扇") >= 0 ||
                             lower.indexOf("风扇开") >= 0;
  const bool lampSpecificOff = lower.indexOf("lamp off") >= 0 || lower.indexOf("light off") >= 0 ||
                               lower.indexOf("turn off lamp") >= 0 || lower.indexOf("turn off light") >= 0 ||
                               lower.indexOf("turn off the light") >= 0 || lower.indexOf("关灯") >= 0 ||
                               lower.indexOf("关闭灯") >= 0 || lower.indexOf("灯关") >= 0;
  const bool lampSpecificOn = lower.indexOf("lamp on") >= 0 || lower.indexOf("light on") >= 0 ||
                              lower.indexOf("turn on lamp") >= 0 || lower.indexOf("turn on light") >= 0 ||
                              lower.indexOf("turn on the light") >= 0 || lower.indexOf("开灯") >= 0 ||
                              lower.indexOf("打开灯") >= 0 || lower.indexOf("灯开") >= 0;
  const bool curtainSpecificClose = lower.indexOf("curtain close") >= 0 || lower.indexOf("close curtain") >= 0 ||
                                    lower.indexOf("close the curtain") >= 0 || lower.indexOf("close window") >= 0 ||
                                    lower.indexOf("close the window") >= 0 || lower.indexOf("关窗帘") >= 0 ||
                                    lower.indexOf("关闭窗帘") >= 0 || lower.indexOf("关窗") >= 0 ||
                                    lower.indexOf("窗帘关") >= 0;
  const bool curtainSpecificOpen = lower.indexOf("curtain open") >= 0 || lower.indexOf("open curtain") >= 0 ||
                                   lower.indexOf("open the curtain") >= 0 || lower.indexOf("open window") >= 0 ||
                                   lower.indexOf("open the window") >= 0 || lower.indexOf("开窗帘") >= 0 ||
                                   lower.indexOf("打开窗帘") >= 0 || lower.indexOf("开窗") >= 0 ||
                                   lower.indexOf("窗帘开") >= 0;
  const bool fanWantsOff = mentionsFan && (fanSpecificOff || ((mentionedDevices <= 1U) && wantsOff) ||
                                           ((wantsOff && !wantsOn) && !fanSpecificOn));
  const bool fanWantsOn = mentionsFan && !fanWantsOff &&
                          (fanSpecificOn || ((mentionedDevices <= 1U) && wantsOn) ||
                           ((wantsOn && !wantsOff) && !fanSpecificOff));
  const bool lampWantsOff = mentionsLamp && (lampSpecificOff || ((mentionedDevices <= 1U) && wantsOff) ||
                                             ((wantsOff && !wantsOn) && !lampSpecificOn));
  const bool lampWantsOn = mentionsLamp && !lampWantsOff &&
                           (lampSpecificOn || ((mentionedDevices <= 1U) && wantsOn) ||
                            ((wantsOn && !wantsOff) && !lampSpecificOff));
  const bool curtainWantsClose = mentionsCurtain && (curtainSpecificClose || ((mentionedDevices <= 1U) && wantsOff) ||
                                                     ((wantsOff && !wantsOn) && !curtainSpecificOpen));
  const bool curtainWantsOpen = mentionsCurtain && !curtainWantsClose &&
                                (curtainSpecificOpen || ((mentionedDevices <= 1U) && wantsOn) ||
                                 ((wantsOn && !wantsOff) && !curtainSpecificClose));

  plan = AiPlan{};
  plan.valid = true;
  plan.intent = "direct_control";
  plan.controlMode = "direct";
  plan.confidence = 0.95f;
  plan.reason = "direct_user_command";
  plan.displayTitle = "AI Advice";
  plan.durationSec = 300;

  if (fanWantsOff) {
    AiAction action;
    action.device = "fan";
    action.action = "off";
    action.mode = "direct";
    (void)directAddAction(plan, action);
  } else if (fanWantsOn) {
    AiAction action;
    action.device = "fan";
    action.action = "on";
    action.mode = asksNaturalFan ? "natural" : (asksDirectFan ? "direct" : "direct");
    action.level = requestedLevel;
    action.duty = directLevelToFanDuty(requestedLevel);
    action.durationSec = 300;
    plan.tempTarget = 26.5f;
    plan.humiTarget = 60.0f;
    (void)directAddAction(plan, action);
  }

  if (lampWantsOff) {
    AiAction action;
    action.device = "lamp";
    action.action = "off";
    (void)directAddAction(plan, action);
  } else if (lampWantsOn) {
    AiAction action;
    action.device = "lamp";
    action.action = "on";
    action.level = requestedLevel;
    action.duty = directLevelToLampDuty(requestedLevel);
    action.durationSec = 300;
    plan.lightMin = 400.0f;
    (void)directAddAction(plan, action);
  }

  if (curtainWantsClose) {
    AiAction action;
    action.device = "curtain";
    action.action = "close";
    action.percent = 0;
    (void)directAddAction(plan, action);
  } else if (curtainWantsOpen) {
    AiAction action;
    action.device = "curtain";
    action.action = "open";
    action.percent = 100;
    (void)directAddAction(plan, action);
  }

  if (plan.actionCount == 0) {
    return false;
  }

  plan.displayLevel = (plan.fanDuty > 0) ? "warning" : "info";
  if (plan.actionCount > 1) {
    plan.displayLine1 = "Multi action";
    plan.displayLine2 = "";
    plan.reply = "Execute multiple home actions now. Keep previous states for devices not mentioned.";
    for (size_t i = 0; i < plan.actionCount; ++i) {
      if (i > 0) {
        plan.displayLine2 += "+";
      }
      if (plan.actions[i].device == "fan") {
        plan.displayLine2 += "FAN";
      } else if (plan.actions[i].device == "lamp") {
        plan.displayLine2 += "LAMP";
      } else if (plan.actions[i].device == "curtain") {
        plan.displayLine2 += "CUR";
      }
    }
  } else if (plan.cmd == "fan_on") {
    plan.displayLine1 = "Fan ON";
    plan.displayLine2 = "T26.5 H60 fan_on";
    plan.reply = "Turn fan on now. Ventilate to lower heat and humidity. Target 26.5C and 60%RH.";
  } else if (plan.cmd == "fan_off") {
    plan.displayLine1 = "Fan OFF";
    plan.displayLine2 = "direct fan_off";
    plan.reply = "Turn fan off now. Keep monitoring humidity and temperature; restart fan if values rise again.";
  } else if (plan.cmd == "lamp_on") {
    plan.displayLine1 = "Lamp ON";
    plan.displayLine2 = "L400 lamp_on";
    plan.reply = "Turn lamp on now. Keep light above 400 lux and recheck sensor after a few minutes.";
  } else if (plan.cmd == "lamp_off") {
    plan.displayLine1 = "Lamp OFF";
    plan.displayLine2 = "direct lamp_off";
    plan.reply = "Turn lamp off now. Keep monitoring lux and turn it back on if light becomes too low.";
  } else if (plan.cmd == "curtain_open") {
    plan.displayLine1 = "Curtain OPEN";
    plan.displayLine2 = "MCU servo";
    plan.reply = "Open the curtain now.";
  } else if (plan.cmd == "curtain_close") {
    plan.displayLine1 = "Curtain CLOSE";
    plan.displayLine2 = "MCU servo";
    plan.reply = "Close the curtain now.";
  }

  return true;
}

static void runAi(const String& userText) {
  EnvData env = getEnvSnapshot();
  if (!env.valid) {
    env.valid = true;
    env.temp = 30.2f;
    env.humi = 78.0f;
    env.soil = 42.0f;
    env.light = 600.0f;
    env.fan = 0;
    env.pump = 0;
    env.lamp = 0;
    setEnvSnapshot(env);
    Serial.println("[ENV] no STM32 env yet, using demo env");
  }

  showMessageSafe("AI thinking", "Please wait");
  Serial.print("[ASK] ");
  Serial.println(userText);

  if (!isValidUtf8(userText)) {
    const String error = "Serial input is not UTF-8. Set your serial tool encoding to UTF-8, or send pinyin/English.";
    Serial.print("[AI] failed: ");
    Serial.println(error);
    printUsbAiError(userText, error);
    showMessageSafe("Input error", "Use UTF-8");
    return;
  }

  String lowerText = userText;
  lowerText.toLowerCase();
  {
    AiPlan plan;
    if (makeDirectCommandPlan(userText, plan)) {
      Serial.println("[AI] direct command handled locally");
      Serial.print("[AI reply] ");
      Serial.println(plan.reply);
      printUsbAiResponse(userText, plan);
      showPlanSafe(plan);
      sendAiPlanSafe(plan);
      return;
    }
  }

  if (lowerText.indexOf("weather") >= 0 || lowerText.indexOf("today") >= 0) {
    AiPlan plan;
    makeLocalFallbackPlan(userText, env, plan, "local_sensor_weather");
    Serial.println("[AI] weather request handled by local room sensors");
    Serial.print("[AI reply] ");
    Serial.println(plan.reply);
    printUsbAiResponse(userText, plan);
    showPlanSafe(plan);
    sendAiPlanSafe(plan);
    return;
  }

#if !AI_MOCK_MODE
  if (!wifi.connected()) {
    Serial.print("[WiFi] reconnecting");
    showMessageSafe("WiFi", "Reconnecting");
    if (wifi.begin(30000)) {
      Serial.print("[WiFi] connected IP=");
      Serial.println(wifi.ip());
    } else {
      const String error = "WiFi not connected";
      Serial.println("[WiFi] reconnect failed");
      printUsbAiError(userText, error);
      showMessageSafe("WiFi failed", "Check config");
      return;
    }
  }
#endif

  AiPlan plan;
  String error;
  if (!qwen.ask(userText, env, plan, error)) {
    Serial.print("[AI] failed: ");
    Serial.println(error);
    printUsbAiError(userText, error);
    makeLocalFallbackPlan(userText, env, plan, "api_failed");
    Serial.println("[AI] using local fallback plan");
  }

  Serial.print("[AI reply] ");
  Serial.println(plan.reply);
  Serial.printf("[AI plan] intent=%s cmd=%s duration=%d confidence=%.2f reason=%s\n",
                plan.intent.c_str(),
                plan.cmd.c_str(),
                plan.durationSec,
                plan.confidence,
                plan.reason.c_str());

  printUsbAiResponse(userText, plan);
  showPlanSafe(plan);
  sendAiPlanSafe(plan);
}

static void finishVoiceRecording(const String& reason) {
  Serial.print("[VOICE] stop: ");
  Serial.println(reason);
  sendVoiceAckSafe("processing", "ASR processing");

#if !AI_MOCK_MODE
  if (!wifi.connected()) {
    Serial.println("[VOICE] reconnecting WiFi for ASR");
    if (!wifi.begin(30000)) {
      sendVoiceAckSafe("error", "WiFi failed");
      Serial.println("[VOICE] WiFi reconnect failed");
      voiceProcessing = false;
      return;
    }
  }
#endif

  String transcript;
  String error;
  if (!voice.stopAndTranscribe(transcript, error)) {
    Serial.print("[VOICE] ASR failed: ");
    Serial.println(error);
    sendVoiceAckSafe("error", error);
    voiceProcessing = false;
    return;
  }

  Serial.print("[VOICE] ASR text: ");
  Serial.println(transcript);
  sendVoiceAckSafe("idle", "ASR OK");
  vTaskDelay(pdMS_TO_TICKS(80));
  runAi(transcript);
  voiceProcessing = false;
}

static void handleVoiceCommand(const String& cmd) {
  if (cmd == "start") {
    if (voiceProcessing) {
      sendVoiceAckSafe("processing", "Voice busy");
      return;
    }
    String error;
    if (voice.beginRecording(error)) {
      Serial.println("[VOICE] recording started");
      sendVoiceAckSafe("recording", "Recording");
    } else {
      Serial.print("[VOICE] start failed: ");
      Serial.println(error);
      sendVoiceAckSafe("error", error);
    }
    return;
  }

  if (cmd == "stop") {
    if (voiceProcessing) {
      sendVoiceAckSafe("processing", "Voice busy");
      return;
    }
    if (voice.recording()) {
      voiceProcessing = true;
      if (!enqueueVoiceStop("button")) {
        voiceProcessing = false;
        sendVoiceAckSafe("error", "AI queue full");
      }
    } else {
      sendVoiceAckSafe("idle", "Not recording");
    }
    return;
  }

  sendVoiceAckSafe("error", "Bad voice cmd");
}

static void handleUsbLine(const String& line) {
  if (line == "help") {
    printHelp();
    return;
  }

  if (line == "demo") {
    parseEnvFromUsb("{\"type\":\"env\",\"temp\":30.2,\"humi\":78,\"soil\":42,\"light\":600,\"fan\":0,\"pump\":0,\"lamp\":0}");
    if (!enqueueAiText("The temperature is a bit high and the humidity is also high.")) {
      Serial.println("[AI] queue full");
    }
    return;
  }

  if (line.startsWith("env ")) {
    parseEnvFromUsb(line.substring(4));
    return;
  }

  if (line.startsWith("ask ")) {
    if (!enqueueAiText(line.substring(4))) {
      Serial.println("[AI] queue full");
    }
    return;
  }

  if (line.startsWith("ai ")) {
    if (!enqueueAiText(line.substring(3))) {
      Serial.println("[AI] queue full");
    }
    return;
  }

  if (!enqueueAiText(line)) {
    Serial.println("[AI] queue full");
  }
}

static void webSendJson(const JsonDocument& doc, int code = 200) {
  String body;
  serializeJson(doc, body);
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.send(code, "application/json", body);
}

static void handleWebRoot() {
  const char* html = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>智能家居网关</title>
  <style>
    body{margin:0;font-family:Arial,"Microsoft YaHei","PingFang SC",sans-serif;background:#101418;color:#eef3f8}
    main{max-width:760px;margin:auto;padding:18px}
    h1{font-size:24px;margin:4px 0 12px}
    section{border:1px solid #2c3742;border-radius:8px;padding:14px;margin:12px 0;background:#151c23}
    .grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
    button{min-height:46px;border:0;border-radius:7px;background:#2364aa;color:white;font-size:16px;font-weight:700}
    button.off{background:#5b6570}
    button.warn{background:#a45f12}
    input,select{box-sizing:border-box;width:100%;min-height:42px;border-radius:7px;border:1px solid #384653;background:#0d1217;color:#eef3f8;padding:0 10px;font-size:16px}
    input[type=range]{padding:0}
    label{display:block;margin:8px 0 5px;color:#a9b8c7}
    pre{white-space:pre-wrap;word-break:break-word;background:#0d1217;border-radius:7px;padding:10px;min-height:72px}
    .row{display:flex;gap:8px}
    .row input{flex:1}
    .row button{width:96px}
    .controls{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px}
    .full{grid-column:1/-1}
  </style>
</head>
<body>
<main>
  <h1>智能家居网关</h1>
  <section>
    <h2>风扇</h2>
    <div class="controls">
      <div>
        <label>风扇模式</label>
        <select id="fanMode">
          <option value="direct">直吹</option>
          <option value="natural">自然风</option>
        </select>
      </div>
      <div>
        <label>风扇档位</label>
        <select id="fanLevel">
          <option value="1">1档 40%</option>
          <option value="2" selected>2档 70%</option>
          <option value="3">3档 100%</option>
        </select>
      </div>
    </div>
    <div class="grid">
      <button onclick="fanOn()">打开风扇</button>
      <button class="off" onclick="action({device:'fan',action:'off'})">关闭风扇</button>
    </div>
  </section>
  <section>
    <h2>灯光</h2>
    <div class="controls">
      <div class="full">
        <label>灯光档位</label>
        <select id="lampLevel">
          <option value="1">1档 30%</option>
          <option value="2" selected>2档 60%</option>
          <option value="3">3档 100%</option>
        </select>
      </div>
    </div>
    <div class="grid">
      <button onclick="lampOn()">打开灯光</button>
      <button class="off" onclick="action({device:'lamp',action:'off'})">关闭灯光</button>
    </div>
  </section>
  <section>
    <h2>窗帘</h2>
    <label>窗帘开合：<span id="curtainValue">100</span>%</label>
    <input id="curtainPercent" type="range" min="0" max="100" value="100" oninput="curtainValue.textContent=this.value">
    <div class="grid" style="margin-top:10px">
      <button class="warn" onclick="curtainSet()">设置窗帘</button>
      <button class="warn" onclick="curtainClose()">关闭窗帘</button>
    </div>
  </section>
  <section>
    <h2>自然语言指令</h2>
    <div class="row">
      <input id="text" value="打开灯和风扇">
      <button onclick="sendText()">发送</button>
    </div>
  </section>
  <section>
    <h2>当前状态</h2>
    <pre id="status">正在加载...</pre>
  </section>
</main>
<script>
async function cmd(path){
  const r=await fetch(path,{method:'POST'});
  document.getElementById('status').textContent=formatStatus(await r.json());
  setTimeout(refresh,700);
}
async function action(payload){
  const r=await fetch('/api/action',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
  document.getElementById('status').textContent=formatStatus(await r.json());
  setTimeout(refresh,700);
}
function fanDuty(level){return level==1?40:(level==2?70:100)}
function lampDuty(level){return level==1?30:(level==2?60:100)}
function fanOn(){
  const level=Number(document.getElementById('fanLevel').value);
  action({device:'fan',action:'on',mode:document.getElementById('fanMode').value,level:level,duty:fanDuty(level)});
}
function lampOn(){
  const level=Number(document.getElementById('lampLevel').value);
  action({device:'lamp',action:'on',level:level,duty:lampDuty(level)});
}
function curtainSet(){
  const percent=Number(document.getElementById('curtainPercent').value);
  action({device:'curtain',action:'open',percent:percent});
}
function curtainClose(){
  document.getElementById('curtainPercent').value=0;
  document.getElementById('curtainValue').textContent='0';
  action({device:'curtain',action:'close',percent:0});
}
async function sendText(){
  const text=document.getElementById('text').value;
  const r=await fetch('/api/cmd?text='+encodeURIComponent(text),{method:'POST'});
  document.getElementById('status').textContent=formatStatus(await r.json());
  setTimeout(refresh,700);
}
async function refresh(){
  const r=await fetch('/api/status');
  document.getElementById('status').textContent=formatStatus(await r.json());
}
function yes(v){return v?'是':'否'}
function onoff(v){return Number(v)?'开':'关'}
function formatStatus(d){
  if(!d || d.ok===false){
    return '请求失败：'+(d&&d.error?d.error:'未知错误');
  }
  if(d.queued){
    return '命令已发送：'+d.text+'\n正在等待 ESP32 处理...';
  }
  const e=d.env||{};
  const dev=d.devices||{};
  const fan=dev.fan||{};
  const lamp=dev.lamp||{};
  const cur=dev.curtain||{};
  return [
    '连接状态：'+(d.wifi?'已连接':'未连接'),
    'IP 地址：'+(d.ip||'-'),
    '录音中：'+yes(d.recording),
    'AI处理中：'+yes(d.voice_processing),
    '',
    '温度：'+Number(e.temp||0).toFixed(1)+' C',
    '湿度：'+Number(e.humi||0).toFixed(1)+' %',
    '光照：'+Number(e.light||0).toFixed(0)+' lux',
    '',
    '风扇：'+(fan.on?'开':'关')+' '+(fan.mode==='natural'?'自然风':'直吹')+' L'+Number(fan.level||0)+' '+Number(fan.duty||0)+'%',
    '灯光：'+(lamp.on?'开':'关')+' L'+Number(lamp.level||0)+' '+Number(lamp.duty||0)+'%',
    '窗帘：'+(cur.known?(Number(cur.percent||0)+'% '+(cur.open?'打开':'关闭')):'未知')
  ].join('\n');
}
setInterval(refresh,2000);
refresh();
</script>
</body>
</html>
)HTML";
  webServer.send(200, "text/html; charset=utf-8", html);
}

static void handleWebStatus() {
  const EnvData env = getEnvSnapshot();
  StaticJsonDocument<768> doc;
  doc["ok"] = true;
  doc["ip"] = wifi.ip();
  doc["wifi"] = wifi.connected();
  doc["voice_processing"] = voiceProcessing;
  doc["recording"] = voice.recording();
  JsonObject e = doc.createNestedObject("env");
  e["valid"] = env.valid;
  e["temp"] = env.temp;
  e["humi"] = env.humi;
  e["soil"] = env.soil;
  e["light"] = env.light;
  e["fan"] = env.fan;
  e["pump"] = env.pump;
  e["lamp"] = env.lamp;
  JsonObject devices = doc.createNestedObject("devices");
  JsonObject fan = devices.createNestedObject("fan");
  fan["on"] = webDeviceState.fanOn;
  fan["mode"] = webDeviceState.fanMode;
  fan["level"] = webDeviceState.fanLevel;
  fan["duty"] = webDeviceState.fanDuty;
  JsonObject lamp = devices.createNestedObject("lamp");
  lamp["on"] = webDeviceState.lampOn;
  lamp["level"] = webDeviceState.lampLevel;
  lamp["duty"] = webDeviceState.lampDuty;
  JsonObject curtain = devices.createNestedObject("curtain");
  curtain["known"] = webDeviceState.curtainKnown;
  curtain["open"] = webDeviceState.curtainOpen;
  curtain["percent"] = webDeviceState.curtainPercent;
  webSendJson(doc);
}

static void handleWebActionJson(const JsonVariantConst& input) {
  StaticJsonDocument<768> resp;
  AiAction action;
  AiPlan plan;
  String error;

  if (!input.is<JsonObjectConst>()) {
    resp["ok"] = false;
    resp["error"] = "bad json";
    webSendJson(resp, 400);
    return;
  }

  JsonObjectConst obj = input.as<JsonObjectConst>();
  action.device = obj["device"] | "none";
  action.action = obj["action"] | "none";
  action.mode = obj["mode"] | "direct";
  action.level = obj["level"] | 0;
  action.duty = obj["duty"] | 0;
  action.percent = obj["percent"] | -1;
  action.durationSec = obj["duration_sec"] | 300;

  if (!makeStructuredActionPlan(action, plan, error)) {
    resp["ok"] = false;
    resp["error"] = error;
    webSendJson(resp, 400);
    return;
  }

  const AiAction& normalized = plan.actions[0];
  updateWebDeviceStateFromAction(normalized);
  showPlanSafe(plan);
  sendAiPlanSafe(plan);
  printUsbAiResponse("web structured action", plan);

  resp["ok"] = true;
  resp["cmd"] = plan.cmd;
  resp["queued"] = false;
  JsonObject out = resp.createNestedObject("action");
  out["device"] = normalized.device;
  out["action"] = normalized.action;
  out["mode"] = normalized.mode;
  out["level"] = normalized.level;
  out["duty"] = normalized.duty;
  if (normalized.percent >= 0) {
    out["percent"] = normalized.percent;
  }
  JsonObject devices = resp.createNestedObject("devices");
  JsonObject fan = devices.createNestedObject("fan");
  fan["on"] = webDeviceState.fanOn;
  fan["mode"] = webDeviceState.fanMode;
  fan["level"] = webDeviceState.fanLevel;
  fan["duty"] = webDeviceState.fanDuty;
  JsonObject lamp = devices.createNestedObject("lamp");
  lamp["on"] = webDeviceState.lampOn;
  lamp["level"] = webDeviceState.lampLevel;
  lamp["duty"] = webDeviceState.lampDuty;
  JsonObject curtain = devices.createNestedObject("curtain");
  curtain["known"] = webDeviceState.curtainKnown;
  curtain["open"] = webDeviceState.curtainOpen;
  curtain["percent"] = webDeviceState.curtainPercent;

  Serial.print("[WEB_ACTION] ");
  serializeJson(out, Serial);
  Serial.println();
  webSendJson(resp);
}

static void handleWebAction() {
  StaticJsonDocument<512> req;
  const String body = webServer.arg("plain");
  DeserializationError err = deserializeJson(req, body);
  if (err) {
    StaticJsonDocument<192> resp;
    resp["ok"] = false;
    resp["error"] = String("json parse failed: ") + err.c_str();
    webSendJson(resp, 400);
    return;
  }
  handleWebActionJson(req.as<JsonVariantConst>());
}

static void handleWebCommandText(const String& text) {
  StaticJsonDocument<256> doc;
  String command = text;
  command.trim();
  if (command.length() == 0) {
    doc["ok"] = false;
    doc["error"] = "missing command";
    webSendJson(doc, 400);
    return;
  }

  const bool queued = enqueueAiText(command);
  doc["ok"] = queued;
  doc["queued"] = queued;
  doc["text"] = command;
  if (!queued) {
    doc["error"] = "AI queue full";
    webSendJson(doc, 503);
    return;
  }
  Serial.print("[WEB] ");
  Serial.println(command);
  webSendJson(doc);
}

static void handleWebCommand() {
  handleWebCommandText(webServer.arg("text"));
}

static void handleWebShortcutAction(const char* device, const char* action) {
  StaticJsonDocument<256> doc;
  doc["device"] = device;
  doc["action"] = action;
  if (strcmp(device, "fan") == 0 && strcmp(action, "on") == 0) {
    doc["mode"] = "direct";
    doc["level"] = 2;
    doc["duty"] = 70;
  } else if (strcmp(device, "lamp") == 0 && strcmp(action, "on") == 0) {
    doc["level"] = 2;
    doc["duty"] = 60;
  } else if (strcmp(device, "curtain") == 0) {
    doc["percent"] = (strcmp(action, "open") == 0) ? 100 : 0;
  }
  handleWebActionJson(doc.as<JsonVariantConst>());
}

static void handleWebNotFound() {
  StaticJsonDocument<192> doc;
  doc["ok"] = false;
  doc["error"] = "not found";
  doc["uri"] = webServer.uri();
  webSendJson(doc, 404);
}

static void configureWebServer() {
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/api/status", HTTP_GET, handleWebStatus);
  webServer.on("/api/cmd", handleWebCommand);
  webServer.on("/api/action", HTTP_POST, handleWebAction);
  webServer.on("/fan/on", []() { handleWebShortcutAction("fan", "on"); });
  webServer.on("/fan/off", []() { handleWebShortcutAction("fan", "off"); });
  webServer.on("/lamp/on", []() { handleWebShortcutAction("lamp", "on"); });
  webServer.on("/lamp/off", []() { handleWebShortcutAction("lamp", "off"); });
  webServer.on("/curtain/open", []() { handleWebShortcutAction("curtain", "open"); });
  webServer.on("/curtain/close", []() { handleWebShortcutAction("curtain", "close"); });
  webServer.onNotFound(handleWebNotFound);
}

static void startWebServerIfNeeded() {
  if (webServerStarted || !wifi.connected()) {
    return;
  }
  configureWebServer();
  webServer.begin();
  webServerStarted = true;

  if (!mdnsStarted) {
    if (MDNS.begin(wifi.hostname())) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.print("[MDNS] http://");
      Serial.print(wifi.hostname());
      Serial.println(".local/");
    } else {
      Serial.println("[MDNS] start failed");
    }
  }

  Serial.print("[WEB] http://");
  Serial.println(wifi.ip());
  Serial.print("[WEB] http://");
  Serial.print(wifi.hostname());
  Serial.println(".local/");
}

static void aiWorkerTask(void*) {
  AiJob job;
  Serial.printf("[TASK] AiWorker running on core %d\n", xPortGetCoreID());

  while (true) {
    if (xQueueReceive(aiQueue, &job, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (job.type == AiJobType::VoiceStop) {
      finishVoiceRecording(String(job.text));
    } else {
      runAi(String(job.text));
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("ESPAI1 booting...");

  envMutex = xSemaphoreCreateMutex();
  uiMutex = xSemaphoreCreateMutex();
  uartTxMutex = xSemaphoreCreateMutex();
  aiQueue = xQueueCreate(4, sizeof(AiJob));
  if (envMutex == nullptr || uiMutex == nullptr || uartTxMutex == nullptr || aiQueue == nullptr) {
    Serial.println("[TASK] RTOS init failed");
  }

  screen.begin();
  screen.showBoot("Booting");
  stm32.begin();
  voice.begin();

#if AI_MOCK_MODE
  Serial.println("[AI] AI_MOCK_MODE=1, network is not required");
  screen.showBoot("Mock AI mode");
#else
  Serial.print("[WiFi] connecting");
  if (wifi.begin(30000)) {
    Serial.print("[WiFi] connected IP=");
    Serial.println(wifi.ip());
    screen.showBoot("WiFi OK");
    startWebServerIfNeeded();
  } else {
    Serial.println("[WiFi] failed");
    screen.showBoot("WiFi failed");
  }
#endif

  BaseType_t taskOk = xTaskCreatePinnedToCore(aiWorkerTask,
                                             "AiWorker",
                                             24576,
                                             nullptr,
                                             1,
                                             nullptr,
                                             1);
  if (taskOk != pdPASS) {
    Serial.println("[TASK] AiWorker create failed");
  }

  sendStatusSafe("ready");
  printHelp();
}

void loop() {
  if (wifi.connected()) {
    startWebServerIfNeeded();
    webServer.handleClient();
    updateHeaderInfo();
  }

  EnvData incoming;
  if (stm32.pollEnv(incoming)) {
    setEnvSnapshot(incoming);
    updateWebDeviceStateFromEnv(incoming);
    Serial.println("[UART<-STM32] env updated");
    showEnvSafe(incoming);
  }

  String voiceCmd;
  if (stm32.pollVoiceCommand(voiceCmd)) {
    Serial.print("[UART<-STM32] voice ");
    Serial.println(voiceCmd);
    handleVoiceCommand(voiceCmd);
  }

  if (!voiceProcessing && voice.updateRecording()) {
    voiceProcessing = true;
    if (!enqueueVoiceStop("max duration")) {
      voiceProcessing = false;
      sendVoiceAckSafe("error", "AI queue full");
    }
  }

  String line;
  if (voice.pollUsbCommand(line)) {
    handleUsbLine(line);
  }
}
