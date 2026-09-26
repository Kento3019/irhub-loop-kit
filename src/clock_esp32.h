// IClock の ESP32 実装。NTP＋JST、synced の判定（N-TIME）。根拠：docs/design/06-runtime.md 5節
#pragma once
#include <sys/time.h>

#include <atomic>
#include <cstdint>

#include "ports.h"

namespace irhub {

constexpr const char* kTzJst = "JST-9";  // POSIX の TZ 表記で UTC+9、夏時間なし
constexpr const char* kNtpServer1 = "ntp.nict.jp";  // 推測（D-06 要件への疑問 2）
constexpr const char* kNtpServer2 = "time.google.com";
constexpr const char* kNtpServer3 = "pool.ntp.org";
constexpr int64_t kMinValidEpochSec = 1577836800;  // 2020-01-01T00:00:00Z。これより前は「取れていない」扱い

class ClockEsp32 : public IClock {
 public:
  // Wi-Fi が初めてつながった後に loop() から毎回呼ばれる。最初の1回だけ
  // sntp_set_time_sync_notification_cb(&onSync) → configTzTime(kTzJst, 3台) を行う。2回目以降は何もしない。
  void startNtp();
  ClockReading now() override;  // ブロックしない（getLocalTime() を使わない）

 private:
  static void onSync(struct timeval* tv);  // lwIP のタスクから呼ばれる。synced_ を true にするだけ
  static std::atomic<bool> synced_;        // 一度 true になったら false に戻さない
  bool started_ = false;
};

}  // namespace irhub
