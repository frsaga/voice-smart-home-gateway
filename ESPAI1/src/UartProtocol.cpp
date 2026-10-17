#include "UartProtocol.h"

#include <string.h>

#include <ArduinoJson.h>

#include "AppConfig.h"

static String asciiOnly(const String& value, const String& fallback, size_t maxLen) {
  String out;
  for (size_t i = 0; i < value.length() && out.length() < maxLen; ++i) {
    const char c = value[i];
    if (c >= 32 && c <= 126) {
      out += c;
    }
  }
  out.trim();
  if (out.length() == 0) {
    out = fallback;
  }
  if (out.length() > maxLen) {
    out = out.substring(0, maxLen);
  }
  return out;
}

static void buildStm32Display(const AiPlan& plan, String& title, String& line1, String& line2, String& level) {
  title = "AI Advice";
  level = asciiOnly(plan.displayLevel, "info", 12);

  if (plan.cmd == "multi" && plan.actionCount > 1) {
    line1 = "Multi action";
  } else if (plan.cmd == "fan_on") {
    line1 = "Fan ON";
  } else if (plan.cmd == "fan_off") {
    line1 = "Fan OFF";
  } else if (plan.cmd == "lamp_on") {
    line1 = "Lamp ON";
  } else if (plan.cmd == "lamp_off") {
    line1 = "Lamp OFF";
  } else if (plan.cmd == "curtain_open") {
    line1 = "Curtain OPEN";
  } else if (plan.cmd == "curtain_close") {
    line1 = "Curtain CLOSE";
  } else if (plan.cmd == "pump_on" || plan.cmd == "pump_off") {
    line1 = "Pump ignored";
  } else {
    line1 = asciiOnly(plan.displayLine1, "AI plan ready", 32);
  }

  if (plan.cmd == "multi" && plan.actionCount > 1) {
    line2 = "";
    for (size_t i = 0; i < plan.actionCount; ++i) {
      if (i > 0) {
        line2 += "+";
      }
      if (plan.actions[i].device == "fan") {
        line2 += "FAN";
      } else if (plan.actions[i].device == "lamp") {
        line2 += "LAMP";
      } else if (plan.actions[i].device == "curtain") {
        line2 += "CUR";
      } else {
        line2 += asciiOnly(plan.actions[i].device, "DEV", 4);
      }
    }
  } else if (!isnan(plan.tempTarget) && !isnan(plan.humiTarget)) {
    line2 = "T";
    line2 += String(plan.tempTarget, 1);
    line2 += " H";
    line2 += String(plan.humiTarget, 0);
    line2 += " ";
    line2 += asciiOnly(plan.cmd, "none", 12);
  } else if (!isnan(plan.tempTarget)) {
    line2 = "T";
    line2 += String(plan.tempTarget, 1);
    line2 += " ";
    line2 += asciiOnly(plan.cmd, "none", 12);
  } else if (!isnan(plan.humiTarget)) {
    line2 = "H";
    line2 += String(plan.humiTarget, 0);
    line2 += " ";
    line2 += asciiOnly(plan.cmd, "none", 12);
  } else if (!isnan(plan.lightMin)) {
    line2 = "Light > ";
    line2 += String(plan.lightMin, 0);
    line2 += "lux";
  } else if (plan.cmd == "curtain_open" || plan.cmd == "curtain_close") {
    line2 = "Curtain ";
    line2 += String((plan.curtainPercent >= 0) ? plan.curtainPercent :
                    ((plan.cmd == "curtain_open") ? 100 : 0));
    line2 += "%";
  } else if (plan.cmd == "pump_on" || plan.cmd == "pump_off") {
    line2 = "No pump on E53";
  } else {
    line2 = asciiOnly(plan.displayLine2, "Check sensors", 32);
  }
}

static String buildStm32Advice(const AiPlan& plan, const String& line1, const String& line2) {
  if (plan.cmd == "multi" && plan.actionCount > 1) {
    return "Execute multiple home actions now. Keep unchanged devices in their previous state.";
  }
  if (plan.cmd == "fan_on") {
    return "Turn fan on now. Ventilate to lower heat and humidity. Target 26.5C and 60%RH. Check after 5 min.";
  }
  if (plan.cmd == "fan_off") {
    return "Fan can stay off. Keep monitoring temperature and humidity. Restart fan if heat or humidity rises.";
  }
  if (plan.cmd == "lamp_on") {
    return "Turn lamp on. Raise room light. Keep target light range and recheck sensor soon.";
  }
  if (plan.cmd == "lamp_off") {
    return "Lamp can stay off. Current light is enough. Keep watching lux changes.";
  }
  if (plan.cmd == "curtain_open") {
    return "Curtain opened by MCU servo. Let in more daylight and recheck room light level soon.";
  }
  if (plan.cmd == "curtain_close") {
    return "Curtain closed by MCU servo. Reduce glare or sunlight and recheck room light soon.";
  }
  if (plan.cmd == "pump_on" || plan.cmd == "pump_off") {
    return "Pump command is ignored on this board. Use fan and lamp only, then check soil manually.";
  }

  String fallback = asciiOnly(plan.reply, "", 112);
  if (fallback.length() > 0) {
    return fallback;
  }
  fallback = line1;
  fallback += ". ";
  fallback += line2;
  fallback += ". Keep monitoring sensors.";
  return asciiOnly(fallback, "Keep monitoring sensors.", 112);
}

