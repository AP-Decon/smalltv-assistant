#pragma once
// Animated pixel "stickers" for the family chat, drawn into the 4-bit 240x104
// sprite. A message that contains one of the emoji below shows its sticker.
#if __has_include(<TFT_eSPI.h>)
#include <TFT_eSPI.h>
#else
#include "c2_shim.h"  // ESP32-C2 (ESPHome) build
#endif
#include <math.h>
#include "common.h"
#include "face.h"

enum Sticker : uint8_t {
  STK_HEART = 0, STK_SMILE, STK_LAUGH, STK_CRY, STK_THUMBS, STK_WAVE,
  STK_STAR, STK_PARTY, STK_FIRE, STK_SLEEP, STK_HUG, STK_COUNT,
  STK_NONE = 0xFF
};

static const char* const STICKER_NAMES[STK_COUNT] = {
  "heart", "smile", "laugh", "cry", "thumbsup", "wave",
  "star", "party", "fire", "sleep", "hug"
};
// Caption when a sticker is sent on its own ("Ella" + "sent a hug")
static const char* const STICKER_CAPTIONS[STK_COUNT] = {
  "sent some love", "is smiling", "is laughing", "is feeling sad", "says thumbs up", "waved hello",
  "sent a star", "is celebrating!", "says that's fire", "says goodnight", "sent a big hug"
};
// Emoji sent along with a sticker so phones show it too (UTF-8)
static const char* const STICKER_EMOJI[STK_COUNT] = {
  "\u2764\ufe0f", "\U0001F60A", "\U0001F602", "\U0001F622", "\U0001F44D", "\U0001F44B",
  "\u2B50", "\U0001F389", "\U0001F525", "\U0001F634", "\U0001F917"
};
static const uint8_t STICKER_MOODS[STK_COUNT] = {
  MOOD_LOVE, MOOD_HAPPY, MOOD_HAPPY, MOOD_SAD, MOOD_HAPPY, MOOD_HAPPY,
  MOOD_HAPPY, MOOD_HAPPY, MOOD_SURPRISED, MOOD_SLEEPY, MOOD_LOVE
};

inline uint8_t stickerFromName(String s) {
  s.trim();
  s.toLowerCase();
  if (s.startsWith("sticker-")) s = s.substring(8);
  for (uint8_t i = 0; i < STK_COUNT; i++)
    if (s == STICKER_NAMES[i]) return i;
  if (s == "thumbs" || s == "thumbs_up" || s == "+1") return STK_THUMBS;
  if (s == "love" || s == "hearts") return STK_HEART;
  return STK_NONE;
}

inline uint8_t stickerForCodepoint(uint32_t cp) {
  switch (cp) {
    case 0x2764: case 0x2665: case 0x1F493: case 0x1F495: case 0x1F496: case 0x1F497:
    case 0x1F498: case 0x1F49D: case 0x1F49E: case 0x1F499: case 0x1F49A: case 0x1F49B:
    case 0x1F49C: case 0x1F9E1: case 0x1F5A4: case 0x1F90D: case 0x1F60D: case 0x1F970:
    case 0x1F618: case 0x1F48B:
      return STK_HEART;
    case 0x1F600: case 0x1F601: case 0x1F603: case 0x1F604: case 0x1F606: case 0x1F60A:
    case 0x1F642: case 0x263A: case 0x1F60E:
      return STK_SMILE;
    case 0x1F602: case 0x1F923:
      return STK_LAUGH;
    case 0x1F622: case 0x1F62D: case 0x1F97A: case 0x1F61E: case 0x1F614:
      return STK_CRY;
    case 0x1F44D:
      return STK_THUMBS;
    case 0x1F44B:
      return STK_WAVE;
    case 0x2B50: case 0x1F31F: case 0x2728: case 0x1F320:
      return STK_STAR;
    case 0x1F389: case 0x1F38A: case 0x1F973: case 0x1F382:
      return STK_PARTY;
    case 0x1F525:
      return STK_FIRE;
    case 0x1F634: case 0x1F319: case 0x1F4A4: case 0x1F6CC:
      return STK_SLEEP;
    case 0x1F917: case 0x1FAC2:
      return STK_HUG;
    default:
      return STK_NONE;
  }
}

