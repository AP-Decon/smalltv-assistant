// SmallTV Assistant
// Custom firmware for the GeekMagic SmallTV-Ultra (ESP8266 + ST7789 240x240):
// a little desk assistant that shows push notifications as an animated
// face, a pixel-art mascot, or minimal cards.
//
// Notifications arrive two ways:
//   * ntfy topic subscription (works from anywhere: phone, scripts, Claude bridge)
//   * HTTP POST to http://<cube>/notify on your home network

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <vector>

#include "common.h"
#include "textutil.h"
#include "face.h"
#include "mascot.h"
#include "webpage.h"

#ifndef PIN_BACKLIGHT
#define PIN_BACKLIGHT 5
#endif
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

static const char* HOSTNAME = "smalltv-assistant";
static const char* CONFIG_PATH = "/assistant.json";
static const uint16_t BUBBLE_BG = 0x18E3;  // dark grey
static const uint16_t GREY = 0x8410;
static const int SPRITE_W = 240, SPRITE_H = 104;
static const size_t HISTORY_MAX = 6, QUEUE_MAX = 5;

TFT_eSPI tft;
TFT_eSprite spr(&tft);
bool sprOk = false;
uint16_t palette[16];

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
Config cfg;
bool fsOk = false;  // LittleFS mounted (settings can be saved)

// ---- notification state -----------------------------------------------
std::vector<Note> history;  // newest last
std::vector<Note> queue;    // waiting to be shown
Note current;

enum Screen : uint8_t { SCR_IDLE, SCR_MSG, SCR_TEST };
Screen screen = SCR_IDLE;
bool needRedraw = true;
uint32_t msgUntil = 0, alertUntil = 0, testUntil = 0, lastFrame = 0;
int lastClockMinute = -1;
int lastBrightness = -1;

FaceAnim anim;
uint8_t idleMood = MOOD_NEUTRAL;
uint32_t idleMoodUntil = 0;

// ---- ntfy stream ------------------------------------------------------
WiFiClient ntfy;
bool ntfyHeadersDone = false, ntfyOk = false;
uint32_t ntfyLastData = 0, ntfyNextTry = 0, ntfyBackoff = 2000;
String ntfyLastId;

// =======================================================================
// Settings
// =======================================================================
void loadConfig() {
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return;
  JsonDocument d;
  if (deserializeJson(d, f)) { f.close(); return; }
  f.close();
  cfg.style = d["style"] | cfg.style;
  if (cfg.style >= STYLE_COUNT) cfg.style = STYLE_FACE;
  cfg.brightness = d["brightness"] | cfg.brightness;
  cfg.nightBrightness = d["nightBrightness"] | cfg.nightBrightness;
  cfg.nightStart = d["nightStart"] | cfg.nightStart;
  cfg.nightEnd = d["nightEnd"] | cfg.nightEnd;
  cfg.showSecs = d["showSecs"] | cfg.showSecs;
  cfg.ntfyServer = d["ntfyServer"] | cfg.ntfyServer.c_str();
  cfg.ntfyPort = d["ntfyPort"] | cfg.ntfyPort;
  cfg.ntfyTopic = d["ntfyTopic"] | cfg.ntfyTopic.c_str();
  cfg.ntfyToken = d["ntfyToken"] | cfg.ntfyToken.c_str();
  cfg.tz = d["tz"] | cfg.tz.c_str();
  cfg.hour12 = d["hour12"] | cfg.hour12;
  cfg.bgr = d["bgr"] | cfg.bgr;
  cfg.invert = d["invert"] | cfg.invert;
  cfg.eyeHex = d["eyeHex"] | cfg.eyeHex.c_str();
}

