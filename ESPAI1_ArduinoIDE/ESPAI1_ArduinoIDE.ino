#include <Arduino.h>
#include <ArduinoJson.h>

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
static EnvData currentEnv;

static void printHelp() {
  Serial.println();
  Serial.println("ESPAI1 commands:");
  Serial.println("  help");
  Serial.println("  demo");
  Serial.println("  ai <user natural language>");
  Serial.println("  ask <user natural language>");
  Serial.println("  <plain text question>");
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
  StaticJsonDocument<1536> doc;
  doc["type"] = "usb_ai_response";
  doc["user_text"] = userText;
  doc["reply"] = plan.reply;
  doc["intent"] = plan.intent;
  doc["cmd"] = plan.cmd;
  doc["device"] = plan.device;
  doc["action"] = plan.action;
  doc["duration_sec"] = plan.durationSec;
  doc["control_mode"] = plan.controlMode;
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

  currentEnv.valid = true;
  currentEnv.temp = doc["temp"] | currentEnv.temp;
  currentEnv.humi = doc["humi"] | currentEnv.humi;
  currentEnv.soil = doc["soil"] | currentEnv.soil;
  currentEnv.light = doc["light"] | currentEnv.light;
  currentEnv.fan = doc["fan"] | currentEnv.fan;
  currentEnv.pump = doc["pump"] | currentEnv.pump;
  currentEnv.lamp = doc["lamp"] | currentEnv.lamp;

  Serial.printf("[ENV] T=%.1f H=%.0f Soil=%.0f Light=%.0f F%d P%d L%d\n",
                currentEnv.temp,
                currentEnv.humi,
                currentEnv.soil,
                currentEnv.light,
                currentEnv.fan,
                currentEnv.pump,
                currentEnv.lamp);
  screen.showEnv(currentEnv);
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
  const bool hot = env.temp >= 28.0f || lower.indexOf("hot") >= 0 || lower.indexOf("heat") >= 0;
  const bool humid = env.humi >= 70.0f || lower.indexOf("humid") >= 0 || lower.indexOf("humidity") >= 0;
  const bool dark = env.light > 0.0f && env.light < 250.0f;

  if (hot || humid) {
    plan.intent = "climate_adjust";
    plan.cmd = "fan_on";
    plan.device = "fan";
    plan.action = "on";
    plan.tempTarget = 26.5f;
    plan.humiTarget = 60.0f;
    plan.durationSec = 300;
    plan.displayLine1 = "Fan ON";
    plan.displayLine2 = "T26.5 H60 fan_on";
    plan.displayLevel = "warning";
    if (asksWeather) {
      plan.reply = "No live weather API. Farm sensors show high humidity or heat. Turn fan on and target 26.5C 60%RH.";
    } else {
      plan.reply = "Turn fan on now. Ventilate to lower heat and humidity. Target 26.5C and 60%RH. Check after 5 min.";
    }
  } else if (dark) {
    plan.intent = "light_adjust";
    plan.cmd = "lamp_on";
    plan.device = "lamp";
    plan.action = "on";
    plan.lightMin = 400.0f;
    plan.durationSec = 300;
    plan.displayLine1 = "Lamp ON";
    plan.displayLine2 = "L400 lamp_on";
    plan.displayLevel = "info";
    plan.reply = "Light is low. Turn lamp on and keep crop light above 400 lux. Recheck sensor after a few minutes.";
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
      plan.reply = "No live weather API. Use farm sensors for local conditions. Current values look acceptable; keep monitoring.";
    } else {
      plan.reply = "Current farm values look acceptable. Keep monitoring temperature, humidity and light before changing devices.";
    }
  }
}

static bool makeDirectCommandPlan(const String& userText, AiPlan& plan) {
  String lower = userText;
  lower.toLowerCase();

  plan = AiPlan{};
  plan.valid = true;
  plan.intent = "direct_control";
  plan.controlMode = "direct";
  plan.confidence = 0.95f;
  plan.reason = "direct_user_command";
  plan.displayTitle = "AI Advice";
  plan.durationSec = 300;

  if ((lower.indexOf("fan") >= 0 || lower.indexOf("motor") >= 0) && lower.indexOf("off") >= 0) {
    plan.cmd = "fan_off";
    plan.device = "fan";
    plan.action = "off";
    plan.displayLine1 = "Fan OFF";
    plan.displayLine2 = "direct fan_off";
    plan.displayLevel = "info";
    plan.reply = "Turn fan off now. Keep monitoring humidity and temperature; restart fan if values rise again.";
    return true;
  }

  if ((lower.indexOf("fan") >= 0 || lower.indexOf("motor") >= 0) && lower.indexOf("on") >= 0) {
    plan.cmd = "fan_on";
    plan.device = "fan";
    plan.action = "on";
    plan.tempTarget = 26.5f;
    plan.humiTarget = 60.0f;
    plan.displayLine1 = "Fan ON";
    plan.displayLine2 = "T26.5 H60 fan_on";
    plan.displayLevel = "warning";
    plan.reply = "Turn fan on now. Ventilate to lower heat and humidity. Target 26.5C and 60%RH.";
    return true;
  }

  if ((lower.indexOf("lamp") >= 0 || lower.indexOf("light") >= 0 || lower.indexOf("led") >= 0) &&
      lower.indexOf("off") >= 0) {
    plan.cmd = "lamp_off";
    plan.device = "lamp";
    plan.action = "off";
    plan.displayLine1 = "Lamp OFF";
    plan.displayLine2 = "direct lamp_off";
    plan.displayLevel = "info";
    plan.reply = "Turn lamp off now. Keep monitoring lux and turn it back on if light becomes too low.";
    return true;
  }

  if ((lower.indexOf("lamp") >= 0 || lower.indexOf("light") >= 0 || lower.indexOf("led") >= 0) &&
      lower.indexOf("on") >= 0) {
    plan.cmd = "lamp_on";
    plan.device = "lamp";
    plan.action = "on";
    plan.lightMin = 400.0f;
    plan.displayLine1 = "Lamp ON";
    plan.displayLine2 = "L400 lamp_on";
    plan.displayLevel = "info";
    plan.reply = "Turn lamp on now. Keep light above 400 lux and recheck sensor after a few minutes.";
    return true;
  }

  return false;
}