void UartProtocol::begin() {
  Serial2.begin(MCU_UART_BAUD, SERIAL_8N1, MCU_UART_RX_PIN, MCU_UART_TX_PIN);
}

bool UartProtocol::pollEnv(EnvData& env) {
  while (Serial2.available() > 0) {
    const char c = static_cast<char>(Serial2.read());
    if (c == '\n') {
      const String line = rxLine_;
      rxLine_ = "";
      if (parseEnvJson(line, env)) {
        return true;
      }
      continue;
    }
    if (c != '\r' && rxLine_.length() < 512) {
      rxLine_ += c;
    } else if (rxLine_.length() >= 512) {
      rxLine_ = "";
    }
  }
  return false;
}

bool UartProtocol::pollVoiceCommand(String& cmd) {
  if (pendingVoiceCmd_.length() == 0) {
    return false;
  }

  cmd = pendingVoiceCmd_;
  pendingVoiceCmd_ = "";
  return true;
}

void UartProtocol::sendAiPlan(const AiPlan& plan) {
  String displayTitle;
  String displayLine1;
  String displayLine2;
  String displayLevel;
  buildStm32Display(plan, displayTitle, displayLine1, displayLine2, displayLevel);
  const String advice = buildStm32Advice(plan, displayLine1, displayLine2);

  StaticJsonDocument<2048> doc;
  doc["type"] = "ai_plan";
  doc["valid"] = plan.valid;
  doc["reply"] = advice;
  doc["cmd"] = asciiOnly(plan.cmd, "none", 16);
  doc["device"] = asciiOnly(plan.device, "none", 16);
  doc["action"] = asciiOnly(plan.action, "none", 16);
  doc["duration_sec"] = plan.durationSec;
  doc["reason"] = asciiOnly(plan.reason, "ai_plan", 24);
  doc["fan_mode"] = asciiOnly(plan.fanMode, "direct", 12);
  doc["fan_level"] = plan.fanLevel;
  doc["fan_duty"] = plan.fanDuty;
  doc["lamp_level"] = plan.lampLevel;
  doc["lamp_duty"] = plan.lampDuty;
  if (plan.curtainPercent >= 0) {
    doc["curtain_percent"] = plan.curtainPercent;
  }

  JsonObject display = doc.createNestedObject("display");
  display["title"] = displayTitle;
  display["line1"] = displayLine1;
  display["line2"] = displayLine2;
  display["level"] = displayLevel;

  if (plan.actionCount > 0) {
    JsonArray actions = doc.createNestedArray("actions");
    for (size_t i = 0; i < plan.actionCount; ++i) {
      JsonObject action = actions.createNestedObject();
      action["device"] = asciiOnly(plan.actions[i].device, "none", 12);
      action["action"] = asciiOnly(plan.actions[i].action, "none", 12);
      action["duration_sec"] = plan.actions[i].durationSec;
      if (plan.actions[i].device == "fan") {
        action["mode"] = asciiOnly(plan.actions[i].mode, "direct", 12);
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

  serializeJson(doc, Serial2);
  Serial2.print('\n');

  Serial.print("[UART->STM32] ");
  serializeJson(doc, Serial);
  Serial.println();
}

void UartProtocol::sendStatus(const String& status) {
  StaticJsonDocument<128> doc;
  doc["type"] = "esp_status";
  doc["status"] = status;
  serializeJson(doc, Serial2);
  Serial2.print('\n');
}

void UartProtocol::sendVoiceAck(const String& state, const String& text) {
  StaticJsonDocument<192> doc;
  doc["type"] = "voice_ack";
  doc["state"] = asciiOnly(state, "unknown", 16);
  doc["text"] = asciiOnly(text, state, 64);
  serializeJson(doc, Serial2);
  Serial2.print('\n');

  Serial.print("[UART->STM32] ");
  serializeJson(doc, Serial);
  Serial.println();
}

void UartProtocol::sendHeaderInfo(const String& timeText, const String& weatherText) {
  StaticJsonDocument<192> doc;
  doc["type"] = "header_info";
  doc["time"] = asciiOnly(timeText, "--:--:--", 28);
  doc["weather"] = asciiOnly(weatherText, "Nanchang weather --", 36);
  serializeJson(doc, Serial2);
  Serial2.print('\n');
}

bool UartProtocol::parseEnvJson(const String& line, EnvData& env) {
  String trimmed = line;
  trimmed.trim();
  if (trimmed.length() == 0 || trimmed[0] != '{') {
    return false;
  }

  StaticJsonDocument<512> doc;
  const DeserializationError err = deserializeJson(doc, trimmed);
  if (err) {
    return false;
  }

  const char* type = doc["type"] | "";
  if (strcmp(type, "voice_cmd") == 0) {
    pendingVoiceCmd_ = doc["cmd"] | "";
    pendingVoiceCmd_.trim();
    return false;
  }

  if (strcmp(type, "env") != 0) {
    return false;
  }

  env.valid = true;
  env.temp = doc["temp"] | env.temp;
  env.humi = doc["humi"] | env.humi;
  env.soil = doc["soil"] | env.soil;
  env.light = doc["light"] | env.light;
  env.fan = doc["fan"] | env.fan;
  env.pump = doc["pump"] | env.pump;
  env.lamp = doc["lamp"] | env.lamp;
  return true;
}
