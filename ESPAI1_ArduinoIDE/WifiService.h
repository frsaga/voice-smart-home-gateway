#pragma once

#include <Arduino.h>

class WifiService {
 public:
  bool begin(unsigned long timeoutMs);
  bool connected() const;
  String ip() const;
};

