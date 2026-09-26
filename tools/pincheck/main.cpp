// 一時的な診断用ファーム（人の指示による。ループの作業項目ではない）
// IO14 の入力レベルを 0.2 秒ごとに表示し、その間のエッジ数（CHANGE 割り込み）も表示する。
// 赤外線は送らない。IRremoteESP8266 は使わない。
#include <Arduino.h>

static const uint8_t kPin = 14;
static const uint32_t kPeriodMs = 200;

static volatile uint32_t g_edges = 0;
static uint32_t g_lastMs = 0;

static void IRAM_ATTR onChange() { g_edges++; }

void setup() {
  Serial.begin(115200);
  pinMode(kPin, INPUT);
  Serial.println();
  Serial.println("pincheck: IO14 level (every 0.2s) and edge count via CHANGE interrupt");
  attachInterrupt(digitalPinToInterrupt(kPin), onChange, CHANGE);
  g_lastMs = millis();
}

void loop() {
  uint32_t now = millis();
  if (now - g_lastMs >= kPeriodMs) {
    g_lastMs = now;
    noInterrupts();
    uint32_t edges = g_edges;
    g_edges = 0;
    interrupts();
    int level = digitalRead(kPin);
    Serial.printf("level=%d edges=%lu\n", level, (unsigned long)edges);
  }
}
