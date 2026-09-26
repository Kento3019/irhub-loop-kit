// IClimateSensor の DHT20 実装（F5、HW-PINS）。根拠：docs/design/06-runtime.md 8節
#pragma once
#include <DHT20.h>

#include "ports.h"

namespace irhub {

class ClimateDht20 : public IClimateSensor {
 public:
  ClimateDht20();                  // dht_(&Wire)。I2C には触らない
  void begin();                    // setup() で1回。Wire.begin(21, 22) → DHT20::begin()
  ClimateReading read() override;  // 周期の判断はしない（Hub::tick が 30 秒ごとに呼ぶ）

 private:
  DHT20 dht_;
};

}  // namespace irhub
