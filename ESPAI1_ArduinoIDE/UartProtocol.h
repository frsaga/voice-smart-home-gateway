#pragma once

#include <Arduino.h>

#include "AiTypes.h"

class UartProtocol {
 public:
  void begin();
  bool pollEnv(EnvData& env);
  void sendAiPlan(const AiPlan& plan);
  void sendStatus(const String& status);

 private:
  String rxLine_;
  bool parseEnvJson(const String& line, EnvData& env);
};

