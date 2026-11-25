#pragma once
// SmallTV Assistant for the ESP32-C2 ("SDC2") cube, running under ESPHome.
// Shares the face, mascot, sticker and text code with the ESP8266 firmware.
#include <string>
#include <vector>
#include "esphome/core/hal.h"
#include "esphome/core/time.h"
#include "esphome/components/json/json_util.h"
#include "c2_shim.h"
#include "common.h"
#include "textutil.h"
#include "face.h"
#include "mascot.h"
#include "stickers.h"

namespace assistant {

using esphome::Color;
using esphome::ESPTime;
using esphome::display::BaseFont;
using esphome::display::Display;
using esphome::display::TextAlign;

// ---- settings (filled in from the ESPHome entities) ----------------------
struct Settings {
  int style = STYLE_FACE;
  std::string name = "Cube";
  std::string familyTopic;
  std::string topic;
  std::string server = "http://ntfy.sh";
  int showSecs = 20;
  int keepMins = 60;   // keep bringing back the last message for this long (0 = off)
  int nightStart = 23, nightEnd = 7;
};
inline Settings cfg;

struct Fonts {
  BaseFont* small = nullptr;   // ~14 px
  BaseFont* text = nullptr;    // ~17 px
  BaseFont* bold = nullptr;    // ~17 px bold
  BaseFont* title = nullptr;   // ~22 px bold
  BaseFont* clock = nullptr;   // big digits
};
inline Fonts fonts;

// ---- state ----------------------------------------------------------------
enum C2Screen : uint8_t { C2_IDLE, C2_MSG };
inline C2Screen screen = C2_IDLE;
inline std::vector<Note> history;
inline std::vector<Note> queue;
inline Note current;
inline uint32_t msgUntil = 0, alertUntil = 0, msgStart = 0, recallAt = 0;
inline FaceAnim anim;
inline uint8_t idleMood = MOOD_NEUTRAL;
inline uint32_t idleMoodUntil = 0;
inline std::string lastId;
inline int64_t pollSince = 0;   // unix time we started listening
inline time_t nowEpoch = 0;
inline int nowHour = 12;
inline Color palette[16];
inline TFT_eSPI measurer;

static const size_t HISTORY_MAX = 6, QUEUE_MAX = 5;

inline Color c565(uint16_t v) {
  return Color(((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31);
}

inline void setup() {
  static const uint16_t p[16] = {0x0000, 0x07FF, 0xF8B2, 0xF800, 0x39E7, 0xC618, 0x0866, 0xFFE0,
                                 0xFFFF, 0x4A69, 0xFD20, 0x07E0, 0x2B9F, 0xA01F, 0x5D9F, 0x0000};
  for (int i = 0; i < 16; i++) palette[i] = c565(p[i]);
}

inline bool isNight() {
  if (cfg.nightStart == cfg.nightEnd || nowEpoch == 0) return false;
  int h = nowHour;
  if (cfg.nightStart < cfg.nightEnd) return h >= cfg.nightStart && h < cfg.nightEnd;
  return h >= cfg.nightStart || h < cfg.nightEnd;
}

// ---- notifications ----------------------------------------------------------
inline void showNext() {
  current = queue.front();
  queue.erase(queue.begin());
  screen = C2_MSG;
  msgUntil = esphome::millis() + (uint32_t)cfg.showSecs * 1000UL;
  msgStart = esphome::millis();
  recallAt = 0;
  alertUntil = esphome::millis() + 1400;
}

inline void addNote(String title, String body, int mood, uint8_t sticker = STK_NONE, bool family = false) {
  title = asciiFold(title);
  body = asciiFold(body);
  if (sticker != STK_NONE && sticker >= STK_COUNT) sticker = STK_NONE;
  if (family) {
    if (!title.length()) title = "Someone";
    if (!body.length() && sticker != STK_NONE) body = STICKER_CAPTIONS[sticker];
    if (mood < 0) mood = sticker != STK_NONE ? (int)STICKER_MOODS[sticker] : (int)MOOD_HAPPY;
  }
  if (!title.length() && !body.length() && sticker == STK_NONE) return;
  if (!body.length()) { body = title; title = ""; }
  Note n;
  n.sticker = sticker;
  n.family = family;
  n.accent = senderColor(title);
  n.title = title.substring(0, 80);
  n.body = body.substring(0, 400);
  n.mood = (mood >= 0 && mood < MOOD_COUNT) ? mood : MOOD_NEUTRAL;
  n.at = nowEpoch;
  history.push_back(n);
  if (history.size() > HISTORY_MAX) history.erase(history.begin());
  queue.push_back(n);
  if (queue.size() > QUEUE_MAX) queue.erase(queue.begin());
}

inline void dismiss() {
  queue.clear();
  recallAt = 0;
  screen = C2_IDLE;
}

// "!style mascot", "!style next", "!clear" (own topic only)
inline bool handleCommand(String m) {
  m.trim();
  if (!m.startsWith("!")) return false;
  m.toLowerCase();
  if (m.startsWith("!style")) {
    String arg = m.substring(6);
    arg.trim();
    if (arg == "next" || arg == "") cfg.style = (cfg.style + 1) % STYLE_COUNT;
    for (int i = 0; i < STYLE_COUNT; i++) if (arg == STYLE_NAMES[i]) cfg.style = i;
    return true;
  }
  if (m == "!clear" || m == "!dismiss") { dismiss(); return true; }
  return true;  // unknown commands are ignored rather than shown
}

inline void handleMessage(JsonObject d) {
  const char* ev = d["event"] | "";
  if (strcmp(ev, "message") != 0) return;
  lastId = (const char*)(d["id"] | "");
  String title = (const char*)(d["title"] | "");
  String msg = (const char*)(d["message"] | "");
  std::string topic = (const char*)(d["topic"] | "");
  bool family = !cfg.familyTopic.empty() && topic == cfg.familyTopic;
  if (!family && msg.startsWith("!")) { handleCommand(msg); return; }
  int mood = -1;
  uint8_t sticker = STK_NONE;
  JsonArray tags = d["tags"].as<JsonArray>();
  for (JsonVariant t : tags) {
    String tag = (const char*)(t.as<const char*>() ? t.as<const char*>() : "");
    if (sticker == STK_NONE) sticker = stickerFromName(tag);
    int m = moodFromString(tag);
    if (m >= 0 && mood < 0) mood = m;
  }
  if (sticker == STK_NONE && family) sticker = findSticker(msg);
  int prio = d["priority"] | 3;
  if (mood < 0 && prio >= 5) mood = MOOD_SURPRISED;
  addNote(title, msg, mood, sticker, family);
}

// Body of an ntfy poll: one JSON object per line
inline void onPollBody(const std::string& body) {
  size_t start = 0;
  while (start < body.size()) {
    size_t end = body.find('\n', start);
    if (end == std::string::npos) end = body.size();
    std::string line = body.substr(start, end - start);
    start = end + 1;
    if (line.size() < 2) continue;
    esphome::json::parse_json(line, [](JsonObject root) -> bool {
      handleMessage(root);
      return true;
    });
  }
}

inline std::string serverBase() {
  std::string s = cfg.server.empty() ? "http://ntfy.sh" : cfg.server;
  while (!s.empty() && s.back() == '/') s.pop_back();
  if (s.rfind("http", 0) != 0) s = "https://" + s;
  return s;
}

inline std::string topics() {
  std::string t = cfg.topic;
  if (!cfg.familyTopic.empty() && cfg.familyTopic != cfg.topic) t += (t.empty() ? "" : ",") + cfg.familyTopic;
  return t;
}

inline bool readyToPoll() { return pollSince > 0 && !topics().empty(); }

inline std::string pollUrl() {
  std::string since = lastId.empty() ? std::to_string((long long)pollSince) : lastId;
  return serverBase() + "/" + topics() + "/json?poll=1&since=" + since;
}

inline std::string jsonEscape(const std::string& in) {
  std::string o;
  for (unsigned char c : in) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': break;
      case '\t': o += ' '; break;
      default:
        if (c < 0x20) break;
        o += (char)c;
    }
  }
  return o;
}

// JSON for publishing to the family topic (POST to the server root)
inline std::string familyBody(const std::string& text, int sticker) {
  std::string msg = text;
  while (!msg.empty() && msg.back() == ' ') msg.pop_back();
  std::string tags;
  if (sticker >= 0 && sticker < STK_COUNT) {
    msg = msg.empty() ? std::string(STICKER_EMOJI[sticker]) : msg + " " + STICKER_EMOJI[sticker];
    tags = std::string(",\"tags\":[\"sticker-") + STICKER_NAMES[sticker] + "\"]";
  }
  return "{\"topic\":\"" + jsonEscape(cfg.familyTopic) + "\",\"title\":\"" + jsonEscape(cfg.name) +
         "\",\"message\":\"" + jsonEscape(msg) + "\"" + tags + "}";
}

// Desired backlight level (0..1) - dimmer at night unless a message is up
inline float brightnessFactor() {
  if (!isNight()) return 1.0f;
  return screen == C2_MSG ? 0.5f : 0.2f;
}

// ---- drawing ----------------------------------------------------------------
inline void roundRect(Display& it, int x, int y, int w, int h, int r, Color col) {
  TFT_eSprite s;
  s.d = &it;
  Color pal[16];
  for (auto& p : pal) p = col;
  s.pal = pal;
  s.fillRoundRect(x, y, w, h, r, 0);
}

inline void text(Display& it, BaseFont* f, int x, int y, Color col, TextAlign a, const String& str) {
  if (!f || !str.length()) return;
  it.print(x, y, f, col, a, str.c_str());
}

inline String fit(Display& it, BaseFont* f, const String& s, int w) {
  measurer.d = &it;
  measurer.font = f;
  return fitWidth(measurer, s, w);
}

inline String clockText() {
  ESPTime t = ESPTime::from_epoch_local(nowEpoch);
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute);
  return String(buf);
}
inline String hhmm(time_t at) {
  if (at < 1700000000) return String("");
  ESPTime t = ESPTime::from_epoch_local(at);
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute);
  return String(buf);
}
inline String dateText() {
  ESPTime t = ESPTime::from_epoch_local(nowEpoch);
  return String(t.strftime("%a %d %b"));
}

