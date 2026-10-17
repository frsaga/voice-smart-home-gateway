#include "QwenClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "AppConfig.h"

static const char* kSystemPrompt =
    "You are a smart home gateway control planner. "
    "Input contains user_text, current environment, devices and safety limits. "
    "Do not directly control hardware. Convert the user's natural language into "
    "target parameters and executable suggested actions. "
    "Return JSON only with fields: reply, display, intent, cmd, targets, actions, safety, "
    "control_mode, fan_mode, fan_level, fan_duty, lamp_level, lamp_duty, curtain_percent, confidence, reason. "
    "Do not write weather-report style text. The reply must be an operation suggestion for the home gateway. "
    "Example JSON: {\"reply\":\"Turn fan on high direct mode. Target 26.5C and 60%RH.\",\"intent\":\"climate_adjust\",\"cmd\":\"fan_on\","
    "\"display\":{\"title\":\"AI\",\"line1\":\"Fan ON suggested\",\"line2\":\"Target 26.5C 60%RH\",\"level\":\"warning\"},"
    "\"targets\":{\"temp\":26.5,\"humi\":60},"
    "\"actions\":[{\"device\":\"fan\",\"action\":\"on\",\"duration_sec\":180,\"mode\":\"direct\",\"level\":3,\"duty\":100}],"
    "\"safety\":{\"require_sensor_valid\":true},\"control_mode\":\"closed_loop\",\"fan_mode\":\"direct\","
    "\"fan_level\":3,\"fan_duty\":100,\"lamp_level\":0,\"lamp_duty\":0,\"curtain_percent\":-1,"
    "\"confidence\":0.8,\"reason\":\"temperature_and_humidity_high\"}. "
    "Use short ASCII English for reply, reason, display.title, display.line1 and display.line2. "
    "Keep reply under 120 ASCII characters so it fits a small LCD. "
    "The display.title, display.line1 and display.line2 fields must use ASCII only, "
    "because the STM32 LCD font cannot render UTF-8 Chinese yet. "
    "Allowed cmd values are none, multi, fan_on, fan_off, pump_on, pump_off, lamp_on, lamp_off, curtain_open, curtain_close. "
    "When one user request needs more than one device, set cmd to multi and include every device in actions. "
    "Examples: 'hot and dark' -> fan on plus lamp on; 'too sunny and hot' -> curtain close plus fan on; "
    "'turn on light and fan' -> lamp on plus fan on. "
    "Use device curtain with action open or close for curtain commands. "
    "Duty levels: level 1 low, level 2 medium, level 3 high. Fan duty must be 40, 70 or 100. Lamp duty must be 30, 60 or 100. "
    "Fan mode must be direct or natural. Use natural for gentle breeze or comfort, direct for strong cooling. "
    "Curtain percent must be 0 closed, 50 half, or 100 open. "
    "Rules for vague user needs: hot, warm, heat, 闷, 热, 太热, 温度高 -> cmd fan_on, level 3 if very hot else 2. "
    "humid, humidity, 潮, 湿, 湿度大 -> cmd fan_on, fan_mode direct, level 2 or 3. "
    "dark, dim, 光线暗, 太暗 -> cmd lamp_on, lamp_level 2 or 3. "
    "bright, glare, sun, 太亮, 刺眼, 晒 -> cmd curtain_close, curtain_percent 0. "
    "Return cmd none only when no useful device action is needed.";