// First recognised emoji in a UTF-8 string (or STK_NONE).
inline uint8_t findSticker(const String& in) {
  const uint8_t* p = (const uint8_t*)in.c_str();
  size_t n = in.length(), i = 0;
  while (i < n) {
    uint8_t c = p[i];
    uint32_t cp;
    int len;
    if (c < 0x80) { i++; continue; }
    if ((c & 0xE0) == 0xC0 && i + 1 < n) { cp = ((c & 0x1F) << 6) | (p[i + 1] & 0x3F); len = 2; }
    else if ((c & 0xF0) == 0xE0 && i + 2 < n) { cp = ((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F); len = 3; }
    else if ((c & 0xF8) == 0xF0 && i + 3 < n) {
      cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6) | (p[i + 3] & 0x3F);
      len = 4;
    } else { i++; continue; }
    uint8_t s = stickerForCodepoint(cp);
    if (s != STK_NONE) return s;
    i += len;
  }
  return STK_NONE;
}

// ---- drawing helpers ------------------------------------------------------
inline void drawPixelArt(TFT_eSprite& s, const char* const* rows, int nrows, int ox, int oy, int sc) {
  for (int r = 0; r < nrows; r++) {
    for (int c = 0; rows[r][c]; c++) {
      uint8_t col;
      switch (rows[r][c]) {
        case 'O': col = PAL_ORANGE; break;
        case 'Y': col = PAL_YELLOW; break;
        case 'B': col = PAL_BLUE; break;
        case 'W': col = PAL_WHITE; break;
        default: continue;
      }
      s.fillRect(ox + c * sc, oy + r * sc, sc, sc, col);
    }
  }
}

inline void drawStarShape(TFT_eSprite& s, int cx, int cy, float ro, float ri, float rot, uint8_t col) {
  int vx[10], vy[10];
  for (int i = 0; i < 10; i++) {
    float a = rot - (float)M_PI / 2 + i * (float)M_PI / 5;
    float r = (i % 2) ? ri : ro;
    vx[i] = cx + (int)(cosf(a) * r);
    vy[i] = cy + (int)(sinf(a) * r);
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    s.fillTriangle(cx, cy, vx[i], vy[i], vx[j], vy[j], col);
  }
}

inline void drawSparkle(TFT_eSprite& s, int x, int y, int r, uint8_t col) {
  s.fillRect(x - r, y - 1, r * 2 + 1, 3, col);
  s.fillRect(x - 1, y - r, 3, r * 2 + 1, col);
}

inline void smileyBase(TFT_eSprite& s, int cx, int cy, int r) {
  s.fillCircle(cx, cy, r + 3, PAL_ORANGE);
  s.fillCircle(cx, cy, r, PAL_YELLOW);
}

inline void drawTear(TFT_eSprite& s, int x, int y, int r) {
  s.fillCircle(x, y, r, PAL_LIGHTBLUE);
  s.fillTriangle(x - r, y - 1, x + r, y - 1, x, y - r * 2 - 2, PAL_LIGHTBLUE);
}

static const char* const ART_THUMBS[14] = {
  "......OO........",
  ".....OYYO.......",
  ".....OYYO.......",
  "....OYYYO.......",
  "....OYYYO.......",
  "...OYYYYOOOOOO..",
  "OOOOYYYYYYYYYYO.",
  "OBBOYYYYYYYYYO..",
  "OBBOYYYYYYYYYYO.",
  "OBBOYYYYYYYYYO..",
  "OBBOYYYYYYYYYYO.",
  "OBBOYYYYYYYYYO..",
  "OBBOOYYYYYYYYO..",
  "OOOO.OOOOOOOO...",
};

static const char* const ART_PALM[13] = {
  "......O.O.O.....",
  ".....OYOYOYO....",
  ".....OYOYOYO....",
  "..O..OYOYOYO....",
  ".OYO.OYOYOYO....",
  ".OYO.OYYYYYO....",
  "..OYOOYYYYYYO...",
  "..OYYYYYYYYYO...",
  "...OYYYYYYYYO...",
  "...OYYYYYYYO....",
  "....OYYYYYYO....",
  "....OYYYYYO.....",
  ".....OOOOO......",
};

