// IRハブ本体ファームウェア。実体の生成と組み立て、setup()／loop()。
// 根拠：docs/design/06-runtime.md 2節・3節、docs/design/01-architecture.md 7節・10節
// REQ: N-BOOT N-WIFI N-TIME N-SECRET N-IP N-RESP F5 HW-PINS C-TECH
#include <Arduino.h>
#include <WebServer.h>
#include <esp_system.h>
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h がありません。include/secrets.h.example をコピーして作ってください（docs/design/06-runtime.md 7節）"
#endif
#include "api.h"
#include "climate_dht20.h"
#include "clock_esp32.h"
#include "core_version.h"
#include "hub.h"
#include "ir_sender_esp32.h"
#include "pins.h"
#include "web_bridge.h"
#include "wifi_manager.h"

namespace {
// 生成順は宣言順。どのコンストラクタもハードウェアに触らず、赤外線も送らない（N-BOOT）
WebServer server(80);
irhub::IrSenderEsp32 ir;  // コンストラクタはピンを触らない
irhub::ClockEsp32 clk;
irhub::ClimateDht20 sensor;
irhub::Hub hub(ir, clk, sensor);  // 送らない（D-01 10節）
irhub::ApiRouter router(hub);
irhub::WebBridge web(server, router);
irhub::WifiManager wifi(irhub::secrets::kWifiSsid, irhub::secrets::kWifiPass);
}  // namespace

void setup() {
  Serial.begin(115200);  // C-TECH：monitor_speed = 115200
  Serial.printf("[boot] irhub %s reset=%d\n", irhub::coreVersion(), static_cast<int>(esp_reset_reason()));
  ir.begin();      // IO23 を出力・LOW にする。送信しない（N-BOOT）
  sensor.begin();  // Wire.begin(21, 22) → DHT20::begin()。失敗しても進む
  if (irhub::secrets::kUseStaticIp) {  // N-IP 方式B のときだけ
    wifi.setStaticIp(irhub::secrets::kStaticIp);
  }
  wifi.begin(millis());  // Wi-Fi 接続を始める（待たない）
  web.begin();           // ハンドラ登録と server.begin()
  Serial.println("[boot] setup done");
  // setup() では赤外線を送らない（N-BOOT）
}

void loop() {
  const uint32_t nowMs = millis();
  wifi.tick(nowMs);                      // Wi-Fi の見張り。ブロックしない
  if (wifi.connected()) clk.startNtp();  // 初めて接続したときに NTP を始める。2回目以降は何もしない
  server.handleClient();                 // HTTP を1件処理
  hub.tick(nowMs);                       // センサー（30秒）とスケジュール（1秒）の判定
  delay(1);                              // 他のタスク（Wi-Fi・lwIP）に CPU を譲る
}
