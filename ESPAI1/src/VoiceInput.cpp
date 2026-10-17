#include "VoiceInput.h"

#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "AppConfig.h"

static const i2s_port_t kMicI2sPort = I2S_NUM_0;

namespace {

static const char kBase64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64EncodedLen(size_t byteLen) {
  return ((byteLen + 2U) / 3U) * 4U;
}

bool writeAll(WiFiClientSecure& client, const uint8_t* data, size_t len, uint32_t timeoutMs = 45000) {
  size_t written = 0;
  const uint32_t start = millis();
  uint32_t zeroWriteCount = 0;
  while (written < len) {
    if ((millis() - start) > timeoutMs) {
      return false;
    }
    if (!client.connected()) {
      return false;
    }
    const size_t n = client.write(data + written, min<size_t>(1024, len - written));
    if (n == 0) {
      zeroWriteCount++;
      if ((zeroWriteCount % 100U) == 0U) {
        delay(20);
      } else {
        delay(2);
      }
      continue;
    }
    written += n;
    zeroWriteCount = 0;
    delay(0);
  }
  return true;
}

bool writeAll(WiFiClientSecure& client, const String& text, uint32_t timeoutMs = 45000) {
  return writeAll(client, reinterpret_cast<const uint8_t*>(text.c_str()), text.length(), timeoutMs);
}

bool writeAllFromPsram(WiFiClientSecure& client, const uint8_t* data, size_t len, size_t& writtenOut,
                       uint32_t timeoutMs = 45000) {
  uint8_t tx[1024];
  size_t written = 0;
  const uint32_t start = millis();
  uint32_t zeroWriteCount = 0;
  uint32_t lastProgress = millis();
  size_t lastProgressBytes = 0;

  while (written < len) {
    if ((millis() - start) > timeoutMs) {
      writtenOut = written;
      return false;
    }
    if (!client.connected()) {
      writtenOut = written;
      return false;
    }

    const size_t chunk = min<size_t>(sizeof(tx), len - written);
    memcpy(tx, data + written, chunk);
    const size_t n = client.write(tx, chunk);
    if (n == 0) {
      zeroWriteCount++;
      delay((zeroWriteCount % 50U) == 0U ? 20U : 2U);
      continue;
    }
    written += n;
    zeroWriteCount = 0;
    if ((written - lastProgressBytes >= 8192U) || ((millis() - lastProgress) > 3000U)) {
      Serial.printf("[ASR] body uploaded %u/%u bytes\n",
                    static_cast<unsigned>(written),
                    static_cast<unsigned>(len));
      lastProgressBytes = written;
      lastProgress = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  writtenOut = written;
  return true;
}

class Base64StreamEncoder {
 public:
  bool flush(WiFiClientSecure& client) {
    if (outLen_ == 0) {
      return true;
    }
    const bool ok = writeAll(client, reinterpret_cast<const uint8_t*>(out_), outLen_);
    outLen_ = 0;
    return ok;
  }

  bool writeBytes(WiFiClientSecure& client, const uint8_t* data, size_t len) {
    size_t pos = 0;

    if (tailLen_ > 0) {
      while (tailLen_ < 3 && pos < len) {
        tail_[tailLen_++] = data[pos++];
      }
      if (tailLen_ == 3) {
        if (!writeTriplet(client, tail_, 3, false)) {
          return false;
        }
        tailLen_ = 0;
      }
    }

    while (pos + 3 <= len) {
      if (!writeTriplet(client, data + pos, 3, false)) {
        return false;
      }
      pos += 3;
    }

    while (pos < len) {
      tail_[tailLen_++] = data[pos++];
    }
    return true;
  }

  bool finish(WiFiClientSecure& client) {
    if (tailLen_ > 0) {
      if (!writeTriplet(client, tail_, tailLen_, true)) {
        tailLen_ = 0;
        return false;
      }
      tailLen_ = 0;
    }
    return flush(client);
  }

 private:
  bool appendOutput(WiFiClientSecure& client, const char* data, size_t len) {
    if (outLen_ + len > sizeof(out_)) {
      if (!flush(client)) {
        return false;
      }
    }
    memcpy(out_ + outLen_, data, len);
    outLen_ += len;
    return true;
  }

  bool writeTriplet(WiFiClientSecure& client, const uint8_t* in, size_t len, bool pad) {
    char out[4];
    const uint32_t v = (static_cast<uint32_t>(in[0]) << 16) |
                       (static_cast<uint32_t>(len > 1 ? in[1] : 0) << 8) |
                       static_cast<uint32_t>(len > 2 ? in[2] : 0);
    out[0] = kBase64Table[(v >> 18) & 0x3F];
    out[1] = kBase64Table[(v >> 12) & 0x3F];
    out[2] = (len > 1) ? kBase64Table[(v >> 6) & 0x3F] : (pad ? '=' : '\0');
    out[3] = (len > 2) ? kBase64Table[v & 0x3F] : (pad ? '=' : '\0');
    return appendOutput(client, out, sizeof(out));
  }

  uint8_t tail_[3] = {};
  size_t tailLen_ = 0;
  char out_[4096] = {};
  size_t outLen_ = 0;
};

class Base64BufferEncoder {
 public:
  Base64BufferEncoder(char* out, size_t capacity, size_t& pos)
      : out_(out), capacity_(capacity), pos_(pos) {}

  bool writeBytes(const uint8_t* data, size_t len) {
    size_t inPos = 0;

    if (tailLen_ > 0) {
      while (tailLen_ < 3 && inPos < len) {
        tail_[tailLen_++] = data[inPos++];
      }
      if (tailLen_ == 3) {
        if (!writeTriplet(tail_, 3, false)) {
          return false;
        }
        tailLen_ = 0;
      }
    }

    while (inPos + 3 <= len) {
      if (!writeTriplet(data + inPos, 3, false)) {
        return false;
      }
      inPos += 3;
    }

    while (inPos < len) {
      tail_[tailLen_++] = data[inPos++];
    }
    return true;
  }

  bool finish() {
    if (tailLen_ > 0) {
      if (!writeTriplet(tail_, tailLen_, true)) {
        tailLen_ = 0;
        return false;
      }
      tailLen_ = 0;
    }
    return true;
  }

 private:
  bool appendOutput(const char* data, size_t len) {
    if (pos_ + len > capacity_) {
      return false;
    }
    memcpy(out_ + pos_, data, len);
    pos_ += len;
    return true;
  }

  bool writeTriplet(const uint8_t* in, size_t len, bool pad) {
    char out[4];
    const uint32_t v = (static_cast<uint32_t>(in[0]) << 16) |
                       (static_cast<uint32_t>(len > 1 ? in[1] : 0) << 8) |
                       static_cast<uint32_t>(len > 2 ? in[2] : 0);
    out[0] = kBase64Table[(v >> 18) & 0x3F];
    out[1] = kBase64Table[(v >> 12) & 0x3F];
    out[2] = (len > 1) ? kBase64Table[(v >> 6) & 0x3F] : (pad ? '=' : '\0');
    out[3] = (len > 2) ? kBase64Table[v & 0x3F] : (pad ? '=' : '\0');
    return appendOutput(out, sizeof(out));
  }

  char* out_ = nullptr;
  size_t capacity_ = 0;
  size_t& pos_;
  uint8_t tail_[3] = {};
  size_t tailLen_ = 0;
};

String readHttpResponse(WiFiClientSecure& client, uint32_t timeoutMs) {
  String response;
  const uint32_t start = millis();
  while (client.connected() || client.available()) {
    while (client.available()) {
      response += static_cast<char>(client.read());
      if (response.length() > 16384) {
        return response;
      }
    }
    if (millis() - start > timeoutMs) {
      break;
    }
    delay(5);
  }
  return response;
}

bool headerHasChunked(const String& header) {
  String lower = header;
  lower.toLowerCase();
  return lower.indexOf("transfer-encoding: chunked") >= 0;
}

String decodeChunkedBody(const String& body) {
  String out;
  size_t pos = 0;
  while (pos < body.length()) {
    const int lineEnd = body.indexOf("\r\n", pos);
    if (lineEnd < 0) {
      break;
    }
    String lenText = body.substring(pos, lineEnd);
    const int semicolon = lenText.indexOf(';');
    if (semicolon >= 0) {
      lenText = lenText.substring(0, semicolon);
    }
    const size_t chunkLen = strtoul(lenText.c_str(), nullptr, 16);
    if (chunkLen == 0) {
      break;
    }
    pos = static_cast<size_t>(lineEnd) + 2U;
    if (pos + chunkLen > body.length()) {
      break;
    }
    out += body.substring(pos, pos + chunkLen);
    pos += chunkLen + 2U;
  }
  return out;
}

String extractAsrText(JsonVariant root) {
  const char* outputText = root["output"]["text"] | nullptr;
  if (outputText != nullptr) {
    return String(outputText);
  }

  JsonVariant choice = root["output"]["choices"][0];
  const char* messageText = choice["message"]["content"] | nullptr;
  if (messageText != nullptr) {
    return String(messageText);
  }

  JsonArray content = choice["message"]["content"].as<JsonArray>();
  for (JsonVariant item : content) {
    const char* text = item["text"] | item["transcript"] | item["content"] | nullptr;
    if (text != nullptr) {
      return String(text);
    }
  }

  const char* directText = root["text"] | nullptr;
  if (directText != nullptr) {
    return String(directText);
  }

  return "";
}

String decodeJsonString(const String& value) {
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c != '\\' || i + 1 >= value.length()) {
      out += c;
      continue;
    }

    c = value[++i];
    if (c == 'n') {
      out += '\n';
    } else if (c == 'r') {
      out += '\r';
    } else if (c == 't') {
      out += '\t';
    } else if (c == '"' || c == '\\' || c == '/') {
      out += c;
    }
  }
  return out;
}

String extractFirstJsonTextField(const String& json) {
  const String key = "\"text\"";
  int keyPos = json.indexOf(key);
  while (keyPos >= 0) {
    int colon = json.indexOf(':', keyPos + key.length());
    if (colon < 0) {
      return "";
    }
    int quote = json.indexOf('"', colon + 1);
    if (quote < 0) {
      return "";
    }

    String raw;
    bool escape = false;
    for (int i = quote + 1; i < static_cast<int>(json.length()); ++i) {
      const char c = json[i];
      if (escape) {
        raw += '\\';
        raw += c;
        escape = false;
        continue;
      }
      if (c == '\\') {
        escape = true;
        continue;
      }
      if (c == '"') {
        String text = decodeJsonString(raw);
        text.trim();
        if (text.length() > 0) {
          return text;
        }
        break;
      }
      raw += c;
    }

    keyPos = json.indexOf(key, keyPos + key.length());
  }
  return "";
}

int16_t* allocAudioBuffer(size_t samples, String& error) {
  const size_t bytes = samples * sizeof(int16_t);

#if defined(ESP32_N16R8) || defined(BOARD_HAS_PSRAM)
  if (!psramFound()) {
    error = "psram not found";
    return nullptr;
  }

  int16_t* buffer = static_cast<int16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer != nullptr) {
    Serial.printf("[VOICE] audio buffer in PSRAM: %u bytes, free PSRAM=%u\n",
                  static_cast<unsigned>(bytes),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return buffer;
  }

  error = "no psram for audio";
  return nullptr;
#else
  int16_t* buffer = static_cast<int16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
  if (buffer != nullptr) {
    Serial.printf("[VOICE] audio buffer in internal RAM: %u bytes, free heap=%u\n",
                  static_cast<unsigned>(bytes),
                  static_cast<unsigned>(ESP.getFreeHeap()));
    return buffer;
  }

  error = "no memory for audio";
  return nullptr;
#endif
}

}  // namespace

