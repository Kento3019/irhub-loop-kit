// HTTP 非依存の API ハンドラ（(method, path, body) → (status, body)）
// 根拠：docs/design/04-api.md（D-04）11節、docs/design/01-architecture.md 6節
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "hub.h"
#include "schedule_json.h"  // ScheduleJsonKind・schedulesToJson・schedulesFromJson・exportFilename・formatJstIso

namespace irhub {

enum class HttpMethod : uint8_t { Get, Post, Put, Other };

struct ApiRequest {
  HttpMethod method;
  std::string path;  // クエリ文字列を除いたパス。例 "/api/ac"
  std::string body;  // 本文（JSON 文字列）。GET は空
};

struct ApiResponse {
  int status = 200;
  std::string contentType = "application/json";
  std::string body;
  std::string downloadFilename;
};

constexpr size_t kApiSmallBodyMaxBytes = 512;  // /api/ac の本文の上限（推測）

// POST /api/ac の本文 → AcPatch。形・型・文字列まで確かめる（値の範囲・選択肢は AcModel::validate）。
// 受け付けるキーは power・mode・temp・fan だけ。out->swingV・out->swingH は埋めない（常に空）。
bool parseAcPatchJson(std::string_view body, AcPatch* out, std::string* error);

class ApiRouter {
 public:
  explicit ApiRouter(Hub& hub);
  ApiResponse handle(const ApiRequest& req);  // 無いパスは 404、メソッド違いは 405

 private:
  ApiResponse getStatus();
  ApiResponse postAc(const std::string& body);
  ApiResponse getSchedules();
  ApiResponse putSchedules(const std::string& body);
  ApiResponse getExport();
  ApiResponse postImport(const std::string& body);
  ApiResponse replaceFrom(const std::string& body, ScheduleJsonKind kind);  // put と import の共通部分
  Hub& hub_;
};

}  // namespace irhub
