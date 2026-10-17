#pragma once

#include <Arduino.h>

#include "AiTypes.h"

class DisplayService {
 public:
  void begin();
  void showBoot(const String& line);
  void showEnv(const EnvData& env);
  void showPlan(const AiPlan& plan);
  void showMessage(const String& title, const String& body);

 private:
  bool ready_ = false;
};

