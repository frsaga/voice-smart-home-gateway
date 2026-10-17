# ESPAI1 UART Protocol

All UART frames are one JSON object per line, ending with `\n`.

## STM32 to ESP32: environment

```json
{
  "type": "env",
  "temp": 30.2,
  "humi": 78,
  "soil": 42,
  "light": 600,
  "fan": 0,
  "pump": 0,
  "lamp": 0
}
```

## ESP32 to STM32: AI plan

```json
{
  "type": "ai_plan",
  "valid": true,
  "reply": "Environment is warm or humid. Ventilation is recommended.",
  "intent": "climate_adjust",
  "cmd": "fan_on",
  "device": "fan",
  "action": "on",
  "duration_sec": 180,
  "control_mode": "closed_loop",
  "confidence": 0.86,
  "reason": "temperature_or_humidity_high",
  "display": {
    "title": "AI",
    "line1": "Fan ON suggested",
    "line2": "Target 26.5C 60%RH",
    "level": "warning"
  },
  "targets": {
    "temp": 26.5,
    "humi": 60
  }
}
```

STM32 should treat this as a suggestion. Final actuator decisions, maximum runtime, sensor validity checks and emergency stop logic stay on STM32.

## Suggested STM32 behavior

- `cmd=fan_on`: start ventilation if sensors are valid and fan safety permits it.
- `targets.temp`: close the loop until current temperature is near target.
- `targets.humi`: close the loop until current humidity is near target.
- `duration_sec`: upper bound, not a forced runtime.
- `confidence < 0.5`: display only, avoid automatic control.
- `display`: short text for the STM32 screen. Use `line1` and `line2` directly on small LCD/OLED screens.
