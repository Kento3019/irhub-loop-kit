// 根拠：docs/design/04-api.md 12節、docs/design/05-ui.md 10節
#include "web_bridge.h"

#include "generated/index_html.h"

namespace irhub {

WebBridge::WebBridge(WebServer& server, ApiRouter& router) : server_(server), router_(router) {}

void WebBridge::begin() {
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.addHandler(new ApiCatchAllHandler(*this));  // 所有権は WebServer に移る（1回だけ）
  server_.begin();
}

void WebBridge::handleRoot() {
  server_.sendHeader("Cache-Control", "no-cache");  // 書き込み後に古い画面を見せない
  server_.send_P(200, "text/html; charset=utf-8", kIndexHtml, kIndexHtmlLen);
}

void WebBridge::handleApi() {
  ApiRequest req;
  switch (server_.method()) {
    case HTTP_GET:  req.method = HttpMethod::Get;  break;
    case HTTP_POST: req.method = HttpMethod::Post; break;
    case HTTP_PUT:  req.method = HttpMethod::Put;  break;
    default:        req.method = HttpMethod::Other; break;
  }
  req.path = server_.uri().c_str();  // クエリは含まれない
  if (server_.hasArg("plain")) req.body = server_.arg("plain").c_str();
  const ApiResponse res = router_.handle(req);
  if (!res.downloadFilename.empty()) {
    String cd = "attachment; filename=\"";
    cd += res.downloadFilename.c_str();
    cd += "\"";
    server_.sendHeader("Content-Disposition", cd);
  }
  server_.send(res.status, res.contentType.c_str(), res.body.c_str());
}

}  // namespace irhub
