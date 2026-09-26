// WebServer と ApiRouter の橋渡し。判断を持たない。根拠：docs/design/04-api.md 12節、docs/design/05-ui.md 10節
#pragma once
#include <WebServer.h>

#include "api.h"

namespace irhub {

class WebBridge {
 public:
  WebBridge(WebServer& server, ApiRouter& router);
  void begin();  // on("/", HTTP_GET, ...) と onNotFound(...) を登録して server.begin()

 private:
  void handleRoot();  // 埋め込んだ画面を返す
  void handleApi();   // onNotFound から呼ぶ。ApiRequest に詰めて ApiRouter::handle
  WebServer& server_;
  ApiRouter& router_;
};

}  // namespace irhub
