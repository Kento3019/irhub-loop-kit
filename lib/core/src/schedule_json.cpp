// スケジュール ⇔ JSON の実装。根拠：docs/design/03-schedule.md 6節
// 文字列 ⇔ 列挙は ac_state の parseAcMode・parseAcFan・toString だけを使う
#include "schedule_json.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace irhub {
namespace {

const char* const kDayNames[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

// JSON 文書 → std::string（ARDUINOJSON_ENABLE_STD_STRING に頼らない）
std::string toJsonString(const JsonDocument& doc) {
  const size_t n = measureJson(doc);
  std::string out(n + 1, '\0');
  serializeJson(doc, &out[0], n + 1);
  out.resize(n);
  return out;
}

// オブジェクトの中のキーを探す（null の値と「キーが無い」を区別するため）
bool findKey(JsonObjectConst o, const char* key, JsonVariantConst* out) {
  for (JsonPairConst kv : o) {
    if (std::strcmp(kv.key().c_str(), key) == 0) {
      *out = kv.value();
      return true;
    }
  }
  return false;
}

// 表に無いキー（本文の中で最初のもの）を返す。無ければ nullptr
const char* firstUnknownKey(JsonObjectConst o, const char* const* allowed, int n) {
  for (JsonPairConst kv : o) {
    const char* k = kv.key().c_str();
    bool ok = false;
    for (int i = 0; i < n; ++i) {
      if (std::strcmp(k, allowed[i]) == 0) {
        ok = true;
        break;
      }
    }
    if (!ok) return k;
  }
  return nullptr;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// "HH:MM"（5文字、00..23 と 00..59）
bool parseHhMm(const char* s, int8_t* hour, int8_t* minute) {
  if (std::strlen(s) != 5) return false;
  if (!isDigit(s[0]) || !isDigit(s[1]) || s[2] != ':' || !isDigit(s[3]) || !isDigit(s[4])) {
    return false;
  }
  const int h = (s[0] - '0') * 10 + (s[1] - '0');
  const int m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h > 23 || m > 59) return false;
  *hour = static_cast<int8_t>(h);
  *minute = static_cast<int8_t>(m);
  return true;
}

class ItemParser {
 public:
  ItemParser(int index, std::string* err) : err_(err) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "schedules[%d]", index);
    prefix_ = buf;
  }

  bool parse(JsonVariantConst v, Schedule* s) {
    if (!v.is<JsonObjectConst>()) return failItem("not an object");
    const JsonObjectConst o = v.as<JsonObjectConst>();
    static const char* const kItemKeys[] = {"id", "enabled", "time", "days", "target", "action"};
    if (const char* k = firstUnknownKey(o, kItemKeys, 6)) return failUnknown("", k);
    *s = Schedule{};
    JsonVariantConst x;
    // id
    if (findKey(o, "id", &x)) {
      if (x.is<int>()) {
        const int id = x.as<int>();
        if (id < 1 || id > static_cast<int>(kScheduleIdMax)) return failKey("id", "out of range");
        s->id = static_cast<uint16_t>(id);
      } else if (x.is<long long>()) {
        return failKey("id", "out of range");
      } else {
        return failKey("id", "must be integer");
      }
    }
    // enabled
    if (!findKey(o, "enabled", &x)) return failKey("enabled", "missing");
    if (!x.is<bool>()) return failKey("enabled", "must be boolean");
    s->enabled = x.as<bool>();
    // time
    if (!findKey(o, "time", &x)) return failKey("time", "missing");
    if (!x.is<const char*>()) return failKey("time", "must be string");
    if (!parseHhMm(x.as<const char*>(), &s->hour, &s->minute)) return failKey("time", "must be HH:MM");
    // days
    if (!findKey(o, "days", &x)) return failKey("days", "missing");
    if (!x.is<JsonArrayConst>()) return failKey("days", "must be array");
    if (!parseDays(x.as<JsonArrayConst>(), &s->days)) return false;
    // target
    if (!findKey(o, "target", &x)) return failKey("target", "missing");
    if (!x.is<const char*>()) return failKey("target", "must be string");
    if (std::strcmp(x.as<const char*>(), "ac") != 0) return failKey("target", "unknown target");
    s->target = ScheduleTarget::Ac;
    // action
    if (!findKey(o, "action", &x)) return failKey("action", "missing");
    if (!x.is<JsonObjectConst>()) return failKey("action", "must be object");
    if (!parseAction(x.as<JsonObjectConst>(), &s->ac)) return false;
    // 1.1 の検証
    const ScheduleError e = validateSchedule(*s);
    if (e != ScheduleError::None) return failItem(errorMessage(e));
    return true;
  }

 private:
  bool parseDays(JsonArrayConst a, uint8_t* days) {
    if (a.size() == 0) return failKey("days", "empty");
    uint8_t bits = 0;
    for (JsonVariantConst d : a) {
      if (!d.is<const char*>()) return failKey("days", "must be string");
      const char* name = d.as<const char*>();
      int w = -1;
      for (int i = 0; i < 7; ++i) {
        if (std::strcmp(name, kDayNames[i]) == 0) {
          w = i;
          break;
        }
      }
      if (w < 0) {
        std::string why = "unknown day \"";
        why += name;
        why += "\"";
        return failKey("days", why.c_str());
      }
      if (bits & kDayBit(w)) return failKey("days", "duplicate day");
      bits = static_cast<uint8_t>(bits | kDayBit(w));
    }
    *days = bits;
    return true;
  }

