// 一時的な診断用ファーム（人の指示による）。IO34 の電圧を 0.5 秒ごとに mV で表示する。
// 赤外線は送らない。IRremoteESP8266 は使わない。
#include <Arduino.h>

static const uint8_t kAdcPin = 34;
static const uint32_t kIntervalMs = 500;
static uint32_t lastMs = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("adccheck: IO34 voltage monitor (analogReadMilliVolts, every 0.5 s)");
}

void loop() {
  uint32_t now = millis();
  if (now - lastMs >= kIntervalMs) {
    lastMs = now;
    uint32_t mv = analogReadMilliVolts(kAdcPin);
    Serial.printf("IO34=%u mV\n", (unsigned)mv);
  }
}
