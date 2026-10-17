#pragma once

// Set to 1 for offline demo. Set to 0 after Wi-Fi and DeepSeek are configured.
#define AI_MOCK_MODE 1

// Wi-Fi for real AI API mode. ESP-WROOM-32 supports 2.4 GHz Wi-Fi only.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// DeepSeek OpenAI-compatible chat endpoint.
// Get a key from https://platform.deepseek.com/api_keys
#define AI_PROVIDER_NAME "OpenAI-compatible"
#define AI_BASE_URL "https://dashscope.aliyuncs.com/compatible-mode/v1"
#define AI_API_KEY ""
#define AI_MODEL "YOUR_CHAT_MODEL"

// DashScope/Bailian ASR endpoint for INMP441 voice input.
// DeepSeek still handles the text planning step; this key is only for ASR.
#define ASR_HOST "example.invalid"
#define ASR_PATH "/configure-your-asr-endpoint"
#define ASR_API_KEY ""
#define ASR_MODEL "YOUR_ASR_MODEL"

// ESP32 UART2 to STM32.
// ESP32 TX2 GPIO17 -> STM32 RX
// ESP32 RX2 GPIO16 <- STM32 TX
#define MCU_UART_BAUD 115200
#define MCU_UART_RX_PIN 16
#define MCU_UART_TX_PIN 17

// INMP441 I2S microphone wiring for YD-ESP32-S3.
// INMP441 SCK/BCLK -> GPIO4, WS/LRCL -> GPIO5, SD/DOUT -> GPIO6, L/R -> GND.
#define MIC_I2S_BCLK_PIN 4
#define MIC_I2S_WS_PIN 5
#define MIC_I2S_DATA_PIN 6
#define MIC_SAMPLE_RATE 16000
#define MIC_MAX_RECORD_SEC 10

// Optional OLED SSD1306 I2C display.
#define DISPLAY_ENABLED 0
#define OLED_SDA_PIN 21
#define OLED_SCL_PIN 22
#define OLED_I2C_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// USB serial input format:
//   ask now temperature is high and humidity is high
//   env {"type":"env","temp":30.2,"humi":78,"soil":42,"light":600,"fan":0,"pump":0,"lamp":0}
//   demo
