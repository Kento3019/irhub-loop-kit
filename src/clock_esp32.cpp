// 根拠：docs/design/06-runtime.md 5節（N-TIME）
#include "clock_esp32.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <time.h>

namespace irhub {

std::atomic<bool> ClockEsp32::synced_{false};

void ClockEsp32::onSync(struct timeval* tv) {
  synced_.store(true);
  Serial.printf("[ntp] synced epoch=%lld\n", static_cast<long long>(tv ? tv->tv_sec : 0));
}

void ClockEsp32::startNtp() {
  if (started_) return;
  started_ = true;
  sntp_set_time_sync_notification_cb(&ClockEsp32::onSync);
  configTzTime(kTzJst, kNtpServer1, kNtpServer2, kNtpServer3);
  Serial.println("[ntp] start");
}

ClockReading ClockEsp32::now() {
  ClockReading r{false, {}, 0};
  if (!synced_.load()) return r;
  const time_t t = time(nullptr);
  if (static_cast<int64_t>(t) < kMinValidEpochSec) return r;
  struct tm tm;
  localtime_r(&t, &tm);  // TZ=JST-9 により JST
  r.synced = true;
  r.epochSec = static_cast<int64_t>(t);
  r.local = LocalTime{static_cast<int16_t>(tm.tm_year + 1900), static_cast<int8_t>(tm.tm_mon + 1),
                      static_cast<int8_t>(tm.tm_mday),        static_cast<int8_t>(tm.tm_hour),
                      static_cast<int8_t>(tm.tm_min),         static_cast<int8_t>(tm.tm_sec),
                      static_cast<int8_t>(tm.tm_wday)};
  return r;
}

}  // namespace irhub
