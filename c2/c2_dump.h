#pragma once
// Small status page for the cube's web server:
//   /chat  -> plain-text report of what the family chat / ntfy polling is doing
#include <string>
#include <cstring>
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/wifi/wifi_component.h"

namespace assistant {

using esphome::web_server_idf::AsyncWebServerRequest;

// chat diagnostics, filled in by the poll/send scripts in smalltv-c2.yaml
inline int diagPollStatus = 0, diagSendStatus = 0;
inline uint32_t diagPolls = 0, diagMsgs = 0, diagBytes = 0;

inline void diagPoll(int status, const std::string& body) {
  diagPolls++;
  diagPollStatus = status;
  diagBytes = body.size();
  for (size_t p = body.find("\"event\":\"message\""); p != std::string::npos;
       p = body.find("\"event\":\"message\"", p + 1))
    diagMsgs++;
}

// Request path check using the raw ESP-IDF request (uri includes any ?query)
inline bool isPath(AsyncWebServerRequest* r, const char* p) {
  const char* uri = static_cast<httpd_req_t*>(*r)->uri;
  size_t n = strlen(p);
  return strncmp(uri, p, n) == 0 && (uri[n] == 0 || uri[n] == '?');
}

inline std::string hideCode(const std::string& s) {
  // show only the start of codes so the page is safe to screenshot
  return s.size() <= 6 ? s : s.substr(0, 6) + "..." + std::to_string(s.size()) + " chars";
}

class DumpHandler : public esphome::web_server_idf::AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest* request) const { return isPath(request, "/chat"); }
  bool canHandle(AsyncWebServerRequest* request) {
    return static_cast<const DumpHandler*>(this)->canHandle(request);
  }

  void handleRequest(AsyncWebServerRequest* request) {
    std::string o;
    o += "wifi connected: " + std::string(esphome::wifi::global_wifi_component->is_connected() ? "yes" : "NO") + "\n";
    o += "clock set: " + std::string(nowEpoch ? "yes" : "NO (no internet time yet)") + "\n";
    o += "cube name: " + cfg.name + "\n";
    o += "family code: " + (cfg.familyTopic.empty() ? std::string("(empty)") : hideCode(cfg.familyTopic)) + "\n";
    o += "own topic: " + (cfg.topic.empty() ? std::string("(empty)") : hideCode(cfg.topic)) + "\n";
    o += "server: " + serverBase() + "\n";
    o += "ready to check: " + std::string(readyToPoll() ? "yes" : "NO") + "\n";
    o += "checks done: " + std::to_string(diagPolls) + "\n";
    o += "last check result: " + std::to_string(diagPollStatus) + " (200 = good, -1 = could not connect)\n";
    o += "last reply size: " + std::to_string(diagBytes) + " bytes\n";
    o += "messages received: " + std::to_string(diagMsgs) + "\n";
    o += "last send result: " + std::to_string(diagSendStatus) + "\n";
    request->send(200, "text/plain", o.c_str());
  }
};

inline void installDumpHandler() {
  esphome::web_server_base::global_web_server_base->add_handler(new DumpHandler());
}

}  // namespace assistant