static int levelToFanDuty(int level) {
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

static int levelToLampDuty(int level) {
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

static String cmdFromAction(const AiAction& action) {
  if (action.device == "fan" && action.action == "on") {
    return "fan_on";
  }
  if (action.device == "fan" && action.action == "off") {
    return "fan_off";
  }
  if (action.device == "pump" && action.action == "on") {
    return "pump_on";
  }
  if (action.device == "pump" && action.action == "off") {
    return "pump_off";
  }
  if (action.device == "lamp" && action.action == "on") {
    return "lamp_on";
  }
  if (action.device == "lamp" && action.action == "off") {
    return "lamp_off";
  }
  if (action.device == "curtain" && (action.action == "open" || action.action == "on")) {
    return "curtain_open";
  }
  if (action.device == "curtain" && (action.action == "close" || action.action == "off")) {
    return "curtain_close";
  }
  return "none";
}

static bool addAction(AiPlan& plan, const AiAction& input) {
  if (plan.actionCount >= AiPlan::kMaxActions) {
    return false;
  }

  AiAction action = input;
  if (action.durationSec <= 0) {
    action.durationSec = plan.durationSec;
  }
  if (action.device == "fan") {
    if (action.mode.length() == 0) {
      action.mode = plan.fanMode;
    }
    if (action.action == "on") {
      if (action.level <= 0) {
        action.level = (plan.fanLevel > 0) ? plan.fanLevel : 2;
      }
      if (action.duty <= 0) {
        action.duty = levelToFanDuty(action.level);
      }
    } else if (action.action == "off") {
      action.level = 0;
      action.duty = 0;
    }
  } else if (action.device == "lamp") {
    if (action.action == "on") {
      if (action.level <= 0) {
        action.level = (plan.lampLevel > 0) ? plan.lampLevel : 2;
      }
      if (action.duty <= 0) {
        action.duty = levelToLampDuty(action.level);
      }
    } else if (action.action == "off") {
      action.level = 0;
      action.duty = 0;
    }
  } else if (action.device == "curtain") {
    if (action.percent < 0) {
      action.percent = (action.action == "open" || action.action == "on") ? 100 : 0;
    }
  }

  plan.actions[plan.actionCount++] = action;
  if (plan.actionCount == 1) {
    plan.device = action.device;
    plan.action = action.action;
    plan.durationSec = action.durationSec;
    plan.cmd = cmdFromAction(action);
  } else {
    plan.cmd = "multi";
    plan.device = "multi";
    plan.action = "multi";
  }

  if (action.device == "fan") {
    plan.fanMode = action.mode;
    plan.fanLevel = action.level;
    plan.fanDuty = action.duty;
  } else if (action.device == "lamp") {
    plan.lampLevel = action.level;
    plan.lampDuty = action.duty;
  } else if (action.device == "curtain") {
    plan.curtainPercent = action.percent;
  }
  return true;
}

static void syncSingleActionFromTopLevel(AiPlan& plan) {
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
  } else if (plan.cmd == "pump_on") {
    action.device = "pump";
    action.action = "on";
  } else if (plan.cmd == "pump_off") {
    action.device = "pump";
    action.action = "off";
  } else {
    return;
  }
  (void)addAction(plan, action);
}

bool QwenClient::ask(const String& userText, const EnvData& env, AiPlan& plan, String& error) {
#if AI_MOCK_MODE
  (void)error;
  return askMock(userText, env, plan);
#else
  return askHttp(userText, env, plan, error);
#endif
}

bool QwenClient::askMock(const String& userText, const EnvData& env, AiPlan& plan) {
  plan = AiPlan{};
  plan.valid = true;
  plan.intent = "climate_adjust";
  plan.controlMode = "closed_loop";
  plan.confidence = 0.78f;
  plan.reason = "mock_rule";
  plan.reply = "AI suggests target parameters based on current room data.";
  plan.displayTitle = "AI";

  const bool hot = env.temp >= 29.0f || userText.indexOf("hot") >= 0 || userText.indexOf("high temp") >= 0 ||
                   userText.indexOf("wen") >= 0 || userText.indexOf("温度") >= 0 || userText.indexOf("热") >= 0;
  const bool humid = env.humi >= 70.0f || userText.indexOf("humid") >= 0 || userText.indexOf("high humidity") >= 0 ||
                     userText.indexOf("shi") >= 0 || userText.indexOf("湿度") >= 0 || userText.indexOf("闷") >= 0;
  const bool drySoil = (env.soil > 0.0f && env.soil < 35.0f) || userText.indexOf("浇水") >= 0 ||
                       userText.indexOf("土壤") >= 0;
  const bool dark = (env.light > 0.0f && env.light < 250.0f) || userText.indexOf("光照") >= 0 ||
                    userText.indexOf("补光") >= 0;

  if (hot || humid) {
    plan.cmd = "fan_on";
    plan.device = "fan";
    plan.action = "on";
    plan.fanMode = "direct";
    plan.fanLevel = 2;
    plan.fanDuty = 70;
    plan.tempTarget = 26.5f;
    plan.humiTarget = 60.0f;
    plan.durationSec = 180;
    plan.reason = "temperature_or_humidity_high";
    plan.reply = "Environment is warm or humid. Ventilation is recommended with a 26.5C and 60% humidity target.";
    plan.displayLine1 = "Fan ON suggested";
    plan.displayLine2 = "Target 26.5C 60%RH";
    plan.displayLevel = "warning";
  } else if (drySoil) {
    plan.intent = "irrigation_adjust";
    plan.cmd = "pump_on";
    plan.device = "pump";
    plan.action = "on";
    plan.soilMin = 40.0f;
    plan.soilMax = 65.0f;
    plan.durationSec = 5;
    plan.reason = "soil_low";
    plan.reply = "Soil moisture is low. Short irrigation is recommended.";
    plan.displayLine1 = "Pump ignored";
    plan.displayLine2 = "No soil sensor";
    plan.displayLevel = "warning";
  } else if (dark) {
    plan.intent = "light_adjust";
    plan.cmd = "lamp_on";
    plan.device = "lamp";
    plan.action = "on";
    plan.lampLevel = 2;
    plan.lampDuty = 60;
    plan.lightMin = 400.0f;
    plan.lightMax = 1200.0f;
    plan.durationSec = 300;
    plan.reason = "light_low";
    plan.reply = "Light is weak. Supplemental lighting is recommended.";
    plan.displayLine1 = "Lamp ON suggested";
    plan.displayLine2 = "Target 400-1200lux";
    plan.displayLevel = "info";
  } else {
    plan.intent = "observe";
    plan.cmd = "none";
    plan.device = "none";
    plan.action = "none";
    plan.durationSec = 0;
    plan.confidence = 0.70f;
    plan.reason = "environment_normal";
    plan.reply = "Environment is acceptable. Keep monitoring.";
    plan.displayLine1 = "Env normal";
    plan.displayLine2 = "Keep monitoring";
    plan.displayLevel = "info";
  }
  syncSingleActionFromTopLevel(plan);
  return true;
}

bool QwenClient::askHttp(const String& userText, const EnvData& env, AiPlan& plan, String& error) {
  if (String(AI_API_KEY).startsWith("YOUR_")) {
    error = String(AI_PROVIDER_NAME) + " API key is not configured";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  const String url = String(AI_BASE_URL) + "/chat/completions";
  if (!http.begin(client, url)) {
    error = "http.begin failed";
    return false;
  }

  http.useHTTP10(true);
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(AI_API_KEY));
  http.addHeader("Connection", "close");

  DynamicJsonDocument req(8192);
  req["model"] = AI_MODEL;
  req["temperature"] = 0.2;
  req["max_tokens"] = 256;
  req["stream"] = false;
  req["thinking"]["type"] = "disabled";
  req["response_format"]["type"] = "json_object";

  JsonArray messages = req.createNestedArray("messages");
  JsonObject sys = messages.createNestedObject();
  sys["role"] = "system";
  sys["content"] = kSystemPrompt;

  JsonObject user = messages.createNestedObject();
  user["role"] = "user";
  user["content"] = buildUserPayload(userText, env);

  String body;
  serializeJson(req, body);

  const int code = http.POST(body);
  const String response = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    error = "HTTP " + String(code) + ": " + response;
    return false;
  }

  if (response.length() == 0) {
    error = "HTTP " + String(code) + " empty response body";
    return false;
  }

  DynamicJsonDocument resp(12288);
  const DeserializationError err = deserializeJson(resp, response);
  if (err) {
    error = String("response JSON parse failed: ") + err.c_str();
    return false;
  }

  const String content = resp["choices"][0]["message"]["content"] | "";
  if (content.length() == 0) {
    error = "empty model content";
    return false;
  }

  return parseQwenContent(content, plan, error);
}