inline uint16_t accentOf(const Note& n) { return n.family ? n.accent : MOOD_COLORS[n.mood]; }

inline void drawBubble(Display& it, const Note& n) {
  Color accent = c565(accentOf(n));
  Color bg = c565(0x18E3);
  const int bx = 6, by = 112, bw = 228, bh = 122;
  roundRect(it, bx, by, bw, bh, 12, accent);
  roundRect(it, bx + 2, by + 2, bw - 4, bh - 4, 10, bg);
  it.filled_triangle(108, by + 2, 132, by + 2, 120, by - 8, accent);
  it.filled_triangle(111, by + 3, 129, by + 3, 120, by - 5, bg);

  int y = by + 8;
  int maxLines = 5;
  if (n.title.length()) {
    text(it, fonts.bold, bx + 10, y, accent, TextAlign::TOP_LEFT, fit(it, fonts.bold, n.title, bw - 20));
    y += 22;
    maxLines = 4;
  }
  measurer.d = &it;
  measurer.font = fonts.text;
  for (const String& l : wrapText(measurer, n.body, bw - 20, maxLines)) {
    text(it, fonts.text, bx + 10, y, Color(255, 255, 255), TextAlign::TOP_LEFT, l);
    y += 20;
  }
  if (!queue.empty())
    text(it, fonts.small, bx + bw - 10, by + bh - 4, c565(0x8410), TextAlign::BOTTOM_RIGHT,
         "+" + String((int)queue.size()) + " more");
}