void VoiceInput::begin() {
  Serial.printf("[MEM] heap=%u largest=%u psram=%s free_psram=%u\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                psramFound() ? "yes" : "no",
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
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

bool VoiceInput::recording() const {
  return recording_;
}

bool VoiceInput::installI2s(String& error) {
  if (i2sInstalled_) {
    return true;
  }

  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = MIC_SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 4;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = MIC_I2S_BCLK_PIN;
  pins.ws_io_num = MIC_I2S_WS_PIN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_I2S_DATA_PIN;

  esp_err_t ret = i2s_driver_install(kMicI2sPort, &config, 0, nullptr);
  if (ret != ESP_OK) {
    error = "i2s install failed";
    return false;
  }

  ret = i2s_set_pin(kMicI2sPort, &pins);
  if (ret != ESP_OK) {
    i2s_driver_uninstall(kMicI2sPort);
    error = "i2s set pin failed";
    return false;
  }

  i2s_zero_dma_buffer(kMicI2sPort);
  i2sInstalled_ = true;
  return true;
}

void VoiceInput::uninstallI2s() {
  if (i2sInstalled_) {
    i2s_driver_uninstall(kMicI2sPort);
    i2sInstalled_ = false;
  }
}

bool VoiceInput::beginRecording(String& error) {
  if (recording_) {
    return true;
  }

  if (!installI2s(error)) {
    return false;
  }

  maxSamples_ = static_cast<size_t>(MIC_SAMPLE_RATE) * MIC_MAX_RECORD_SEC;
  if (pcm_ == nullptr) {
    pcm_ = allocAudioBuffer(maxSamples_, error);
  }
  if (pcm_ == nullptr) {
    uninstallI2s();
    return false;
  }

  sampleCount_ = 0;
  recording_ = true;
  return true;
}

bool VoiceInput::updateRecording() {
  if (!recording_ || pcm_ == nullptr) {
    return false;
  }

  int32_t raw[128];
  size_t bytesRead = 0;
  const esp_err_t ret = i2s_read(kMicI2sPort, raw, sizeof(raw), &bytesRead, 0);
  if (ret != ESP_OK || bytesRead == 0) {
    return false;
  }

  const size_t samples = bytesRead / sizeof(raw[0]);
  for (size_t i = 0; i < samples && sampleCount_ < maxSamples_; ++i) {
    int32_t s = raw[i] >> 14;
    if (s > 32767) {
      s = 32767;
    } else if (s < -32768) {
      s = -32768;
    }
    pcm_[sampleCount_++] = static_cast<int16_t>(s);
  }

  return sampleCount_ >= maxSamples_;
}

bool VoiceInput::stopAndTranscribe(String& transcript, String& error) {
  if (!recording_) {
    error = "not recording";
    return false;
  }

  recording_ = false;
  uninstallI2s();

  if (sampleCount_ < static_cast<size_t>(MIC_SAMPLE_RATE / 4)) {
    error = "recording too short";
    return false;
  }

  int16_t peak = 0;
  uint64_t sumSquares = 0;
  for (size_t i = 0; i < sampleCount_; ++i) {
    int32_t s = pcm_[i];
    if (s < 0) {
      s = -s;
    }
    if (s > peak) {
      peak = static_cast<int16_t>(s);
    }
    sumSquares += static_cast<uint64_t>(static_cast<int32_t>(pcm_[i]) * static_cast<int32_t>(pcm_[i]));
  }
  const float rms = sqrtf(static_cast<float>(sumSquares) / static_cast<float>(sampleCount_));
  Serial.printf("[VOICE] samples=%u rms=%.1f peak=%d\n",
                static_cast<unsigned>(sampleCount_),
                rms,
                peak);
  if (peak < 120 && rms < 12.0f) {
    error = "mic silent";
    return false;
  }

  return transcribePcm(transcript, error);
}

size_t VoiceInput::writeWavHeader(uint8_t* out, uint32_t pcmBytes) const {
  const uint32_t sampleRate = MIC_SAMPLE_RATE;
  const uint16_t channels = 1;
  const uint16_t bitsPerSample = 16;
  const uint32_t byteRate = sampleRate * channels * bitsPerSample / 8U;
  const uint16_t blockAlign = channels * bitsPerSample / 8U;
  const uint32_t riffSize = 36U + pcmBytes;

  memcpy(out + 0, "RIFF", 4);
  memcpy(out + 4, &riffSize, 4);
  memcpy(out + 8, "WAVEfmt ", 8);
  const uint32_t fmtSize = 16;
  const uint16_t audioFormat = 1;
  memcpy(out + 16, &fmtSize, 4);
  memcpy(out + 20, &audioFormat, 2);
  memcpy(out + 22, &channels, 2);
  memcpy(out + 24, &sampleRate, 4);
  memcpy(out + 28, &byteRate, 4);
  memcpy(out + 32, &blockAlign, 2);
  memcpy(out + 34, &bitsPerSample, 2);
  memcpy(out + 36, "data", 4);
  memcpy(out + 40, &pcmBytes, 4);
  return 44;
}

bool VoiceInput::transcribePcm(String& transcript, String& error) {
  if (String(ASR_API_KEY).startsWith("YOUR_")) {
    error = "ASR API key not configured";
    return false;
  }

  const uint32_t pcmBytes = static_cast<uint32_t>(sampleCount_ * sizeof(int16_t));
  uint8_t wavHeader[44];
  writeWavHeader(wavHeader, pcmBytes);

  const String prefix =
      String("{\"model\":\"") + ASR_MODEL +
      "\",\"input\":{\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\","
      "\"input_audio\":{\"data\":\"data:audio/wav;base64,";
  const String suffix = String("\"}}]}]},\"parameters\":{\"format\":\"wav\",\"sample_rate\":\"") +
                        MIC_SAMPLE_RATE + "\"}}";
  const size_t audioBytes = 44U + pcmBytes;
  const size_t bodyLen = prefix.length() + base64EncodedLen(audioBytes) + suffix.length();

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(20);
  client.setTimeout(20);

  Serial.printf("[ASR] connect %s%s, samples=%u\n",
                ASR_HOST,
                ASR_PATH,
                static_cast<unsigned>(sampleCount_));
  if (!client.connect(ASR_HOST, 443)) {
    error = "ASR connect failed";
    return false;
  }
  Serial.println("[ASR] connected");

  String requestHeader =
      String("POST ") + ASR_PATH + " HTTP/1.1\r\n" +
      "Host: " + ASR_HOST + "\r\n" +
      "Authorization: Bearer " + ASR_API_KEY + "\r\n" +
      "Content-Type: application/json\r\n" +
      "X-DashScope-SSE: disable\r\n" +
      "Connection: close\r\n" +
      "Content-Length: " + String(bodyLen) + "\r\n\r\n";

  if (!writeAll(client, requestHeader) || !writeAll(client, prefix)) {
    error = "ASR request write failed";
    client.stop();
    return false;
  }
  Serial.printf("[ASR] upload wav bytes=%u body=%u\n",
                static_cast<unsigned>(audioBytes),
                static_cast<unsigned>(bodyLen));

  Base64StreamEncoder encoder;
  if (!encoder.writeBytes(client, wavHeader, sizeof(wavHeader))) {
    error = "ASR wav header write failed";
    client.stop();
    return false;
  }

  const uint8_t* audio = reinterpret_cast<const uint8_t*>(pcm_);
  size_t sent = 0;
  uint32_t lastProgress = millis();
  uint32_t uploadStart = millis();
  while (sent < pcmBytes) {
    if ((millis() - uploadStart) > 60000) {
      error = "ASR upload timeout";
      client.stop();
      return false;
    }
    const size_t chunk = min<size_t>(1536, pcmBytes - sent);
    if (!encoder.writeBytes(client, audio + sent, chunk)) {
      error = "ASR audio write failed at " + String(sent) + "/" + String(pcmBytes);
      client.stop();
      return false;
    }
    sent += chunk;
    if ((millis() - lastProgress) > 3000) {
      Serial.printf("[ASR] uploaded %u/%u pcm bytes\n",
                    static_cast<unsigned>(sent),
                    static_cast<unsigned>(pcmBytes));
      lastProgress = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  if (!encoder.finish(client) || !writeAll(client, suffix)) {
    error = "ASR request finish failed";
    client.stop();
    return false;
  }
  Serial.println("[ASR] upload done, waiting response");

  const String rawResponse = readHttpResponse(client, 45000);
  client.stop();

  const int headerEnd = rawResponse.indexOf("\r\n\r\n");
  if (headerEnd < 0) {
    error = "ASR invalid HTTP response";
    return false;
  }

  const String responseHeader = rawResponse.substring(0, headerEnd);
  String responseBody = rawResponse.substring(headerEnd + 4);
  const int firstSpace = responseHeader.indexOf(' ');
  const int secondSpace = responseHeader.indexOf(' ', firstSpace + 1);
  const int code = (firstSpace >= 0 && secondSpace > firstSpace) ?
                   responseHeader.substring(firstSpace + 1, secondSpace).toInt() : -1;

  if (headerHasChunked(responseHeader)) {
    responseBody = decodeChunkedBody(responseBody);
  }

  if (code < 200 || code >= 300) {
    error = "ASR HTTP " + String(code) + ": " + responseBody.substring(0, 220);
    return false;
  }

  transcript = extractFirstJsonTextField(responseBody);
  transcript.trim();
  if (transcript.length() > 0) {
    Serial.print("[ASR] text: ");
    Serial.println(transcript);
    return true;
  }

  DynamicJsonDocument doc(8192);
  const DeserializationError err = deserializeJson(doc, responseBody);
  if (err) {
    error = String("ASR JSON parse failed: ") + err.c_str();
    return false;
  }

  transcript = extractAsrText(doc.as<JsonVariant>());
  transcript.trim();
  if (transcript.length() == 0) {
    error = "ASR empty text: " + responseBody.substring(0, 220);
    return false;
  }

  Serial.print("[ASR] text: ");
  Serial.println(transcript);
  return true;
}
