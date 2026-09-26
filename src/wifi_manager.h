// Wi-Fi の接続・再接続・全失敗時の再起動（N-WIFI）と IP 固定（N-IP）。
// 根拠：docs/design/01-architecture.md 6a節、docs/design/06-runtime.md 4節・6節
#pragma once
#include <cstdint>

namespace irhub {

constexpr uint8_t kWifiMaxAttempts = 3;           // N-WIFI（決定）：最大3回
constexpr uint32_t kWifiAttemptTimeoutMs = 10000;  // 仮(N-WIFI)：「計30秒程度」を 3回×10秒 とした

// N-IP 方式B（ESP32 側で固定）のときの設定。値は include/secrets.h から来る
struct StaticIpConfig {
  uint8_t ip[4];
  uint8_t gateway[4];
  uint8_t subnet[4];
  uint8_t dns1[4];  // NTP のサーバー名を引くのに必要。通常はルーターのアドレス（= gateway）
};

class WifiManager {
 public:
  WifiManager(const char* ssid, const char* pass);
  // begin() の前に呼ぶ。呼ばなければ DHCP（方式A）
  void setStaticIp(const StaticIpConfig& cfg);
  // setup() で1回。設定をして最初の試行を始める。接続は待たない
  void begin(uint32_t nowMs);
  // loop() から毎回呼ぶ。ブロックしない（D-01 6a 節の表）
  void tick(uint32_t nowMs);
  bool connected() const;  // WiFi.status() == WL_CONNECTED

 private:
  void startAttempt(uint32_t nowMs);  // WiFi.disconnect() → WiFi.begin()、attempts_++、ログ
  const char* ssid_;
  const char* pass_;
  bool useStatic_ = false;
  StaticIpConfig static_{};
  bool trying_ = false;
  uint8_t attempts_ = 0;
  uint32_t attemptMs_ = 0;
};

}  // namespace irhub