String QwenClient::buildUserPayload(const String& userText, const EnvData& env) const {
  DynamicJsonDocument doc(2048);
  doc["user_text"] = userText;
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
  devices["fan"] = true;
  devices["pump"] = true;
  devices["lamp"] = true;
  devices["curtain"] = true;

  JsonObject safety = doc.createNestedObject("safety");
  safety["fan_max_duration_sec"] = 300;
  safety["pump_max_duration_sec"] = 10;
  safety["lamp_max_duration_sec"] = 600;
  safety["curtain_servo_mcu_pin"] = "PA11";
  safety["fan_levels"] = "1=40%,2=70%,3=100%";
  safety["lamp_levels"] = "1=30%,2=60%,3=100%";
  safety["fan_modes"] = "direct,natural";
  safety["curtain_percent_values"] = "0,50,100";
  safety["temp_min"] = 18;
  safety["temp_max"] = 35;
  safety["humi_min"] = 40;
  safety["humi_max"] = 85;
  safety["soil_min"] = 25;
  safety["soil_max"] = 80;

  String out;
  serializeJson(doc, out);
  return out;
}

bool QwenClient::parseQwenContent(const String& content, AiPlan& plan, String& error) const {
  const String json = stripJsonFence(content);
  DynamicJsonDocument doc(4096);
  const DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("model JSON parse failed: ") + err.c_str() + " content=" + content;
    return false;
  }

  plan = AiPlan{};
  plan.valid = true;
  plan.raw = json;
  plan.reply = doc["reply"] | "";
  plan.intent = doc["intent"] | "none";
  plan.controlMode = doc["control_mode"] | "closed_loop";
  plan.fanMode = doc["fan_mode"] | "direct";
  plan.fanLevel = doc["fan_level"] | 0;
  plan.fanDuty = doc["fan_duty"] | 0;
  plan.lampLevel = doc["lamp_level"] | 0;
  plan.lampDuty = doc["lamp_duty"] | 0;
  plan.curtainPercent = doc["curtain_percent"] | -1;
  plan.confidence = doc["confidence"] | 0.0f;
  plan.reason = doc["reason"] | "";

  JsonObject display = doc["display"];
  if (!display.isNull()) {
    plan.displayTitle = display["title"] | "AI";
    plan.displayLine1 = display["line1"] | "";
    plan.displayLine2 = display["line2"] | "";
    plan.displayLevel = display["level"] | "info";
  }
  if (plan.displayLine1.length() == 0) {
    plan.displayLine1 = plan.reply;
  }

  JsonObject targets = doc["targets"];
  if (!targets.isNull()) {
    JsonObject tempObj = targets["temperature_c"].as<JsonObject>();
    JsonObject humiObj = targets["humidity_percent"].as<JsonObject>();
    JsonObject soilObj = targets["soil_percent"].as<JsonObject>();
    JsonObject lightObj = targets["light_lux"].as<JsonObject>();

    if (targets.containsKey("temp")) {
      plan.tempTarget = targets["temp"].as<float>();
    } else if (!tempObj.isNull() && tempObj.containsKey("target")) {
      plan.tempTarget = tempObj["target"].as<float>();
    }
    if (targets.containsKey("humi")) {
      plan.humiTarget = targets["humi"].as<float>();
    } else if (!humiObj.isNull() && humiObj.containsKey("target")) {
      plan.humiTarget = humiObj["target"].as<float>();
    }
    if (targets.containsKey("soil_min")) {
      plan.soilMin = targets["soil_min"].as<float>();
    } else if (!soilObj.isNull() && soilObj.containsKey("min")) {
      plan.soilMin = soilObj["min"].as<float>();
    }
    if (targets.containsKey("soil_max")) {
      plan.soilMax = targets["soil_max"].as<float>();
    } else if (!soilObj.isNull() && soilObj.containsKey("max")) {
      plan.soilMax = soilObj["max"].as<float>();
    }
    if (targets.containsKey("light_min")) {
      plan.lightMin = targets["light_min"].as<float>();
    } else if (!lightObj.isNull() && lightObj.containsKey("min")) {
      plan.lightMin = lightObj["min"].as<float>();
    }
    if (targets.containsKey("light_max")) {
      plan.lightMax = targets["light_max"].as<float>();
    } else if (!lightObj.isNull() && lightObj.containsKey("max")) {
      plan.lightMax = lightObj["max"].as<float>();
    }
  }

  plan.cmd = doc["cmd"] | "none";

  JsonArray actions = doc["actions"].as<JsonArray>();
  if (!actions.isNull() && actions.size() > 0) {
    for (JsonVariant actionValue : actions) {
      JsonObject actionObj = actionValue.as<JsonObject>();
      if (actionObj.isNull()) {
        continue;
      }
      AiAction action;
      action.device = actionObj["device"] | "none";
      action.action = actionObj["action"] | "";
      if (action.action.length() == 0) {
        action.action = actionObj["suggested_state"] | "none";
      }
      action.mode = actionObj["mode"] | plan.fanMode;
      action.level = actionObj["level"] | 0;
      action.duty = actionObj["duty"] | 0;
      action.percent = actionObj["percent"] | -1;
      action.durationSec = actionObj["duration_sec"] | plan.durationSec;
      (void)addAction(plan, action);
    }
  }

  if (plan.actionCount == 0) {
    plan.cmd = doc["cmd"] | "none";
  }

  if (plan.cmd == "fan_on" && plan.fanLevel <= 0) {
    plan.fanLevel = 2;
    plan.fanDuty = 70;
  }
  if (plan.cmd == "fan_off") {
    plan.fanLevel = 0;
    plan.fanDuty = 0;
  }
  if (plan.cmd == "lamp_on" && plan.lampLevel <= 0) {
    plan.lampLevel = 2;
    plan.lampDuty = 60;
  }
  if (plan.cmd == "lamp_off") {
    plan.lampLevel = 0;
    plan.lampDuty = 0;
  }
  if (plan.cmd == "curtain_open" && plan.curtainPercent < 0) {
    plan.curtainPercent = 100;
  }
  if (plan.cmd == "curtain_close" && plan.curtainPercent < 0) {
    plan.curtainPercent = 0;
  }
  syncSingleActionFromTopLevel(plan);

  return true;
}

String QwenClient::stripJsonFence(String content) const {
  content.trim();
  if (content.startsWith("```")) {
    const int firstNewline = content.indexOf('\n');
    const int lastFence = content.lastIndexOf("```");
    if (firstNewline >= 0 && lastFence > firstNewline) {
      content = content.substring(firstNewline + 1, lastFence);
      content.trim();
    }
  }
  return content;
}
