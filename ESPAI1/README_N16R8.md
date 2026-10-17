# ESPAI1 YD-ESP32-S3 N16R8 Build

This project now defaults to a YD-ESP32-S3 style module with 16 MB flash and 8 MB PSRAM.

Use the board USB port marked `UART` for PlatformIO upload and `Serial` logs. The firmware keeps
`ARDUINO_USB_CDC_ON_BOOT=0` so logs go through the USB-UART bridge.

## Wiring

STM32 UART:

```text
ESP32-S3 GPIO17 TX  -> STM32 PA3 USART2_RX
ESP32-S3 GPIO16 RX  <- STM32 PA2 USART2_TX
ESP32-S3 GND        -> STM32 GND
```

INMP441:

```text
INMP441 SCK/BCLK    -> ESP32-S3 GPIO4
INMP441 WS/LRCL     -> ESP32-S3 GPIO5
INMP441 SD/DOUT     -> ESP32-S3 GPIO6
INMP441 L/R         -> GND
INMP441 VDD         -> 3V3
INMP441 GND         -> GND
```

## Build and upload

```powershell
& 'C:/path/to/project\.platformio\penv\Scripts\platformio.exe' run
& 'C:/path/to/project\.platformio\penv\Scripts\platformio.exe' run -t upload --upload-port COM6
```

If upload stops at `Connecting...`, hold `BOOT`, release it after writing starts.

## Old WROOM32 fallback

```powershell
& 'C:/path/to/project\.platformio\penv\Scripts\platformio.exe' run -e esp32wroom32
& 'C:/path/to/project\.platformio\penv\Scripts\platformio.exe' run -e esp32wroom32 -t upload --upload-port COM6
```

## Runtime check

Open serial monitor at 115200. On boot, the ESP32-S3 N16R8 firmware should print:

```text
[MEM] ... psram=yes ...
```

When voice recording starts, it should print:

```text
[VOICE] audio buffer in PSRAM: ... bytes
```

If it prints `psram not found`, the board type, PSRAM wiring, or module variant is not matching the N16R8 configuration.
