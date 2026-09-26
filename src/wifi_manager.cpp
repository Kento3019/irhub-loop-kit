// 根拠：docs/design/01-architecture.md 6a節、docs/design/06-runtime.md 4節（N-WIFI）・6節（N-IP）
#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>

namespace irhub {

namespace {
IPAddress toIp(const uint8_t a[4]) { return IPAddress(a[0], a[1], a[2], a[3]); }
}  // namespace

WifiManager::WifiManager(const char* ssid, const char* pass) : ssid_(ssid), pass_(pass) {}

void WifiManager::setStaticIp(const StaticIpConfig& cfg) {
  static_ = cfg;
  useStatic_ = true;
}

void WifiManager::begin(uint32_t nowMs) {
  WiFi.persistent(false);        // 接続情報を毎回フラッシュへ書かない
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // 再接続は WifiManager だけが行う（D-06 4.2）
  WiFi.setSleep(false);          // 受信の遅れを避ける（N-RESP、D-06 3.3）
  if (useStatic_) {              // N-IP 方式B
    const bool ok = WiFi.config(toIp(static_.ip), toIp(static_.gateway), toIp(static_.subnet),
                                toIp(static_.dns1));
    Serial.printf("[wifi] static ip %s\n", ok ? "set" : "FAILED");  // 失敗してもそのまま begin
  }
  Serial.printf("[wifi] mac %s\n", WiFi.macAddress().c_str());  // 方式A の登録に使う
  attempts_ = 0;
  startAttempt(nowMs);  // 最初の接続も1回目の試行
}

void WifiManager::startAttempt(uint32_t nowMs) {
  WiFi.disconnect();
  WiFi.begin(ssid_, pass_);
  trying_ = true;
  ++attempts_;
  attemptMs_ = nowMs;
  Serial.printf("[wifi] attempt %u/%u at %lums\n", static_cast<unsigned>(attempts_),
                static_cast<unsigned>(kWifiMaxAttempts), static_cast<unsigned long>(nowMs));
}

void WifiManager::tick(uint32_t nowMs) {
  const bool up = connected();
  if (!trying_) {
    if (up) return;
    Serial.printf("[wifi] lost at %lums\n", static_cast<unsigned long>(nowMs));
    attempts_ = 0;
    startAttempt(nowMs);  // attempts_=1
    return;
  }
  if (up) {
    trying_ = false;
    attempts_ = 0;
    Serial.printf("[wifi] connected ip=%s at %lums\n", WiFi.localIP().toString().c_str(),
                  static_cast<unsigned long>(nowMs));
    return;
  }
  if (static_cast<uint32_t>(nowMs - attemptMs_) < kWifiAttemptTimeoutMs) return;
  if (attempts_ < kWifiMaxAttempts) {
    startAttempt(nowMs);
    return;
  }
  Serial.printf("[wifi] giving up, restart at %lums\n", static_cast<unsigned long>(nowMs));
  Serial.flush();
  ESP.restart();
}

bool WifiManager::connected() const { return WiFi.status() == WL_CONNECTED; }

}  // namespace irhub
