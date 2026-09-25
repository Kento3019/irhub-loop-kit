// フェーズ0：ESP32 単体の動作確認用。I-05 でファームウェア本体に置き換わる。
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
}

void loop() {
  static unsigned long n = 0;
  Serial.printf("irhub phase0 tick %lu\n", n++);
  delay(1000);
}
