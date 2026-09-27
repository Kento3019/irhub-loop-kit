// IIrSender の ESP32 実装（HW-PINS、N-BOOT）。根拠：docs/design/06-runtime.md 2.4
// I-06：AcState を stdAc::state_t に変換して IRac で送る。public の形は I-05 と同じ。
#pragma once
#include <IRac.h>

#include "ports.h"  // IIrSender、AcState（D-01）

namespace irhub {

class IrSenderEsp32 : public IIrSender {
 public:
  // ピンにも赤外線にも触らない（グローバル変数として生成されるため。N-BOOT）。
  // 送信ピンは pins::kIrSend（IO4）を .cpp の中で使い、引数では受け取らない。
  IrSenderEsp32();
  // setup() で1回だけ呼ぶ。送信ピンを出力・LOW にするだけで、何も送らない（N-BOOT）。
  void begin();
  // D-01 の IIrSender。begin() の後にだけ呼ばれる（呼ぶのは Hub だけ）。
  bool sendAc(const AcState& state) override;

 private:
  IRac ac_;  // コンストラクタは値を覚えるだけでピンに触らない（N-BOOT）
};

}  // namespace irhub