  bool parseAction(JsonObjectConst o, AcPatch* p) {
    static const char* const kActionKeys[] = {"power", "mode", "temp", "fan"};
    if (const char* k = firstUnknownKey(o, kActionKeys, 4)) return failUnknown(".action", k);
    *p = AcPatch{};
    JsonVariantConst x;
    if (!findKey(o, "power", &x)) return failKey("action.power", "missing");
    if (!x.is<bool>()) return failKey("action.power", "must be boolean");
    p->power = x.as<bool>();
    if (findKey(o, "mode", &x)) {
      if (!x.is<const char*>()) return failKey("action.mode", "must be string");
      const std::optional<AcMode> m = parseAcMode(x.as<const char*>());
      if (!m) return failKey("action.mode", "unknown mode");
      p->mode = *m;
    }
    if (findKey(o, "temp", &x)) {
      if (!x.is<int>()) return failKey("action.temp", "must be integer");
      p->tempC = x.as<int>();
    }
    if (findKey(o, "fan", &x)) {
      if (!x.is<const char*>()) return failKey("action.fan", "must be string");
      const std::optional<AcFan> f = parseAcFan(x.as<const char*>());
      if (!f) return failKey("action.fan", "unknown fan");
      p->fan = *f;
    }
    return true;
  }

  // "schedules[i]: <why>"
  bool failItem(const char* why) {
    *err_ = prefix_ + ": " + why;
    return false;
  }
  // "schedules[i].<key>: <why>"
  bool failKey(const char* key, const char* why) {
    *err_ = prefix_ + "." + key + ": " + why;
    return false;
  }
  // "schedules[i]<where>: unknown key \"k\""
  bool failUnknown(const char* where, const char* key) {
    *err_ = prefix_ + where + ": unknown key \"" + key + "\"";
    return false;
  }

  std::string prefix_;
  std::string* err_;
};

void writeItem(JsonObject o, const Schedule& s) {
  o["id"] = s.id;
  o["enabled"] = s.enabled;
  char time[16];
  std::snprintf(time, sizeof time, "%02d:%02d", static_cast<int>(s.hour), static_cast<int>(s.minute));
  o["time"] = static_cast<const char*>(time);  // const char* として渡してコピーさせる
  JsonArray days = o["days"].to<JsonArray>();
  for (int w = 0; w < 7; ++w) {
    if (s.days & kDayBit(w)) days.add(kDayNames[w]);
  }
  o["target"] = "ac";
  JsonObject a = o["action"].to<JsonObject>();
  if (s.ac.power) a["power"] = *s.ac.power;
  if (s.ac.mode) a["mode"] = toString(*s.ac.mode);
  if (s.ac.tempC) a["temp"] = *s.ac.tempC;
  if (s.ac.fan) a["fan"] = toString(*s.ac.fan);
}

}  // namespace

std::string formatJstIso(const LocalTime& t) {
  char buf[80];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d+09:00", static_cast<int>(t.year),
                static_cast<int>(t.month), static_cast<int>(t.day), static_cast<int>(t.hour),
                static_cast<int>(t.minute), static_cast<int>(t.second));
  return std::string(buf);
}

std::string exportFilename(const ClockReading& now) {
  if (!now.synced) return "irhub-schedules.json";
  const LocalTime& t = now.local;
  char buf[80];
  std::snprintf(buf, sizeof buf, "irhub-schedules-%04d%02d%02d-%02d%02d.json",
                static_cast<int>(t.year), static_cast<int>(t.month), static_cast<int>(t.day),
                static_cast<int>(t.hour), static_cast<int>(t.minute));
  return std::string(buf);
}

std::string schedulesToJson(const ScheduleList& list, ScheduleJsonKind kind, const ClockReading& now) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  if (kind == ScheduleJsonKind::Export) {
    root["version"] = kScheduleExportVersion;
    if (now.synced) {
      const std::string at = formatJstIso(now.local);
      root["exportedAt"] = at.c_str();  // const char* はコピーされる
    } else {
      root["exportedAt"] = nullptr;
    }
  } else {
    root["max"] = kScheduleMax;
  }
  JsonArray arr = root["schedules"].to<JsonArray>();
  for (int i = 0; i < list.size(); ++i) writeItem(arr.add<JsonObject>(), list.at(i));
  return toJsonString(doc);
}

bool schedulesFromJson(std::string_view body, ScheduleJsonKind kind, ScheduleParseResult* out) {
  out->count = 0;
  out->error.clear();
  // 1. 本文の長さ
  if (body.size() > kScheduleJsonMaxBytes) {
    out->error = "body too large";
    return false;
  }
  // 2. JSON として読める、一番外側がオブジェクト
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body.data(), body.size());
  if (err || !doc.is<JsonObjectConst>()) {
    out->error = "invalid json";
    return false;
  }
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  JsonVariantConst x;
  // 3・4. version（エクスポートのときだけ）
  if (kind == ScheduleJsonKind::Export) {
    if (!findKey(root, "version", &x)) {
      out->error = "version missing";
      return false;
    }
    if (!x.is<int>() || x.as<int>() != kScheduleExportVersion) {
      out->error = "unsupported version";
      return false;
    }
  }
  // 5. schedules
  if (!findKey(root, "schedules", &x) || !x.is<JsonArrayConst>()) {
    out->error = "schedules missing";
    return false;
  }
  const JsonArrayConst arr = x.as<JsonArrayConst>();
  // 6. 件数
  if (arr.size() > static_cast<size_t>(kScheduleMax)) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "too many schedules (max %d)", kScheduleMax);
    out->error = buf;
    return false;
  }
  // 7・8. 件ごとに形・型 → validateSchedule
  int i = 0;
  for (JsonVariantConst v : arr) {
    ItemParser p(i, &out->error);
    if (!p.parse(v, &out->items[i])) {
      out->count = 0;
      return false;
    }
    ++i;
  }
  out->count = i;
  return true;
}

}  // namespace irhub
