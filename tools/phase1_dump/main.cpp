// フェーズ1用の受信ダンプ（I-HW1、HW-PINS、C-TECH）
// IRremoteESP8266 の examples/IRrecvDumpV2 相当。純正リモコンの信号を受けてシリアルに出す。
// 受信のみ。赤外線は送らない（N-BOOT）。本体ファーム（env:esp32）には含めない。
// ビルド：pio run -e dump ／ 書き込みは人が行う：pio run -e dump -t upload → pio device monitor -e dump

#include <Arduino.h>
#include <IRac.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRtext.h>
#include <IRutils.h>

namespace {
constexpr uint16_t kRecvPin = 14;              // IO14：受信モジュール OUT（HW-PINS）
constexpr uint32_t kBaudRate = 115200;
constexpr uint16_t kCaptureBufferSize = 1024;  // エアコンの長い信号用
constexpr uint8_t kTimeout = 50;               // エアコンのパケット間の隙間（~40ms）を1通として受ける
constexpr uint16_t kMinUnknownSize = 12;       // 短いノイズを UNKNOWN として出さない
constexpr uint8_t kRecvTolerance = 50;         // 受信の bit mark が 436〜572µs で、基準400µsに40%(240〜560µs)では572µsが外れ HITACHI_AC296 が UNKNOWN になるため50%(200〜600µs)に広げる。space は0=250〜750µs、1=625〜1875µsで実測(362〜514/1032〜1230µs)が収まり重ならない（上限100）

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;
}  // namespace

void setup() {
  Serial.begin(kBaudRate, SERIAL_8N1);
  while (!Serial) delay(50);
  Serial.printf("\n" D_STR_IRRECVDUMP_STARTUP "\n", kRecvPin);
#if DECODE_HASH
  irrecv.setUnknownThreshold(kMinUnknownSize);
#endif
  irrecv.setTolerance(kRecvTolerance);
  irrecv.enableIRIn();
}

void loop() {
  if (!irrecv.decode(&results)) return;
  uint32_t now = millis();
  Serial.printf(D_STR_TIMESTAMP " : %06u.%03u\n", now / 1000, now % 1000);
  if (results.overflow) Serial.printf(D_WARN_BUFFERFULL "\n", kCaptureBufferSize);
  Serial.println(D_STR_LIBRARY "   : v" _IRREMOTEESP8266_VERSION_STR "\n");
  Serial.print(resultToHumanReadableBasic(&results));
  String description = IRAcUtils::resultAcToString(&results);
  if (description.length()) Serial.println(D_STR_MESGDESC ": " + description);
  yield();
  Serial.println(resultToSourceCode(&results));
  Serial.println();
  yield();
}