inline void drawIdleInfo(Display& it) {
  Color grey = c565(0x8410);
  if (nowEpoch == 0) {
    text(it, fonts.text, 120, 170, grey, TextAlign::CENTER, "Syncing clock...");
    return;
  }
  text(it, fonts.clock, 120, 160, Color(255, 255, 255), TextAlign::CENTER, clockText());
  text(it, fonts.text, 120, 200, palette[PAL_EYE], TextAlign::CENTER, dateText());
  String status = "SmallTV Assistant";
  if (!history.empty()) {
    const Note& n = history.back();
    status = "Last " + hhmm(n.at) + ": " + (n.title.length() ? n.title : n.body);
  }
  text(it, fonts.small, 120, 224, grey, TextAlign::CENTER, fit(it, fonts.small, status, 226));
}

inline void drawCardsIdle(Display& it) {
  Color acc = palette[PAL_EYE];
  Color grey = c565(0x8410);
  if (nowEpoch == 0) {
    text(it, fonts.text, 120, 120, grey, TextAlign::CENTER, "Syncing clock...");
    return;
  }
  text(it, fonts.title, 120, 32, acc, TextAlign::CENTER, dateText());
  text(it, fonts.clock, 120, 92, Color(255, 255, 255), TextAlign::CENTER, clockText());
  const int px = 8, py = 146, pw = 224, ph = 84;
  roundRect(it, px, py, pw, ph, 10, c565(0x18E3));
  if (history.empty()) {
    text(it, fonts.text, px + 10, py + 12, Color(255, 255, 255), TextAlign::TOP_LEFT, "No notifications yet");
    return;
  }
  const Note& n = history.back();
  text(it, fonts.small, px + 10, py + 6, c565(accentOf(n)), TextAlign::TOP_LEFT, "Last " + hhmm(n.at));
  text(it, fonts.bold, px + 10, py + 26, Color(255, 255, 255), TextAlign::TOP_LEFT,
       fit(it, fonts.bold, n.title.length() ? n.title : n.body, pw - 20));
  if (n.title.length())
    text(it, fonts.text, px + 10, py + 52, grey, TextAlign::TOP_LEFT, fit(it, fonts.text, n.body, pw - 20));
}

