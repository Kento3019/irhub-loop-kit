// 根拠：docs/design/06-runtime.md 8節（F5、HW-PINS）
#include "climate_dht20.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace irhub {

ClimateDht20::ClimateDht20() : dht_(&Wire) {}

void ClimateDht20::begin() {
  // DHT20 は電源投入から 100 ms 以上たってから通信する（setup() の中の1回だけ）
  const uint32_t ms = millis();
  if (ms < 100) delay(100 - ms);
  Wire.begin(pins::kI2cSda, pins::kI2cScl);
  if (!dht_.begin()) {
    Serial.println("[dht20] begin failed");
  }
}

ClimateReading ClimateDht20::read() {
  const uint32_t t0 = millis();
  const auto st = dht_.read();  // 型を決め打ちしない（D-01）
  if (st != DHT20_OK) {
    Serial.printf("[dht20] read error %d\n", static_cast<int>(st));
    return {false, 0.0f, 0.0f};
  }
  const ClimateReading r{true, dht_.getTemperature(), dht_.getHumidity()};
  Serial.printf("[dht20] %.1fC %.1f%% (%lums)\n", r.temperatureC, r.humidityPct,
                static_cast<unsigned long>(millis() - t0));
  return r;
}

}  // namespace irhub
