# Voice Smart Home Gateway

Educational STM32L431 + ESP32-S3 gateway: sensors, lights, motor/curtain control, UART JSON messages, web control and optional voice/model services.

## Source layout

- `06efarm+ai/`: STM32Cube/Keil project, hardware drivers and UART integration.
- `ESPAI1/`: PlatformIO ESP32-S3 application.
- `ESPAI1_ArduinoIDE/`: alternate Arduino IDE snapshot.

Copy each `AppConfig.example.h` to `AppConfig.h` in the same directory before building. Supply your own Wi-Fi and API settings locally; the live files are ignored by Git. Example configuration defaults to mock mode. ASR endpoint/model values must match your own compatible provider.

Hardware-specific firmware is provided as a source snapshot; no device was flashed for this publication. MCU wiring and protocol notes are included beside the code. Original coursework reports, photographs, logs, credentials and Git history are excluded. ST/Arm source notices and licenses remain with vendor files.
