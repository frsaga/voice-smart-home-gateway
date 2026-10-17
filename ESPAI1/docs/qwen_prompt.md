# Qwen Planner Prompt

The firmware uses a system prompt that asks Qwen to return JSON only.

Expected model output:

```json
{
  "reply": "Current air is warm and humid. Ventilation is recommended.",
  "intent": "climate_adjust",
  "targets": {
    "temp": 26.5,
    "humi": 60,
    "soil_min": 35,
    "soil_max": 70
  },
  "actions": [
    {
      "device": "fan",
      "action": "on",
      "duration_sec": 180
    }
  ],
  "safety": {
    "require_sensor_valid": true,
    "max_duration_sec": 300
  },
  "control_mode": "closed_loop",
  "confidence": 0.86,
  "reason": "temperature_and_humidity_high"
}
```

Design rule:

```text
AI produces target parameters and suggested actions.
STM32 performs the final closed-loop control and safety checks.
```