inline void drawCardsMessage(Display& it, const Note& n) {
  Color accent = c565(accentOf(n));
  it.filled_rectangle(0, 0, 240, 46, accent);
  String when = hhmm(n.at);
  if (when.length()) text(it, fonts.small, 232, 23, Color(0, 0, 0), TextAlign::CENTER_RIGHT, when);
  text(it, fonts.title, 10, 23, Color(0, 0, 0), TextAlign::CENTER_LEFT,
       fit(it, fonts.title, n.title.length() ? n.title : String("Message"), 165));
  measurer.d = &it;
  measurer.font = fonts.title;
  bool over = false;
  std::vector<String> lines = wrapText(measurer, n.body, 220, 6, &over);
  BaseFont* f = fonts.title;
  int step = 27;
  if (over) {
    measurer.font = fonts.text;
    lines = wrapText(measurer, n.body, 220, 8, nullptr);
    f = fonts.text;
    step = 21;
  }
  int y = 58;
  for (const String& l : lines) { text(it, f, 10, y, Color(255, 255, 255), TextAlign::TOP_LEFT, l); y += step; }
  if (!queue.empty())
    text(it, fonts.small, 232, 238, c565(0x8410), TextAlign::BOTTOM_RIGHT, "+" + String((int)queue.size()) + " more");
}

inline uint8_t currentIdleMood(uint32_t now) {
  if (isNight()) return MOOD_SLEEPY;
  if ((int32_t)(now - idleMoodUntil) >= 0) {
    int r = random(0, 10);
    idleMood = r == 0 ? MOOD_HAPPY : (r == 1 ? MOOD_THINKING : MOOD_NEUTRAL);
    idleMoodUntil = now + (idleMood == MOOD_NEUTRAL ? random(15000, 40000) : random(2500, 5000));
  }
  return idleMood;
}

// The display may call its drawing code several times per frame (one pass per
// part of the screen), so time only moves on between frames.
inline uint32_t frameTime() {
  static uint32_t last = 0;
  uint32_t m = esphome::millis();
  if (m - last > 60) last = m;
  return last;
}

inline void render(Display& it, ESPTime now) {
  if (now.is_valid()) {
    nowEpoch = now.timestamp;
    nowHour = now.hour;
    if (pollSince == 0) pollSince = (int64_t)now.timestamp - 60;
  }
  uint32_t t = frameTime();

  if (screen == C2_MSG && current.mood == MOOD_THINKING && !queue.empty()) msgUntil = t;
  if (screen == C2_MSG && (int32_t)(t - msgUntil) >= 0) {
    if (!queue.empty()) showNext();
    else {
      screen = C2_IDLE;
      // between clock/face screens, keep bringing the last message back for a while
      recallAt = (cfg.keepMins > 0 && (uint32_t)(t - msgStart) < (uint32_t)cfg.keepMins * 60000UL) ? t + 20000 : 0;
    }
  }
  if (screen == C2_IDLE && queue.empty() && recallAt && (int32_t)(t - recallAt) >= 0) {
    recallAt = 0;
    if ((uint32_t)(t - msgStart) < (uint32_t)cfg.keepMins * 60000UL) {
      screen = C2_MSG;
      msgUntil = t + 8000;
    }
  }
  if (screen == C2_IDLE && !queue.empty()) showNext();

  it.fill(Color(0, 0, 0));
  bool msg = screen == C2_MSG;
  bool alert = msg && (int32_t)(t - alertUntil) < 0;
  bool stickerMode = msg && current.sticker < STK_COUNT;

  if (cfg.style == STYLE_CARDS && !stickerMode) {
    if (msg) drawCardsMessage(it, current);
    else drawCardsIdle(it);
    return;
  }

  TFT_eSprite spr;
  spr.d = &it;
  spr.pal = palette;
  spr.ox = 0;
  spr.oy = msg ? 0 : 6;
  uint8_t mood = msg ? (alert ? (uint8_t)MOOD_SURPRISED : current.mood) : currentIdleMood(t);
  if (stickerMode) drawSticker(spr, current.sticker, t);
  else if (cfg.style == STYLE_MASCOT) drawMascot(spr, anim, mood, msg, alert, t);
  else drawFace(spr, anim, mood, msg, t);

  if (msg) drawBubble(it, current);
  else drawIdleInfo(it);
}

inline void testMessage() {
  addNote("Hello!", "Your little assistant is working.", MOOD_HAPPY);
}

}  // namespace assistant
