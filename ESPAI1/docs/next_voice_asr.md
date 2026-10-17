# Adding Real Voice ASR

The current `VoiceInput` module reads USB serial text, which is useful for independent ESP32 testing.

For real voice control, replace `VoiceInput` with:

```text
INMP441 I2S microphone
        |
ESP32 records PCM audio
        |
ASR API converts speech to text
        |
runAi(asr_text)
```

Recommended hardware for ESP-WROOM-32:

- INMP441 I2S microphone
- Push button for recording
- Short recording window, such as 3 to 5 seconds

Note: ESP-WROOM-32 has limited RAM and no PSRAM on many boards. Keep audio chunks short. If you later need smoother real-time ASR, ESP32-S3 with PSRAM is a better board.