// ---- the stickers ---------------------------------------------------------
inline void drawSticker(TFT_eSprite& s, uint8_t id, uint32_t t) {
  s.fillSprite(PAL_BG);
  const int cx = 120, cy = 52;

  switch (id) {
    case STK_HEART: {
      for (int i = 0; i < 4; i++) {  // little hearts floating up
        int phase = (int)((t / 18 + i * 31) % 110);
        int x = 30 + i * 60 + (int)(sinf((t + i * 400) / 300.0f) * 6);
        drawHeart(s, x, 104 - phase, 14, PAL_PINK);
      }
      float sc = 1.0f + 0.1f * sinf(t / 140.0f);
      drawHeart(s, cx, cy - 4, (int)(80 * sc), PAL_RED);
      break;
    }
    case STK_SMILE: {
      smileyBase(s, cx, cy, 44);
      s.fillCircle(cx, cy + 4, 27, PAL_BG);      // smile = black crescent
      s.fillCircle(cx, cy - 6, 29, PAL_YELLOW);
      bool blink = (t % 3200) < 160;
      int eh = blink ? 3 : 16;
      s.fillRoundRect(cx - 18, cy - 14 + (16 - eh) / 2, 9, eh, 4, PAL_BG);
      s.fillRoundRect(cx + 9, cy - 14 + (16 - eh) / 2, 9, eh, 4, PAL_BG);
      break;
    }
    case STK_LAUGH: {
      int dx = ((t / 70) % 2) ? 3 : -3;
      int x = cx + dx;
      smileyBase(s, x, cy, 44);
      // big open mouth
      s.fillCircle(x, cy + 6, 24, PAL_BG);
      s.fillRect(x - 26, cy - 20, 53, 26, PAL_YELLOW);
      s.fillCircle(x, cy + 22, 9, PAL_RED);
      // closed happy eyes
      s.fillCircle(x - 16, cy - 14, 9, PAL_BG);
      s.fillCircle(x - 16, cy - 10, 9, PAL_YELLOW);
      s.fillCircle(x + 16, cy - 14, 9, PAL_BG);
      s.fillCircle(x + 16, cy - 10, 9, PAL_YELLOW);
      // tears flying off
      int b = (int)((t / 25) % 24);
      drawTear(s, x - 54 - b / 2, cy - 4 + b / 3, 6);
      drawTear(s, x + 54 + b / 2, cy - 4 + b / 3, 6);
      break;
    }
    case STK_CRY: {
      smileyBase(s, cx, cy, 44);
      s.fillCircle(cx - 16, cy - 10, 5, PAL_BG);
      s.fillCircle(cx + 16, cy - 10, 5, PAL_BG);
      // frown: a thick arc drawn from small dots
      for (int k = 0; k <= 16; k++) {
        float a = (float)M_PI * (1.15f + 0.7f * k / 16.0f);
        s.fillCircle(cx + (int)(cosf(a) * 20), cy + 34 + (int)(sinf(a) * 14), 3, PAL_BG);
      }
      // falling tears
      for (int k = 0; k < 3; k++) {
        int y = cy - 2 + (int)((t / 9 + k * 17) % 50);
        if (y < cy + 44) {
          drawTear(s, cx - 16, y, 4);
          drawTear(s, cx + 16, (y + 25 - (cy - 2)) % 50 + cy - 2, 4);
        }
      }
      break;
    }
    case STK_THUMBS: {
      int bob = (int)(sinf(t / 160.0f) * 5);
      drawPixelArt(s, ART_THUMBS, 14, cx - 48, 6 + bob, 6);
      if ((t / 300) % 2) { drawSparkle(s, 40, 20, 6, PAL_YELLOW); drawSparkle(s, 200, 70, 6, PAL_YELLOW); }
      else { drawSparkle(s, 200, 22, 5, PAL_WHITE); drawSparkle(s, 44, 78, 5, PAL_WHITE); }
      break;
    }
    case STK_WAVE: {
      int dx = (int)(sinf(t / 110.0f) * 12);
      drawPixelArt(s, ART_PALM, 13, cx - 48 + dx, 8, 6);
      // motion lines
      uint8_t col = PAL_WHITE;
      int side = dx > 0 ? -1 : 1;
      int lx = cx + side * 66;
      for (int i = 0; i < 3; i++) s.fillRect(lx + side * i * 2, 26 + i * 18, 4, 12, col);
      break;
    }
    case STK_STAR: {
      float rot = t / 700.0f;
      float sc = 1.0f + 0.06f * sinf(t / 180.0f);
      drawStarShape(s, cx, cy, 48 * sc, 20 * sc, rot, PAL_ORANGE);
      drawStarShape(s, cx, cy, 40 * sc, 16 * sc, rot, PAL_YELLOW);
      const int sp[4][2] = {{36, 22}, {204, 26}, {48, 84}, {196, 86}};
      for (int i = 0; i < 4; i++)
        if (((t / 250) + i) % 3 != 0) drawSparkle(s, sp[i][0], sp[i][1], 5 + ((t / 250 + i) % 2) * 2, PAL_WHITE);
      break;
    }
    case STK_PARTY: {
      static const uint8_t cols[6] = {PAL_RED, PAL_YELLOW, PAL_GREEN, PAL_LIGHTBLUE, PAL_PINK, PAL_PURPLE};
      for (int i = 0; i < 34; i++) {
        int speed = 1 + i % 3;
        int x = (i * 53 + 17) % 236 + (int)(sinf((t + i * 300) / 250.0f) * 4);
        int y = (int)(((t * speed) / 28 + i * 37) % 124) - 12;
        bool flip = ((t / 180) + i) % 2;
        s.fillRect(x, y, flip ? 7 : 4, flip ? 4 : 8, cols[i % 6]);
      }
      // party popper
      s.fillTriangle(96, 102, 130, 102, 146, 58, PAL_PURPLE);
      s.fillTriangle(112, 102, 122, 102, 141, 70, PAL_YELLOW);
      break;
    }
    case STK_FIRE: {
      float f1 = sinf(t / 90.0f), f2 = sinf(t / 63.0f + 1.0f);
      struct L { int w, h; uint8_t col; float jit; } layers[3] = {
        {64, 92, PAL_RED, 10}, {46, 70, PAL_ORANGE, 8}, {26, 44, PAL_YELLOW, 6}};
      for (auto& l : layers) {
        int base = 100, r = l.w / 2;
        int tipx = cx + (int)(f1 * l.jit), tipy = base - l.h + (int)(f2 * 4);
        s.fillCircle(cx, base - r, r, l.col);
        s.fillTriangle(cx - r, base - r, cx + r, base - r, tipx, tipy, l.col);
        s.fillTriangle(cx - r + 4, base - r - 6, cx - 2, base - r, cx - r - 6 - (int)(f2 * 5), tipy + l.h / 3, l.col);
        s.fillTriangle(cx + r - 4, base - r - 6, cx + 2, base - r, cx + r + 6 + (int)(f1 * 5), tipy + l.h / 3, l.col);
      }
      break;
    }
    case STK_SLEEP: {
      s.fillCircle(104, cy, 38, PAL_YELLOW);
      s.fillCircle(122, cy - 12, 34, PAL_BG);  // crescent moon
      int phase = (t / 450) % 4;
      for (int i = 0; i < phase; i++) {
        int zx = 150 + i * 18, zy = 58 - i * 18, z = 10 + i * 4;
        for (int w = 0; w < 3; w++) {
          s.drawLine(zx, zy + w, zx + z, zy + w, PAL_WHITE);
          s.drawLine(zx + z, zy + w, zx, zy + z + w - 2, PAL_WHITE);
          s.drawLine(zx, zy + z + w - 2, zx + z, zy + z + w - 2, PAL_WHITE);
        }
      }
      const int st[3][2] = {{30, 20}, {60, 88}, {210, 90}};
      for (int i = 0; i < 3; i++) if (((t / 400) + i) % 2) drawSparkle(s, st[i][0], st[i][1], 3, PAL_WHITE);
      break;
    }
    case STK_HUG: {
      float a = t / 320.0f;
      int ox = (int)(cosf(a) * 30), oy = (int)(sinf(a) * 10);
      bool pinkFront = sinf(a) > 0;
      if (pinkFront) {
        drawHeart(s, cx - ox, cy - oy, 56, PAL_RED);
        drawHeart(s, cx + ox, cy + oy, 60, PAL_PINK);
      } else {
        drawHeart(s, cx + ox, cy + oy, 60, PAL_PINK);
        drawHeart(s, cx - ox, cy - oy, 56, PAL_RED);
      }
      break;
    }
    default:
      break;
  }
}
