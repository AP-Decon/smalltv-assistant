#pragma once
// Read-only flash reader for the ESP32-C2 cube's web server.
//   /parts                      -> JSON list of flash partitions (+ app name/version)
//   /dump?p=<label>&off=N&len=M -> hex of M bytes (max 4096) from a partition
// Used to recover the original SDC2 firmware from the spare update slot.
#include <string>
#include <cstdio>
#include <cstring>
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esphome/components/web_server_base/web_server_base.h"

namespace assistant {

using esphome::web_server_idf::AsyncWebServerRequest;

inline std::string printableAt(const uint8_t* b, size_t n) {
  std::string s;
  for (size_t i = 0; i < n && b[i]; i++) s += (b[i] >= 32 && b[i] < 127) ? (char) b[i] : '?';
  return s;
}

class DumpHandler : public esphome::web_server_idf::AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest* request) const {
    std::string u(request->url().c_str());
    return u == "/parts" || u == "/dump";
  }
  bool canHandle(AsyncWebServerRequest* request) {
    return static_cast<const DumpHandler*>(this)->canHandle(request);
  }

  void handleRequest(AsyncWebServerRequest* request) {
    std::string u(request->url().c_str());
    if (u == "/parts") {
      const esp_partition_t* running = esp_ota_get_running_partition();
      std::string out = "[";
      esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
      bool first = true;
      while (it) {
        const esp_partition_t* p = esp_partition_get(it);
        char buf[200];
        snprintf(buf, sizeof(buf), "%s{\"label\":\"%s\",\"type\":%d,\"sub\":%d,\"addr\":%lu,\"size\":%lu,\"running\":%s",
                 first ? "" : ",", p->label, (int) p->type, (int) p->subtype, (unsigned long) p->address,
                 (unsigned long) p->size, p == running ? "true" : "false");
        out += buf;
        if (p->type == ESP_PARTITION_TYPE_APP) {
          uint8_t hdr[256];
          if (esp_partition_read(p, 0, hdr, sizeof(hdr)) == ESP_OK) {
            // esp_app_desc_t follows the 24-byte image header + 8-byte segment header
            const uint8_t* d = hdr + 32;
            uint32_t magic;
            memcpy(&magic, d, 4);
            out += ",\"magic\":" + std::to_string((int) hdr[0]);
            if (magic == 0xABCD5432) {
              out += ",\"version\":\"" + printableAt(d + 16, 32) + "\"";
              out += ",\"project\":\"" + printableAt(d + 48, 32) + "\"";
            }
          }
        }
        out += "}";
        first = false;
        it = esp_partition_next(it);
      }
      out += "]";
      request->send(200, "application/json", out.c_str());
      return;
    }
    // /dump
    std::string label = request->arg("p");
    long off = atol(request->arg("off").c_str());
    long len = atol(request->arg("len").c_str());
    if (len <= 0 || len > 4096) len = 4096;
    const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, label.c_str());
    if (!p || off < 0 || (unsigned long) off >= p->size) { request->send(404, "text/plain", "no such partition/offset"); return; }
    if ((unsigned long) (off + len) > p->size) len = p->size - off;
    static uint8_t buf[4096];
    if (esp_partition_read(p, off, buf, len) != ESP_OK) { request->send(500, "text/plain", "read failed"); return; }
    static char hex[8193];
    static const char* H = "0123456789abcdef";
    for (long i = 0; i < len; i++) { hex[2 * i] = H[buf[i] >> 4]; hex[2 * i + 1] = H[buf[i] & 15]; }
    hex[2 * len] = 0;
    request->send(200, "text/plain", hex);
  }
};

inline void installDumpHandler() {
  if (esphome::web_server_base::global_web_server_base)
    esphome::web_server_base::global_web_server_base->add_handler(new DumpHandler());
}

}  // namespace assistant
