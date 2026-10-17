#pragma once

#include <Arduino.h>

#include "AiTypes.h"

class QwenClient {
 public:
  bool ask(const String& userText, const EnvData& env, AiPlan& plan, String& error);

 private:
  bool askMock(const String& userText, const EnvData& env, AiPlan& plan);
  bool askHttp(const String& userText, const EnvData& env, AiPlan& plan, String& error);
  String buildUserPayload(const String& userText, const EnvData& env) const;
  bool parseQwenContent(const String& content, AiPlan& plan, String& error) const;
  String stripJsonFence(String content) const;
};