void saveConfig() {
  if (!fsOk) return;
  JsonDocument d;
  d["style"] = cfg.style;
  d["brightness"] = cfg.brightness;
  d["nightBrightness"] = cfg.nightBrightness;
  d["nightStart"] = cfg.nightStart;
  d["nightEnd"] = cfg.nightEnd;
  d["showSecs"] = cfg.showSecs;
  d["ntfyServer"] = cfg.ntfyServer;
  d["ntfyPort"] = cfg.ntfyPort;
  d["ntfyTopic"] = cfg.ntfyTopic;
  d["ntfyToken"] = cfg.ntfyToken;
  d["tz"] = cfg.tz;
  d["hour12"] = cfg.hour12;
  d["bgr"] = cfg.bgr;
  d["invert"] = cfg.invert;
  d["eyeHex"] = cfg.eyeHex;
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return;
  serializeJson(d, f);
  f.close();
}

// =======================================================================
// Display helpers
// =======================================================================
void setupPalette() {
  for (auto& c : palette) c = TFT_BLACK;
  palette[PAL_BG] = TFT_BLACK;
  palette[PAL_EYE] = hexTo565(cfg.eyeHex);
  palette[PAL_PINK] = 0xF8B2;
  palette[PAL_RED] = 0xF800;
  palette[PAL_OUTLINE] = 0x39E7;
  palette[PAL_BODY] = 0xC618;
  palette[PAL_SCREEN] = 0x0866;
  palette[PAL_YELLOW] = 0xFFE0;
  palette[PAL_WHITE] = 0xFFFF;
  palette[PAL_DIM] = 0x4A69;
  if (sprOk) spr.createPalette(palette, 16);
}

void applyPanel() {
  tft.writecommand(0x36);  // MADCTL: rotation 0, colour order bit
  tft.writedata(cfg.bgr ? 0x08 : 0x00);
  tft.invertDisplay(cfg.invert);
}

void setBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (pct == lastBrightness) return;
  lastBrightness = pct;
  // backlight is active-low
  analogWrite(PIN_BACKLIGHT, 255 - (pct * 255 / 100));
}

bool timeValid() { return time(nullptr) > 1700000000; }

bool isNight() {
  if (!timeValid() || cfg.nightStart == cfg.nightEnd) return false;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int h = t.tm_hour;
  if (cfg.nightStart < cfg.nightEnd) return h >= cfg.nightStart && h < cfg.nightEnd;
  return h >= cfg.nightStart || h < cfg.nightEnd;
}

void useFont(const GFXfont* f) { tft.setFreeFont(f); }
void useFont(uint8_t n) { tft.setTextFont(n); }

String clockText(const struct tm& t) {
  char buf[8];
  int h = t.tm_hour;
  if (cfg.hour12) { h %= 12; if (h == 0) h = 12; }
  snprintf(buf, sizeof(buf), cfg.hour12 ? "%d:%02d" : "%02d:%02d", h, t.tm_min);
  return String(buf);
}

String dateText(const struct tm& t) {
  char buf[24];
  strftime(buf, sizeof(buf), "%a %d %b", &t);
  return String(buf);
}

String hhmm(time_t at) {
  if (at < 1700000000) return "";
  struct tm t;
  localtime_r(&at, &t);
  return clockText(t);
}

