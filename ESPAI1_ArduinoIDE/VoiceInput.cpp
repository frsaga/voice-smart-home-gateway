#include "VoiceInput.h"

void VoiceInput::begin() {
}

bool VoiceInput::pollUsbCommand(String& line) {
  static String rx;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      line = rx;
      line.trim();
      rx = "";
      return line.length() > 0;
    }
    if (c != '\r' && rx.length() < 512) {
      rx += c;
    }
  }
  return false;
}

