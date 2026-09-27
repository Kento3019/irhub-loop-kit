// WebServer と ApiRouter の橋渡し。判断を持たない。根拠：docs/design/04-api.md 12節、docs/design/05-ui.md 10節
#pragma once
#include <WebServer.h>

#include "api.h"

namespace irhub {

class WebBridge {
 public:
  WebBridge(WebServer& server, ApiRouter& router);
  void begin();      // on("/", HTTP_GET, ...) と addHandler(new ApiCatchAllHandler(*this)) を登録して server.begin()
  void handleApi();  // ApiCatchAllHandler::handle から呼ぶ。ApiRequest に詰めて ApiRouter::handle

 private:
  void handleRoot();  // 埋め込んだ画面を返す
  WebServer& server_;
  ApiRouter& router_;
};

// 全要求受けの handler。「GET かつ uri == "/"」以外のすべてを受けて WebBridge::handleApi() に渡す。
// canUpload・canRaw は既定の false のまま（本文は arg("plain") に入る）。
// 所有権は addHandler で WebServer に移る（~WebServer() が delete する）。
class ApiCatchAllHandler : public RequestHandler {
 public:
  explicit ApiCatchAllHandler(WebBridge& bridge) : bridge_(bridge) {}
  bool canHandle(HTTPMethod method, String uri) override {
    return !(method == HTTP_GET && uri == "/");
  }
  bool handle(WebServer& server, HTTPMethod requestMethod, String requestUri) override {
    (void)server;
    (void)requestMethod;
    (void)requestUri;  // 中身は WebBridge が server_ から読む
    bridge_.handleApi();
    return true;  // 応答は必ず送る（404／405 も ApiRouter が返す）
  }

 private:
  WebBridge& bridge_;
};

}  // namespace irhub
