# ESPAI1

ESPAI1 is the ESP-WROOM-32 AI side project for the smart agriculture system.

Role split:

- ESP32: user interaction, Qwen API call, AI planning, display, UART output.
- STM32: sensor collection, actuator control, safety rules, BC28CNV MQTT cloud link.
- UART: the only coupling point between the two boards.

## Current flow

```text
USB text input / later ASR text
        |
        v
ESP32 QwenClient
        |
        v
AI plan JSON: targets + suggested action
        |
        v
OLED display + UART2 to STM32
```

The first version uses USB serial text to simulate speech recognition. Real microphone ASR can later replace `src/VoiceInput.cpp` without changing Qwen or STM32 UART protocol.

## Build

Open this folder with PlatformIO, then build the `esp32dev` environment.

```powershell
pio run
```

## Arduino IDE

Arduino IDE can open the sketch file:

```text
ESPAI1/ESPAI1.ino
```

Install these libraries from Library Manager:

- ArduinoJson by Benoit Blanchon
- Adafruit SSD1306
- Adafruit GFX Library

Select board:

```text
ESP32 Dev Module
```

Use these common settings:

- Upload Speed: `921600` or `115200`
- CPU Frequency: `240MHz`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Partition Scheme: `Default 4MB with spiffs`

If the board package is missing, install `esp32 by Espressif Systems` in Boards Manager.

## Quick test

Open the PlatformIO serial monitor at `115200`, then type:

```text
demo
```

Or manually set environment data:

```text
env {"type":"env","temp":30.2,"humi":78,"soil":42,"light":600,"fan":0,"pump":0,"lamp":0}
ask The temperature is a bit high and humidity is high.
```

You can also use the USB serial port as a temporary voice input replacement:

```text
ai 现在温度有点高湿度有点大
```

or directly send a plain text question:

```text
现在温度有点高湿度有点大
```

ESP32 replies on the USB serial port with:

```text
[AI_REPLY]
...
[AI_JSON] {"type":"usb_ai_response",...}
```

The same AI plan is also sent to STM32 through UART2.

Default mode can be switched between offline mock mode and real API mode:

```cpp
#define AI_MOCK_MODE 1
```

To use the real DeepSeek API, edit `include/AppConfig.h`:

```cpp
#define AI_MOCK_MODE 0
#define WIFI_SSID "..."
#define WIFI_PASSWORD "..."
#define AI_BASE_URL "https://api.deepseek.com"
#define AI_API_KEY "sk-..."
#define AI_MODEL "deepseek-v4-flash"
```

## UART wiring

Default UART2 pins:

- ESP32 GPIO17 TX2 -> STM32 RX
- ESP32 GPIO16 RX2 <- STM32 TX
- ESP32 GND <-> STM32 GND

Default baud rate: `115200`.

## OLED wiring

Default SSD1306 I2C pins:

- SDA: GPIO21
- SCL: GPIO22
- Address: `0x3C`

The OLED display uses ASCII fields. The full Chinese AI reply is printed to USB serial and sent to STM32 in UTF-8 JSON.
