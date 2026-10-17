#pragma once

#include <Arduino.h>

#include "AiTypes.h"

class UartProtocol {
 public:
  void begin();
  bool pollEnv(EnvData& env);
  bool pollVoiceCommand(String& cmd);
  void sendAiPlan(const AiPlan& plan);
  void sendStatus(const String& status);
  void sendVoiceAck(const String& state, const String& text);
  void sendHeaderInfo(const String& timeText, const String& weatherText);

 private:
  String rxLine_;
  String pendingVoiceCmd_;
  bool parseEnvJson(const String& line, EnvData& env);
};