void bootScreen(const String& l1, const String& l2 = "", const String& l3 = "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(hexTo565(cfg.eyeHex), TFT_BLACK);
  useFont(&FreeSansBold12pt7b);
  tft.drawString("SmallTV", 120, 60);
  tft.drawString("Assistant", 120, 88);
  useFont(&FreeSans9pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(l1, 120, 140);
  tft.setTextColor(GREY, TFT_BLACK);
  if (l2.length()) tft.drawString(l2, 120, 166);
  if (l3.length()) tft.drawString(l3, 120, 190);
}

// Text block in the bottom half under the face / mascot (idle)
void drawIdleInfo(bool force) {
  if (!timeValid()) {
    if (!force) return;
    tft.fillRect(0, SPRITE_H + 10, 240, 240 - SPRITE_H - 10, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(GREY, TFT_BLACK);
    useFont(2);
    tft.drawString("Syncing clock...", 120, 170);
    tft.drawString(WiFi.localIP().toString(), 120, 200);
    return;
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (lastClockMinute < 0) force = true;  // first draw since the clock synced
  if (!force && t.tm_min == lastClockMinute) return;
  lastClockMinute = t.tm_min;
  if (force) tft.fillRect(0, SPRITE_H + 10, 240, 240 - SPRITE_H - 10, TFT_BLACK);

  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(240);
  useFont(7);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(clockText(t), 120, 158);

  useFont(2);
  tft.setTextColor(hexTo565(cfg.eyeHex), TFT_BLACK);
  tft.drawString(dateText(t), 120, 198);

  String status;
  if (!history.empty()) {
    const Note& n = history.back();
    status = "Last " + hhmm(n.at) + ": " + (n.title.length() ? n.title : n.body);
  } else {
    status = "http://" + WiFi.localIP().toString();
  }
  tft.setTextColor(GREY, TFT_BLACK);
  tft.drawString(fitWidth(tft, status, 226), 120, 222);
  tft.setTextPadding(0);
}

// Speech bubble under the face / mascot (message)
void drawBubble(const Note& n) {
  uint16_t accent = MOOD_COLORS[n.mood];
  const int bx = 6, by = 112, bw = 228, bh = 122;
  tft.fillRect(0, SPRITE_H, 240, 240 - SPRITE_H, TFT_BLACK);
  tft.fillRoundRect(bx, by, bw, bh, 12, BUBBLE_BG);
  tft.drawRoundRect(bx, by, bw, bh, 12, accent);
  tft.fillTriangle(108, by + 1, 132, by + 1, 120, by - 8, BUBBLE_BG);
  tft.drawLine(108, by, 120, by - 8, accent);
  tft.drawLine(132, by, 120, by - 8, accent);

  tft.setTextDatum(TL_DATUM);
  int y = by + 9;
  int maxLines = 5;
  if (n.title.length()) {
    useFont(&FreeSansBold9pt7b);
    tft.setTextColor(accent);
    tft.drawString(fitWidth(tft, n.title, bw - 20), bx + 10, y);
    y += 22;
    maxLines = 4;
  }
  useFont(&FreeSans9pt7b);
  tft.setTextColor(TFT_WHITE);
  for (const String& l : wrapText(tft, n.body, bw - 20, maxLines)) {
    tft.drawString(l, bx + 10, y);
    y += 20;
  }
  if (!queue.empty()) {
    useFont(1);
    tft.setTextDatum(BR_DATUM);
    tft.setTextColor(GREY);
    tft.drawString("+" + String(queue.size()) + " more", bx + bw - 10, by + bh - 4);
  }
}

// ---- Cards style ------------------------------------------------------
void drawCardsIdle(bool force) {
  if (!timeValid()) {
    if (force) bootScreen("Syncing clock...", "http://" + WiFi.localIP().toString());
    return;
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (lastClockMinute < 0) force = true;
  if (!force && t.tm_min == lastClockMinute) return;
  lastClockMinute = t.tm_min;
  uint16_t acc = hexTo565(cfg.eyeHex);

  if (force) tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(240);
  useFont(4);
  tft.setTextColor(acc, TFT_BLACK);
  tft.drawString(dateText(t), 120, 32);
  useFont(7);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(clockText(t), 120, 92);
  tft.setTextPadding(0);

  // last-notification panel
  const int px = 8, py = 146, pw = 224, ph = 84;
  tft.fillRoundRect(px, py, pw, ph, 10, BUBBLE_BG);
  tft.setTextDatum(TL_DATUM);
  if (history.empty()) {
    useFont(&FreeSans9pt7b);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("No notifications yet", px + 10, py + 12);
    useFont(2);
    tft.setTextColor(GREY);
    tft.drawString("http://" + WiFi.localIP().toString(), px + 10, py + 44);
  } else {
    const Note& n = history.back();
    useFont(2);
    tft.setTextColor(MOOD_COLORS[n.mood]);
    tft.drawString("Last " + hhmm(n.at), px + 10, py + 6);
    useFont(&FreeSansBold9pt7b);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(fitWidth(tft, n.title.length() ? n.title : n.body, pw - 20), px + 10, py + 26);
    if (n.title.length()) {
      useFont(&FreeSans9pt7b);
      tft.setTextColor(GREY);
      tft.drawString(fitWidth(tft, n.body, pw - 20), px + 10, py + 52);
    }
  }
}

void drawCardsMessage(const Note& n) {
  uint16_t accent = MOOD_COLORS[n.mood];
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 240, 46, accent);
  tft.setTextDatum(ML_DATUM);
  useFont(2);
  String when = hhmm(n.at);
  int whenW = when.length() ? tft.textWidth(when) + 8 : 0;
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(MR_DATUM);
  if (when.length()) tft.drawString(when, 232, 23);
  useFont(&FreeSansBold12pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(fitWidth(tft, n.title.length() ? n.title : String("Message"), 220 - whenW), 10, 23);

  // body: 12pt if it fits in 6 lines, otherwise 9pt
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE);
  bool over = false;
  useFont(&FreeSans12pt7b);
  std::vector<String> lines = wrapText(tft, n.body, 220, 6, &over);
  int step = 27;
  if (over) {
    useFont(&FreeSans9pt7b);
    lines = wrapText(tft, n.body, 220, 8, nullptr);
    step = 21;
  }
  int y = 58;
  for (const String& l : lines) { tft.drawString(l, 10, y); y += step; }

  if (!queue.empty()) {
    useFont(2);
    tft.setTextDatum(BR_DATUM);
    tft.setTextColor(GREY);
    tft.drawString("+" + String(queue.size()) + " more", 232, 238);
  }
}

void drawColourTest() {
  tft.fillScreen(TFT_BLACK);
  const uint16_t cols[3] = {TFT_RED, TFT_GREEN, TFT_BLUE};
  const char* names[3] = {"RED", "GREEN", "BLUE"};
  tft.setTextDatum(MC_DATUM);
  useFont(4);
  for (int i = 0; i < 3; i++) {
    tft.fillRect(0, 10 + i * 62, 240, 56, cols[i]);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(names[i], 120, 38 + i * 62);
  }
  useFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Bars should match their labels", 120, 210);
  tft.drawString("Background should be black", 120, 228);
}

// =======================================================================
// Notifications
// =======================================================================
void showNext() {
  current = queue.front();
  queue.erase(queue.begin());
  screen = SCR_MSG;
  msgUntil = millis() + (uint32_t)cfg.showSecs * 1000UL;
  alertUntil = millis() + 1400;
  needRedraw = true;
}

void addNote(String title, String body, int mood) {
  title = asciiFold(title);
  body = asciiFold(body);
  if (!title.length() && !body.length()) return;
  if (!body.length()) { body = title; title = ""; }
  Note n;
  n.title = title.substring(0, 80);
  n.body = body.substring(0, 400);
  n.mood = (mood >= 0 && mood < MOOD_COUNT) ? mood : MOOD_NEUTRAL;
  n.at = timeValid() ? time(nullptr) : 0;

  history.push_back(n);
  if (history.size() > HISTORY_MAX) history.erase(history.begin());
  queue.push_back(n);
  if (queue.size() > QUEUE_MAX) queue.erase(queue.begin());
  if (screen == SCR_MSG) needRedraw = true;  // refresh the "+N more" counter
}

void dismiss() {
  queue.clear();
  screen = SCR_IDLE;
  needRedraw = true;
}

void setStyle(uint8_t s) {
  cfg.style = s % STYLE_COUNT;
  needRedraw = true;
}

// Simple control messages: "!style mascot", "!style next", "!brightness 40", "!clear"
bool handleCommand(const String& raw) {
  String m = raw;
  m.trim();
  if (!m.startsWith("!")) return false;
  m.toLowerCase();
  if (m.startsWith("!style")) {
    String arg = m.substring(6);
    arg.trim();
    if (arg == "next" || arg == "") setStyle(cfg.style + 1);
    for (uint8_t i = 0; i < STYLE_COUNT; i++) if (arg == STYLE_NAMES[i]) setStyle(i);
    saveConfig();
    return true;
  }
  if (m.startsWith("!brightness")) {
    int v = m.substring(11).toInt();
    if (v > 0) { cfg.brightness = constrain(v, 1, 100); lastBrightness = -1; saveConfig(); }
    return true;
  }
  if (m == "!clear" || m == "!dismiss") { dismiss(); return true; }
  return false;
}

int moodFromTags(const String& tags) {
  int start = 0;
  while (start <= (int)tags.length()) {
    int comma = tags.indexOf(',', start);
    if (comma < 0) comma = tags.length();
    int m = moodFromString(tags.substring(start, comma));
    if (m >= 0) return m;
    start = comma + 1;
  }
  return -1;
}

// ---- ntfy ------------------------------------------------------------
String ntfyHost() {
  String h = cfg.ntfyServer;
  h.trim();
  if (h.startsWith("http://")) h = h.substring(7);
  if (h.startsWith("https://")) h = h.substring(8);
  int slash = h.indexOf('/');
  if (slash >= 0) h = h.substring(0, slash);
  return h;
}

void ntfyRestart() {
  ntfy.stop();
  ntfyOk = false;
  ntfyHeadersDone = false;
  ntfyLastId = "";
  ntfyBackoff = 2000;
  ntfyNextTry = millis();
}

void ntfyConnect() {
  ntfy.stop();
  ntfyHeadersDone = false;
  ntfyOk = false;
  String host = ntfyHost();
  ntfy.setTimeout(1500);
  if (!ntfy.connect(host.c_str(), cfg.ntfyPort)) {
    ntfyBackoff = std::min<uint32_t>(ntfyBackoff * 2, 60000);
    return;
  }
  String path = "/" + cfg.ntfyTopic + "/json";
  if (ntfyLastId.length()) path += "?since=" + ntfyLastId;
  String req = "GET " + path + " HTTP/1.0\r\nHost: " + host +
               "\r\nUser-Agent: smalltv-assistant/" FW_VERSION "\r\n";
  if (cfg.ntfyToken.length()) req += "Authorization: Bearer " + cfg.ntfyToken + "\r\n";
  req += "\r\n";
  ntfy.print(req);
  ntfyLastData = millis();
}

void handleNtfyLine(const String& line) {
  JsonDocument d;
  if (deserializeJson(d, line)) return;
  const char* ev = d["event"] | "";
  if (strcmp(ev, "message") != 0) return;
  ntfyLastId = (const char*)(d["id"] | "");
  String title = d["title"] | "";
  String msg = d["message"] | "";
  if (handleCommand(msg)) return;
  int mood = -1;
  JsonArray tags = d["tags"].as<JsonArray>();
  for (JsonVariant t : tags) {
    int m = moodFromString(t.as<String>());
    if (m >= 0) { mood = m; break; }
  }
  int prio = d["priority"] | 3;
  if (mood < 0 && prio >= 5) mood = MOOD_SURPRISED;
  addNote(title, msg, mood);
}

void ntfyLoop() {
  if (!cfg.ntfyTopic.length() || WiFi.status() != WL_CONNECTED) {
    if (ntfy.connected()) ntfy.stop();
    ntfyOk = false;
    return;
  }
  if (!ntfy.connected() && !ntfy.available()) {
    ntfyOk = false;
    if ((int32_t)(millis() - ntfyNextTry) >= 0) {
      ntfyNextTry = millis() + ntfyBackoff;
      ntfyConnect();
    }
    return;
  }
  int budget = 4;
  while (ntfy.available() && budget-- > 0) {
    String line = ntfy.readStringUntil('\n');
    line.trim();
    ntfyLastData = millis();
    if (!ntfyHeadersDone) {
      if (line.startsWith("HTTP/")) {
        ntfyOk = line.indexOf(" 200") > 0;
        if (!ntfyOk) ntfyBackoff = std::min<uint32_t>(ntfyBackoff * 2, 60000);
      } else if (line.length() == 0) {
        ntfyHeadersDone = true;
        if (ntfyOk) ntfyBackoff = 2000;
      }
      continue;
    }
    if (line.length()) handleNtfyLine(line);
  }
  // ntfy sends a keepalive roughly every 45 s; reconnect if it goes quiet
  if (millis() - ntfyLastData > 120000UL) ntfy.stop();
}

// =======================================================================
// Web server
// =======================================================================
void sendState() {
  JsonDocument d;
  JsonObject c = d["config"].to<JsonObject>();
  c["style"] = STYLE_NAMES[cfg.style];
  c["brightness"] = cfg.brightness;
  c["nightBrightness"] = cfg.nightBrightness;
  c["nightStart"] = cfg.nightStart;
  c["nightEnd"] = cfg.nightEnd;
  c["showSecs"] = cfg.showSecs;
  c["ntfyServer"] = cfg.ntfyServer;
  c["ntfyPort"] = cfg.ntfyPort;
  c["ntfyTopic"] = cfg.ntfyTopic;
  c["ntfyToken"] = cfg.ntfyToken.length() ? "********" : "";
  c["tz"] = cfg.tz;
  c["hour12"] = cfg.hour12;
  c["bgr"] = cfg.bgr;
  c["invert"] = cfg.invert;
  c["eyeHex"] = cfg.eyeHex;
  d["ip"] = WiFi.localIP().toString();
  d["heap"] = ESP.getFreeHeap();
  d["version"] = FW_VERSION;
  d["ntfy"] = ntfyOk && ntfy.connected();
  d["fsOk"] = fsOk;
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    d["time"] = dateText(t) + " " + clockText(t);
  }
  JsonArray h = d["history"].to<JsonArray>();
  for (int i = (int)history.size() - 1; i >= 0; i--) {
    JsonObject o = h.add<JsonObject>();
    o["title"] = history[i].title;
    o["body"] = history[i].body;
    o["mood"] = MOOD_NAMES[history[i].mood];
  }
  JsonArray moods = d["moods"].to<JsonArray>();
  for (uint8_t i = 0; i < MOOD_COUNT; i++) moods.add(MOOD_NAMES[i]);
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void handleConfig() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  bool ntfyChanged = false, panelChanged = false, tzChanged = false;
  auto num = [&](const char* k, int lo, int hi, int cur) -> int {
    if (d[k].isNull()) return cur;
    int v = d[k].is<const char*>() ? atoi(d[k].as<const char*>()) : d[k].as<int>();
    return constrain(v, lo, hi);
  };
  auto str = [&](const char* k, const String& cur) -> String {
    if (d[k].isNull()) return cur;
    String v = d[k].as<String>();
    v.trim();
    return v;
  };

  cfg.brightness = num("brightness", 1, 100, cfg.brightness);
  cfg.nightBrightness = num("nightBrightness", 0, 100, cfg.nightBrightness);
  cfg.nightStart = num("nightStart", 0, 23, cfg.nightStart);
  cfg.nightEnd = num("nightEnd", 0, 23, cfg.nightEnd);
  cfg.showSecs = num("showSecs", 3, 600, cfg.showSecs);
  lastBrightness = -1;

  String s = str("ntfyServer", cfg.ntfyServer);
  if (!s.length()) s = "ntfy.sh";
  int port = num("ntfyPort", 1, 65535, cfg.ntfyPort);
  String topic = str("ntfyTopic", cfg.ntfyTopic);
  String token = str("ntfyToken", cfg.ntfyToken);
  if (token == "********") token = cfg.ntfyToken;
  if (s != cfg.ntfyServer || port != cfg.ntfyPort || topic != cfg.ntfyTopic || token != cfg.ntfyToken) ntfyChanged = true;
  cfg.ntfyServer = s; cfg.ntfyPort = port; cfg.ntfyTopic = topic; cfg.ntfyToken = token;

  String tz = str("tz", cfg.tz);
  if (tz.length() && tz != cfg.tz) { cfg.tz = tz; tzChanged = true; }

  if (!d["hour12"].isNull()) cfg.hour12 = d["hour12"].as<bool>();
  if (!d["bgr"].isNull()) { bool v = d["bgr"].as<bool>(); panelChanged |= v != cfg.bgr; cfg.bgr = v; }
  if (!d["invert"].isNull()) { bool v = d["invert"].as<bool>(); panelChanged |= v != cfg.invert; cfg.invert = v; }
  String eye = str("eyeHex", cfg.eyeHex);
  if (eye.length() == 7 && eye != cfg.eyeHex) { cfg.eyeHex = eye; setupPalette(); }

  if (!d["style"].isNull()) {
    String st = d["style"].as<String>();
    for (uint8_t i = 0; i < STYLE_COUNT; i++) if (st == STYLE_NAMES[i]) cfg.style = i;
  }

  saveConfig();
  if (panelChanged) applyPanel();
  if (tzChanged) configTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com");
  if (ntfyChanged) ntfyRestart();
  needRedraw = true;
  sendState();
}

void handleNotify() {
  String title = server.hasArg("title") ? server.arg("title") : server.header("Title");
  if (!title.length()) title = server.header("X-Title");
  String msg = server.arg("message");
  int mood = server.hasArg("mood") ? moodFromString(server.arg("mood")) : -1;

  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    JsonDocument d;
    if (!deserializeJson(d, body) && d.is<JsonObject>()) {
      if (!d["title"].isNull()) title = d["title"].as<String>();
      if (!d["message"].isNull()) msg = d["message"].as<String>();
      else if (!d["body"].isNull()) msg = d["body"].as<String>();
      if (!d["mood"].isNull()) mood = moodFromString(d["mood"].as<String>());
    } else if (!msg.length()) {
      msg = body;
    }
  }
  if (mood < 0) {
    String tags = server.header("Tags");
    if (!tags.length()) tags = server.header("X-Tags");
    mood = moodFromTags(tags);
  }
  if (handleCommand(msg)) { sendState(); return; }
  if (!title.length() && !msg.length()) { server.send(400, "text/plain", "need title or message"); return; }
  addNote(title, msg, mood);
  server.send(200, "application/json", "{\"ok\":true}");
}

void setupServer() {
  server.collectHeaders("Title", "X-Title", "Tags", "X-Tags");

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/notify", HTTP_POST, handleNotify);
  server.on("/notify", HTTP_GET, handleNotify);
  server.on("/api/style", [] {
    String s = server.arg("s");
    if (s == "next" || !s.length()) setStyle(cfg.style + 1);
    for (uint8_t i = 0; i < STYLE_COUNT; i++) if (s == STYLE_NAMES[i]) setStyle(i);
    saveConfig();
    sendState();
  });
  server.on("/api/dismiss", [] { dismiss(); sendState(); });
  server.on("/api/test", [] {
    screen = SCR_TEST;
    testUntil = millis() + 8000;
    needRedraw = true;
    sendState();
  });
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  httpUpdater.setup(&server, "/update");
  server.begin();
}

// =======================================================================
// Main loop rendering
// =======================================================================
uint8_t currentIdleMood(uint32_t now) {
  if (isNight()) return MOOD_SLEEPY;
  if ((int32_t)(now - idleMoodUntil) >= 0) {
    // mostly neutral, with the odd happy or curious moment
    int r = random(0, 10);
    idleMood = r == 0 ? MOOD_HAPPY : (r == 1 ? MOOD_THINKING : MOOD_NEUTRAL);
    idleMoodUntil = now + (idleMood == MOOD_NEUTRAL ? random(15000, 40000) : random(2500, 5000));
  }
  return idleMood;
}

void render() {
  uint32_t now = millis();

  // screen transitions
  if (screen == SCR_TEST && (int32_t)(now - testUntil) >= 0) { screen = SCR_IDLE; needRedraw = true; }
  // a "thinking..." placeholder gives way as soon as the answer arrives
  if (screen == SCR_MSG && current.mood == MOOD_THINKING && !queue.empty()) msgUntil = now;
  if (screen == SCR_MSG && (int32_t)(now - msgUntil) >= 0) {
    if (!queue.empty()) showNext();
    else { screen = SCR_IDLE; needRedraw = true; }
  }
  if (screen == SCR_IDLE && !queue.empty()) showNext();

  // brightness (a new message briefly brightens a dimmed night screen)
  bool night = isNight();
  uint8_t level = night ? cfg.nightBrightness : cfg.brightness;
  if (night && screen == SCR_MSG) level = std::max<uint8_t>(level, 30);
  setBacklight(level);

  if (screen == SCR_TEST) {
    if (needRedraw) { drawColourTest(); needRedraw = false; }
    return;
  }

  bool full = needRedraw;
  needRedraw = false;
  bool alert = screen == SCR_MSG && (int32_t)(now - alertUntil) < 0;

  if (cfg.style == STYLE_CARDS) {
    if (screen == SCR_MSG) { if (full) drawCardsMessage(current); }
    else drawCardsIdle(full);
    return;
  }

  // face / mascot: animate the sprite ~25 fps
  if (full) {
    tft.fillScreen(TFT_BLACK);
    lastClockMinute = -1;
    if (screen == SCR_MSG) drawBubble(current);
  }
  if (screen == SCR_IDLE) drawIdleInfo(full);

  if (!sprOk || now - lastFrame < 40) return;
  lastFrame = now;
  bool small = screen == SCR_MSG;
  uint8_t mood = small ? (alert ? (uint8_t)MOOD_SURPRISED : current.mood) : currentIdleMood(now);
  if (cfg.style == STYLE_FACE) drawFace(spr, anim, mood, small, now);
  else drawMascot(spr, anim, mood, small, alert, now);
  spr.pushSprite(0, small ? 0 : 6);
}

// =======================================================================
void setup() {
  Serial.begin(115200);
  analogWriteRange(255);
  analogWriteFreq(1000);
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, HIGH);  // off while we initialise
  randomSeed(ESP.getChipId() ^ micros());

  // Never auto-format: the stock firmware's pages, photos and GIFs live in
  // this filesystem and are needed if you ever go back to the stock .bin.
  LittleFSConfig fsCfg;
  fsCfg.setAutoFormat(false);
  LittleFS.setConfig(fsCfg);
  fsOk = LittleFS.begin();
  if (fsOk) loadConfig();

  tft.init();
  tft.setRotation(0);
  applyPanel();
  tft.fillScreen(TFT_BLACK);
  setBacklight(cfg.brightness);

  spr.setColorDepth(4);
  sprOk = spr.createSprite(SPRITE_W, SPRITE_H) != nullptr;
  setupPalette();

  bootScreen("Connecting to Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  wm.setAPCallback([](WiFiManager*) {
    bootScreen("Wi-Fi setup:", "join  SmallTV-Assistant", "then open 192.168.4.1");
  });
  if (!wm.autoConnect("SmallTV-Assistant")) {
    bootScreen("Wi-Fi setup timed out", "restarting...");
    delay(3000);
    ESP.restart();
  }

  configTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com");
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  setupServer();

  bootScreen("Ready!", WiFi.localIP().toString(), String(HOSTNAME) + ".local");
  delay(4000);
  needRedraw = true;
  if (!fsOk) addNote("Storage", "Could not open the flash filesystem, so settings will not be saved after a restart.", MOOD_SAD);
  if (!sprOk) addNote("Low memory", "Animations are off (sprite allocation failed). Cards style still works.", MOOD_SAD);
}

void loop() {
  server.handleClient();
  MDNS.update();
  ntfyLoop();
  render();
  delay(1);
}
