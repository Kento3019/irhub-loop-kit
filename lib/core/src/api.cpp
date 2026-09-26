// ApiRouter の実装。根拠：docs/design/04-api.md（D-04）3〜11節
#include "api.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>
#include <utility>

namespace irhub {

namespace {

ApiResponse jsonOk(std::string body) {
  ApiResponse r;
  r.status = 200;
  r.body = std::move(body);
  return r;
}

ApiResponse jsonError(int status, std::string_view message) {
  JsonDocument doc;
  doc["error"] = std::string(message);
  ApiResponse r;
  r.status = status;
  serializeJson(doc, r.body);
  return r;
}

// 3節の "ac" の形。風向は書かない
void writeAcState(JsonObject o, const AcState& s) {
  o["power"] = s.power;
  o["mode"] = toString(s.mode);
  if (s.hasTemp) {
    o["temp"] = static_cast<int>(s.tempC);
  } else {
    o["temp"] = nullptr;
  }
  o["fan"] = toString(s.fan);
}

// 3節の "acCapabilities"。cap:: だけから作る。風向は書かない
void writeCapabilities(JsonObject o) {
  static const AcMode kModes[kAcModeCount] = {AcMode::Auto, AcMode::Cool, AcMode::Dry, AcMode::Heat};
  JsonArray modes = o["modes"].to<JsonArray>();
  for (AcMode m : kModes) modes.add(toString(m));
  o["tempMin"] = cap::kTempMinC;
  o["tempMax"] = cap::kTempMaxC;
  o["tempStep"] = cap::kTempStepC;
  JsonArray tempModes = o["tempModes"].to<JsonArray>();
  for (AcMode m : kModes)
    if (cap::tempSupported(m)) tempModes.add(toString(m));
  JsonArray fan = o["fan"].to<JsonArray>();
  for (int i = 0; i < cap::kFanChoiceCount; ++i) fan.add(toString(cap::kFanChoices[i]));
}

double round1(float x) { return std::round(static_cast<double>(x) * 10.0) / 10.0; }

// POST /api/ac で受け付けるキー（この順で形を検査する）。風向は入れない
enum AcKey { kKeyPower = 0, kKeyMode, kKeyTemp, kKeyFan, kAcKeyCount };
constexpr const char* kAcKeys[kAcKeyCount] = {"power", "mode", "temp", "fan"};

}  // namespace

bool parseAcPatchJson(std::string_view body, AcPatch* out, std::string* error) {
  auto fail = [error](std::string msg) {
    if (error) *error = std::move(msg);
    return false;
  };
  if (body.size() > kApiSmallBodyMaxBytes) return fail("body too large");
  JsonDocument doc;
  if (deserializeJson(doc, body.data(), body.size())) return fail("invalid json");
  if (!doc.is<JsonObject>()) return fail("invalid json");
  JsonObjectConst obj = doc.as<JsonObjectConst>();

  bool seen[kAcKeyCount] = {false, false, false, false};
  JsonVariantConst val[kAcKeyCount];
  for (JsonPairConst kv : obj) {
    const char* k = kv.key().c_str();
    int idx = -1;
    for (int i = 0; i < kAcKeyCount; ++i) {
      if (std::strcmp(k, kAcKeys[i]) == 0) {
        idx = i;
        break;
      }
    }
    if (idx < 0) return fail(std::string("unknown key \"") + k + "\"");
    seen[idx] = true;
    val[idx] = kv.value();
  }

  AcPatch p;
  if (seen[kKeyPower]) {
    if (!val[kKeyPower].is<bool>()) return fail("power: must be boolean");
    p.power = val[kKeyPower].as<bool>();
  }
  if (seen[kKeyMode]) {
    if (!val[kKeyMode].is<const char*>()) return fail("mode: must be string");
    const auto m = parseAcMode(val[kKeyMode].as<const char*>());
    if (!m) return fail("mode: unknown mode");
    p.mode = *m;
  }
  if (seen[kKeyTemp]) {
    if (!val[kKeyTemp].is<int>()) return fail("temp: must be integer");
    p.tempC = val[kKeyTemp].as<int>();
  }
  if (seen[kKeyFan]) {
    if (!val[kKeyFan].is<const char*>()) return fail("fan: must be string");
    const auto f = parseAcFan(val[kKeyFan].as<const char*>());
    if (!f) return fail("fan: unknown fan");
    p.fan = *f;
  }
  if (out) *out = p;
  return true;
}

ApiRouter::ApiRouter(Hub& hub) : hub_(hub) {}

ApiResponse ApiRouter::handle(const ApiRequest& req) {
  const std::string& path = req.path;
  const HttpMethod m = req.method;
  const auto notAllowed = [] { return jsonError(405, "method not allowed"); };
  if (path == "/api/status") {
    if (m == HttpMethod::Get) return getStatus();
    return notAllowed();
  }
  if (path == "/api/ac") {
    if (m == HttpMethod::Post) return postAc(req.body);
    return notAllowed();
  }
  if (path == "/api/schedules") {
    if (m == HttpMethod::Get) return getSchedules();
    if (m == HttpMethod::Put) return putSchedules(req.body);
    return notAllowed();
  }
  if (path == "/api/schedules/export") {
    if (m == HttpMethod::Get) return getExport();
    return notAllowed();
  }
  if (path == "/api/schedules/import") {
    if (m == HttpMethod::Post) return postImport(req.body);
    return notAllowed();
  }
  return jsonError(404, "not found");
}

ApiResponse ApiRouter::getStatus() {
  const AcState& s = hub_.acState();
  const ClimateReading c = hub_.lastClimate();
  const ClockReading now = hub_.clockNow();

  JsonDocument doc;
  writeAcState(doc["ac"].to<JsonObject>(), s);
  writeCapabilities(doc["acCapabilities"].to<JsonObject>());
  JsonObject climate = doc["climate"].to<JsonObject>();
  climate["valid"] = c.valid;
  if (c.valid) {
    climate["temperature"] = round1(c.temperatureC);
    climate["humidity"] = round1(c.humidityPct);
  } else {
    climate["temperature"] = nullptr;
    climate["humidity"] = nullptr;
  }
  JsonObject clock = doc["clock"].to<JsonObject>();
  clock["synced"] = now.synced;
  if (now.synced) {
    clock["now"] = formatJstIso(now.local);
  } else {
    clock["now"] = nullptr;
  }
  std::string out;
  serializeJson(doc, out);
  return jsonOk(std::move(out));
}

ApiResponse ApiRouter::postAc(const std::string& body) {
  AcPatch patch;
  std::string err;
  if (!parseAcPatchJson(body, &patch, &err)) return jsonError(400, err);
  if (!hub_.applyAc(patch, &err)) return jsonError(400, err);  // D-02 の errorMessage
  JsonDocument doc;
  writeAcState(doc["ac"].to<JsonObject>(), hub_.acState());
  std::string out;
  serializeJson(doc, out);
  return jsonOk(std::move(out));
}

ApiResponse ApiRouter::getSchedules() {
  return jsonOk(schedulesToJson(hub_.schedules(), ScheduleJsonKind::List, hub_.clockNow()));
}

ApiResponse ApiRouter::putSchedules(const std::string& body) {
  return replaceFrom(body, ScheduleJsonKind::List);
}

ApiResponse ApiRouter::getExport() {
  const ClockReading now = hub_.clockNow();  // 本文とファイル名で同じ時刻を使う
  ApiResponse r = jsonOk(schedulesToJson(hub_.schedules(), ScheduleJsonKind::Export, now));
  r.downloadFilename = exportFilename(now);
  return r;
}

ApiResponse ApiRouter::postImport(const std::string& body) {
  return replaceFrom(body, ScheduleJsonKind::Export);
}

ApiResponse ApiRouter::replaceFrom(const std::string& body, ScheduleJsonKind kind) {
  ScheduleParseResult r;
  if (!schedulesFromJson(body, kind, &r)) return jsonError(400, r.error);
  int bad = -1;
  const ScheduleError e = hub_.replaceSchedules(r.items, r.count, &bad);
  if (e != ScheduleError::None) {
    std::string msg = errorMessage(e);
    if (bad >= 0) msg = "schedules[" + std::to_string(bad) + "]: " + msg;
    return jsonError(400, msg);
  }
  return jsonOk(schedulesToJson(hub_.schedules(), ScheduleJsonKind::List, hub_.clockNow()));
}

}  // namespace irhub
