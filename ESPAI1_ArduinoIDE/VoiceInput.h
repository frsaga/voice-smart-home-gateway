#pragma once

#include <Arduino.h>

class VoiceInput {
 public:
  void begin();
  bool pollUsbCommand(String& line);
};

