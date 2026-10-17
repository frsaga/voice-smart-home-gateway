#include "DisplayService.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "AppConfig.h"

#if DISPLAY_ENABLED
static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
#endif

static String fitLine(const String& value, size_t maxLen) {
  if (value.length() <= maxLen) {
    return value;
  }
  return value.substring(0, maxLen - 1) + ".";
}

void DisplayService::begin() {
#if DISPLAY_ENABLED
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  ready_ = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
  if (ready_) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.display();
  }
#endif
}

void DisplayService::showBoot(const String& line) {
  showMessage("ESPAI1", line);
}

void DisplayService::showEnv(const EnvData& env) {
  if (!ready_) {
    return;
  }
#if DISPLAY_ENABLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("ENV");
  display.printf("T: %.1f C\n", env.temp);
  display.printf("H: %.0f %%\n", env.humi);
  display.printf("Soil: %.0f %%\n", env.soil);
  display.printf("Light: %.0f\n", env.light);
  display.printf("F%d P%d L%d\n", env.fan, env.pump, env.lamp);
  display.display();
#endif
}

void DisplayService::showPlan(const AiPlan& plan) {
  if (!ready_) {
    return;
  }
#if DISPLAY_ENABLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("AI PLAN");
  display.println(fitLine("Intent:" + plan.intent, 21));
  display.println(fitLine("Cmd:" + plan.cmd, 21));
  if (!isnan(plan.tempTarget)) {
    display.printf("T target: %.1f\n", plan.tempTarget);
  }
  if (!isnan(plan.humiTarget)) {
    display.printf("H target: %.0f\n", plan.humiTarget);
  }
  display.printf("Dur:%ds C:%.2f\n", plan.durationSec, plan.confidence);
  display.display();
#endif
}

void DisplayService::showMessage(const String& title, const String& body) {
  if (!ready_) {
    return;
  }
#if DISPLAY_ENABLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(fitLine(title, 21));
  display.println();
  display.println(fitLine(body, 21));
  display.display();
#endif
}

