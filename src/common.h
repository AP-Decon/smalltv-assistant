#pragma once
#include <Arduino.h>

// ---- Styles -----------------------------------------------------------
enum Style : uint8_t { STYLE_FACE = 0, STYLE_MASCOT = 1, STYLE_CARDS = 2, STYLE_COUNT = 3 };
static const char* const STYLE_NAMES[STYLE_COUNT] = {"face", "mascot", "cards"};

// ---- Moods ------------------------------------------------------------
enum Mood : uint8_t {
  MOOD_NEUTRAL = 0, MOOD_HAPPY, MOOD_SAD, MOOD_SURPRISED,
  MOOD_THINKING, MOOD_SLEEPY, MOOD_LOVE, MOOD_ANGRY, MOOD_COUNT
};
static const char* const MOOD_NAMES[MOOD_COUNT] = {
  "neutral", "happy", "sad", "surprised", "thinking", "sleepy", "love", "angry"
};

// Accent colour per mood (RGB565), used for bubble borders / card headers.
static const uint16_t MOOD_COLORS[MOOD_COUNT] = {
  0x07FF, // neutral   cyan
  0xFFE0, // happy     yellow
  0x3D7F, // sad       blue
  0xFD20, // surprised orange
  0xA01F, // thinking  purple
  0x7BEF, // sleepy    grey
  0xF8B2, // love      pink
  0xF800  // angry     red
};

inline int moodFromString(String s) {
  s.trim();
  s.toLowerCase();
  if (!s.length()) return -1;
  for (uint8_t i = 0; i < MOOD_COUNT; i++)
    if (s == MOOD_NAMES[i]) return i;
  // a few friendly aliases
  if (s == "smile" || s == "tada" || s == "white_check_mark" || s == "ok") return MOOD_HAPPY;
  if (s == "warning" || s == "rotating_light" || s == "exclamation" || s == "alert") return MOOD_SURPRISED;
  if (s == "cry" || s == "x" || s == "error") return MOOD_SAD;
  if (s == "heart" || s == "hearts") return MOOD_LOVE;
  if (s == "zzz" || s == "sleep") return MOOD_SLEEPY;
  if (s == "thought_balloon" || s == "think" || s == "question") return MOOD_THINKING;
  if (s == "rage" || s == "mad") return MOOD_ANGRY;
  return -1;
}

// ---- 4-bit sprite palette (shared by face + mascot) --------------------
enum PalIdx : uint8_t {
  PAL_BG = 0, PAL_EYE = 1, PAL_PINK = 2, PAL_RED = 3, PAL_OUTLINE = 4,
  PAL_BODY = 5, PAL_SCREEN = 6, PAL_YELLOW = 7, PAL_WHITE = 8, PAL_DIM = 9
};

// ---- Notification -----------------------------------------------------
struct Note {
  String title;
  String body;
  uint8_t mood = MOOD_NEUTRAL;
  time_t at = 0;
};

// ---- Settings ---------------------------------------------------------
struct Config {
  uint8_t style = STYLE_FACE;
  uint8_t brightness = 70;       // %
  uint8_t nightBrightness = 15;  // %
  uint8_t nightStart = 23;       // hour, 24h
  uint8_t nightEnd = 7;          // hour, 24h
  uint16_t showSecs = 20;        // how long each notification stays up
  String ntfyServer = "ntfy.sh";
  uint16_t ntfyPort = 80;
  String ntfyTopic = "";
  String ntfyToken = "";
  String tz = "GMT0BST,M3.5.0/1,M10.5.0";  // UK; any POSIX TZ string works
  bool hour12 = false;
  bool bgr = true;      // panel colour order (flip if red/blue are swapped)
  bool invert = true;   // panel inversion (flip if colours look like a negative)
  String eyeHex = "#00ffff";
};

inline uint16_t hexTo565(const String& hex) {
  String h = hex;
  if (h.startsWith("#")) h = h.substring(1);
  if (h.length() != 6) return 0x07FF;
  long v = strtol(h.c_str(), nullptr, 16);
  uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
