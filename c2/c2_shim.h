#pragma once
// Small stand-ins for the Arduino / TFT_eSPI pieces the shared drawing code
// (common.h, face.h, mascot.h, stickers.h, textutil.h) uses, so the same
// code runs under ESPHome (ESP-IDF) on the ESP32-C2 cube.
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <algorithm>
#include "esphome/core/helpers.h"
#include "esphome/core/color.h"
#include "esphome/components/display/display.h"

// ---- Arduino-style String (only what the shared code needs) ---------------
class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& c) : s(c) {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  size_t length() const { return s.size(); }
  const char* c_str() const { return s.c_str(); }
  void reserve(size_t n) { s.reserve(n); }
  void trim() {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) { s.clear(); return; }
    size_t b = s.find_last_not_of(" \t\r\n");
    s = s.substr(a, b - a + 1);
  }
  void toLowerCase() { for (auto& c : s) c = (char)tolower((unsigned char)c); }
  bool startsWith(const String& p) const { return s.rfind(p.s, 0) == 0; }
  bool endsWith(const String& p) const {
    return s.size() >= p.s.size() && s.compare(s.size() - p.s.size(), p.s.size(), p.s) == 0;
  }
  String substring(size_t a) const { return a >= s.size() ? String() : String(s.substr(a)); }
  String substring(size_t a, size_t b) const {
    if (a >= s.size() || b <= a) return String();
    return String(s.substr(a, b - a));
  }
  int indexOf(char c, size_t from = 0) const { auto p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  void remove(size_t i) { if (i < s.size()) s.erase(i); }
  long toInt() const { return atol(s.c_str()); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  char operator[](size_t i) const { return i < s.size() ? s[i] : 0; }
  char& operator[](size_t i) { return s[i]; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator==(const char* o) const { return s == o; }
  bool operator!=(const String& o) const { return s != o.s; }
};
inline String operator+(const String& a, const String& b) { return String(a.s + b.s); }
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
inline String operator+(const String& a, const char* b) { return String(a.s + b); }
inline String operator+(const String& a, char b) { return String(a.s + b); }

// Arduino random(min, max): min inclusive, max exclusive
inline long random(long a, long b) {
  if (b <= a) return a;
  return a + (long)(esphome::random_uint32() % (uint32_t)(b - a));
}

// ---- TFT_eSPI stand-ins ----------------------------------------------------
// Text measuring for word-wrap (textutil.h)
struct TFT_eSPI {
  esphome::display::Display* d = nullptr;
  esphome::display::BaseFont* font = nullptr;
  int16_t textWidth(const String& str) {
    if (!d || !font || !str.length()) return 0;
    int x1, y1, w, h;
    d->get_text_bounds(0, 0, str.c_str(), font, esphome::display::TextAlign::TOP_LEFT, &x1, &y1, &w, &h);
    return (int16_t)w;
  }
};

// The face/mascot/sticker code draws into a 240x104 "sprite" using palette
// indexes. Here that becomes direct drawing on the display at (ox, oy).
struct TFT_eSprite {
  esphome::display::Display* d = nullptr;
  int ox = 0, oy = 0;
  const esphome::Color* pal = nullptr;

  esphome::Color c(uint32_t i) const { return pal[i & 15]; }
  void fillSprite(uint32_t i) { d->filled_rectangle(ox, oy, 240, 104, c(i)); }
  void fillRect(int x, int y, int w, int h, uint32_t i) {
    if (w > 0 && h > 0) d->filled_rectangle(ox + x, oy + y, w, h, c(i));
  }
  void fillCircle(int x, int y, int r, uint32_t i) {
    if (r <= 0) { d->draw_pixel_at(ox + x, oy + y, c(i)); return; }
    d->filled_circle(ox + x, oy + y, r, c(i));
  }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t i) {
    d->filled_triangle(ox + x0, oy + y0, ox + x1, oy + y1, ox + x2, oy + y2, c(i));
  }
  void drawLine(int x0, int y0, int x1, int y1, uint32_t i) {
    d->line(ox + x0, oy + y0, ox + x1, oy + y1, c(i));
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint32_t i) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) { fillRect(x, y, w, h, i); return; }
    fillRect(x + r, y, w - 2 * r, h, i);
    fillRect(x, y + r, r, h - 2 * r, i);
    fillRect(x + w - r, y + r, r, h - 2 * r, i);
    fillCircle(x + r, y + r, r, i);
    fillCircle(x + w - r - 1, y + r, r, i);
    fillCircle(x + r, y + h - r - 1, r, i);
    fillCircle(x + w - r - 1, y + h - r - 1, r, i);
  }
};