static void runAi(const String& userText) {
  if (!currentEnv.valid) {
    currentEnv.valid = true;
    currentEnv.temp = 30.2f;
    currentEnv.humi = 78.0f;
    currentEnv.soil = 42.0f;
    currentEnv.light = 600.0f;
    currentEnv.fan = 0;
    currentEnv.pump = 0;
    currentEnv.lamp = 0;
    Serial.println("[ENV] no STM32 env yet, using demo env");
  }

  screen.showMessage("AI thinking", "Please wait");
  Serial.print("[ASK] ");
  Serial.println(userText);

  if (!isValidUtf8(userText)) {
    const String error = "Serial input is not UTF-8. Set your serial tool encoding to UTF-8, or send pinyin/English.";
    Serial.print("[AI] failed: ");
    Serial.println(error);
    printUsbAiError(userText, error);
    screen.showMessage("Input error", "Use UTF-8");
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
      screen.showPlan(plan);
      stm32.sendAiPlan(plan);
      return;
    }
  }

  if (lowerText.indexOf("weather") >= 0 || lowerText.indexOf("today") >= 0) {
    AiPlan plan;
    makeLocalFallbackPlan(userText, currentEnv, plan, "local_sensor_weather");
    Serial.println("[AI] weather request handled by local farm sensors");
    Serial.print("[AI reply] ");
    Serial.println(plan.reply);
    printUsbAiResponse(userText, plan);
    screen.showPlan(plan);
    stm32.sendAiPlan(plan);
    return;
  }

#if !AI_MOCK_MODE
  if (!wifi.connected()) {
    Serial.print("[WiFi] reconnecting");
    screen.showMessage("WiFi", "Reconnecting");
    if (wifi.begin(30000)) {
      Serial.print("[WiFi] connected IP=");
      Serial.println(wifi.ip());
    } else {
      const String error = "WiFi not connected";
      Serial.println("[WiFi] reconnect failed");
      printUsbAiError(userText, error);
      screen.showMessage("WiFi failed", "Check config");
      return;
    }
  }
#endif

  AiPlan plan;
  String error;
  if (!qwen.ask(userText, currentEnv, plan, error)) {
    Serial.print("[AI] failed: ");
    Serial.println(error);
    printUsbAiError(userText, error);
    makeLocalFallbackPlan(userText, currentEnv, plan, "api_failed");
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
  screen.showPlan(plan);
  stm32.sendAiPlan(plan);
}

static void handleUsbLine(const String& line) {
  if (line == "help") {
    printHelp();
    return;
  }

  if (line == "demo") {
    parseEnvFromUsb("{\"type\":\"env\",\"temp\":30.2,\"humi\":78,\"soil\":42,\"light\":600,\"fan\":0,\"pump\":0,\"lamp\":0}");
    runAi("The temperature is a bit high and the humidity is also high.");
    return;
  }

  if (line.startsWith("env ")) {
    parseEnvFromUsb(line.substring(4));
    return;
  }

  if (line.startsWith("ask ")) {
    runAi(line.substring(4));
    return;
  }

  if (line.startsWith("ai ")) {
    runAi(line.substring(3));
    return;
  }

  runAi(line);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("ESPAI1 booting...");

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
  } else {
    Serial.println("[WiFi] failed");
    screen.showBoot("WiFi failed");
  }
#endif

  stm32.sendStatus("ready");
  printHelp();
}

void loop() {
  EnvData incoming;
  if (stm32.pollEnv(incoming)) {
    currentEnv = incoming;
    Serial.println("[UART<-STM32] env updated");
    screen.showEnv(currentEnv);
  }

  String line;
  if (voice.pollUsbCommand(line)) {
    handleUsbLine(line);
  }
}
