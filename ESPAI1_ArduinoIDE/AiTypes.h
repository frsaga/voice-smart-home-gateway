#pragma once

#include <Arduino.h>

struct EnvData {
  bool valid = false;
  float temp = 0.0f;
  float humi = 0.0f;
  float soil = 0.0f;
  float light = 0.0f;
  int fan = 0;
  int pump = 0;
  int lamp = 0;
};

struct AiPlan {
  bool valid = false;
  String reply;
  String intent = "none";
  String cmd = "none";
  String reason;
  String device = "none";
  String action = "none";
  String controlMode = "closed_loop";
  String displayTitle = "AI";
  String displayLine1;
  String displayLine2;
  String displayLevel = "info";
  float confidence = 0.0f;
  float tempTarget = NAN;
  float humiTarget = NAN;
  float soilMin = NAN;
  float soilMax = NAN;
  float lightMin = NAN;
  float lightMax = NAN;
  int durationSec = 0;
  String raw;
};
