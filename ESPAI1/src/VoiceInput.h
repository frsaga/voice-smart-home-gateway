#pragma once

#include <Arduino.h>

class VoiceInput {
 public:
  void begin();
  bool pollUsbCommand(String& line);
  bool beginRecording(String& error);
  bool updateRecording();
  bool stopAndTranscribe(String& transcript, String& error);
  bool recording() const;

 private:
  bool installI2s(String& error);
  void uninstallI2s();
  bool transcribePcm(String& transcript, String& error);
  size_t writeWavHeader(uint8_t* out, uint32_t pcmBytes) const;

  int16_t* pcm_ = nullptr;
  size_t maxSamples_ = 0;
  size_t sampleCount_ = 0;
  bool i2sInstalled_ = false;
  bool recording_ = false;
};
