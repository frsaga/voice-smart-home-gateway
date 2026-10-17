# 05efarm ESP32 AI UART Integration

## Wiring

USART2 is used on the STM32 side.

```text
STM32 PA2 USART2_TX -> ESP32 GPIO16 RX2
STM32 PA3 USART2_RX <- ESP32 GPIO17 TX2
STM32 GND           -> ESP32 GND
Baud rate: 115200
```

## STM32 to ESP32

STM32 sends current E53_IA1 environment data every second:

```json
{
  "type": "env",
  "temp": 28.5,
  "humi": 65.0,
  "light": 120.0,
  "fan": 0,
  "lamp": 1,
  "pump": 0
}
```

`light` is mapped from `E53_IA1_Data.Lux`. There is no soil sensor or pump in this `05efarm` board, so those fields are ignored by STM32 control.

## ESP32 to STM32

STM32 accepts one JSON object per line:

```json
{
  "type": "ai_plan",
  "cmd": "fan_on",
  "duration_sec": 180,
  "display": {
    "title": "AI",
    "line1": "Fan ON suggested",
    "line2": "Target 26.5C 60%RH",
    "level": "warning"
  },
  "targets": {
    "temp": 26.5,
    "humi": 60,
    "light_min": 400
  }
}
```

## Control Mapping

The actual `05efarm` hardware is authoritative:

- `fan_on` / `fan_off` controls `E53_IA1_Motor_Set`.
- `lamp_on` / `lamp_off` controls `E53_IA1_LED_Set`.
- `targets.temp` and `targets.humi` can drive the motor for ventilation.
- `targets.light_min` can drive the LED for supplemental light.
- `pump_*`, `soil_*`, and unknown devices are ignored for control, but AI display text can still be shown.

## Display

The STM32 screen shows:

```text
display.line1
display.line2
```

The current LCD code supports ASCII and GBK bytes. ESP32 is therefore configured to send `display` text in ASCII. The full AI `reply` can remain Chinese in the JSON for USB serial/debug use.

