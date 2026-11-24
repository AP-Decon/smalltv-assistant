#pragma once
// "Bit" - a 16x16 pixel-art robot, scaled up and drawn into the 4-bit sprite.
#if __has_include(<TFT_eSPI.h>)
#include <TFT_eSPI.h>
#else
#include "c2_shim.h"  // ESP32-C2 (ESPHome) build
#endif
#include "common.h"
#include "face.h"

// . transparent  K outline  W body  D face screen  C chest light  R antenna tip
static const char* const MASCOT_BASE[16] = {
  ".......RR.......",
  ".......KK.......",
  "...KKKKKKKKKK...",
  "..KWWWWWWWWWWK..",
  "..KWDDDDDDDDWK..",
  "..KWDDDDDDDDWK..",
  "..KWDDDDDDDDWK..",
  "..KWDDDDDDDDWK..",
  "..KWWWWWWWWWWK..",
  "...KKKKKKKKKK...",
  "....KWWWWWWK....",
  ".KKKWWWWWWWWKKK.",
  ".KWKWWCCCCWWKWK.",
  ".KKKWWWWWWWWKKK.",
  "....KWWKKWWK....",
  "....KKK..KKK....",
};


inline void mascotPx(TFT_eSprite& s, int ox, int oy, int sc, int r, int c, uint8_t col) {
  s.fillRect(ox + c * sc, oy + r * sc, sc, sc, col);
}

inline void drawMascotFace(TFT_eSprite& s, int ox, int oy, int sc, uint8_t mood, bool blink, uint32_t now) {
  auto px = [&](int r, int c, uint8_t col) { mascotPx(s, ox, oy, sc, r, c, col); };
  const uint8_t E = PAL_EYE;

  if (blink && mood != MOOD_LOVE) {
    px(6, 5, E); px(6, 6, E); px(6, 9, E); px(6, 10, E);
    return;
  }
  switch (mood) {
    case MOOD_HAPPY:
      px(5, 6, E); px(6, 5, E); px(6, 7, E);
      px(5, 9, E); px(6, 8, E); px(6, 10, E);
      px(7, 7, E); px(7, 8, E);
      break;
    case MOOD_SAD:
      px(6, 5, E); px(6, 10, E);
      px(5, 6, E); px(5, 9, E);  // inner brows raised
      px(7, 7, E); px(7, 8, E);
      if ((now / 500) % 2) px(7, 5, PAL_EYE);  // tear
      break;
    case MOOD_SURPRISED:
      px(5, 5, E); px(5, 6, E); px(6, 5, E); px(6, 6, E);
      px(5, 9, E); px(5, 10, E); px(6, 9, E); px(6, 10, E);
      px(7, 7, PAL_WHITE); px(7, 8, PAL_WHITE);
      break;
    case MOOD_THINKING:
      px(5, 6, E); px(5, 11, E); px(4, 6, E); px(4, 11, E);
      px(7, 8, E); px(7, 9, E);
      break;
    case MOOD_SLEEPY:
      px(6, 5, E); px(6, 6, E); px(6, 9, E); px(6, 10, E);
      break;
    case MOOD_LOVE: {
      const uint8_t P = PAL_PINK;
      px(4, 4, P); px(4, 6, P); px(5, 4, P); px(5, 5, P); px(5, 6, P); px(6, 5, P);
      px(4, 9, P); px(4, 11, P); px(5, 9, P); px(5, 10, P); px(5, 11, P); px(6, 10, P);
      px(7, 7, P); px(7, 8, P);
      break;
    }
    case MOOD_ANGRY:
      px(5, 5, PAL_RED); px(6, 5, PAL_RED); px(6, 6, PAL_RED);
      px(5, 10, PAL_RED); px(6, 9, PAL_RED); px(6, 10, PAL_RED);
      px(7, 7, PAL_RED); px(7, 8, PAL_RED);
      break;
    default:  // neutral
      px(5, 5, E); px(6, 5, E); px(5, 10, E); px(6, 10, E);
      px(7, 7, E); px(7, 8, E);
      break;
  }
}

// alert = a notification just arrived (bounce + exclamation mark)
inline void drawMascot(TFT_eSprite& s, FaceAnim& a, uint8_t mood, bool small, bool alert, uint32_t now) {
  s.fillSprite(PAL_BG);
  int sc = small ? 5 : 6;
  int w = 16 * sc;
  int ox = (240 - w) / 2;
  int bob;
  if (alert) bob = ((now / 120) % 2) ? 0 : 6;          // excited hop
  else if (mood == MOOD_SLEEPY) bob = 4;               // slumped
  else bob = ((now / 600) % 2) ? 2 : 0;                // gentle idle bob
  int oy = (small ? 10 : 2) + bob;

  float bf = blinkFactor(a, now, mood);

  for (int r = 0; r < 16; r++) {
    const char* row = MASCOT_BASE[r];
    for (int c = 0; c < 16; c++) {
      uint8_t col;
      switch (row[c]) {
        case 'K': col = PAL_OUTLINE; break;
        case 'W': col = PAL_BODY; break;
        case 'D': col = PAL_SCREEN; break;
        case 'C': col = ((now / 800) % 2) ? PAL_EYE : PAL_DIM; break;
        case 'R':
          if (alert) col = ((now / 150) % 2) ? PAL_YELLOW : PAL_RED;
          else col = ((now / 1000) % 2) ? PAL_RED : PAL_OUTLINE;
          break;
        default: continue;
      }
      mascotPx(s, ox, oy, sc, r, c, col);
    }
  }
  drawMascotFace(s, ox, oy, sc, mood, bf < 0.5f, now);

  if (alert) {
    int ex = ox + w + 10, ey = oy + 6;
    s.fillRect(ex, ey, sc, sc * 4, PAL_YELLOW);
    s.fillRect(ex, ey + sc * 5, sc, sc, PAL_YELLOW);
  }
  if (mood == MOOD_SLEEPY) {
    int phase = (now / 400) % 3;
    for (int i = 0; i <= phase; i++) {
      int zx = ox + w + 6 + i * 10, zy = oy + 20 - i * 10, z = 5 + i * 2;
      s.drawLine(zx, zy, zx + z, zy, PAL_DIM);
      s.drawLine(zx + z, zy, zx, zy + z, PAL_DIM);
      s.drawLine(zx, zy + z, zx + z, zy + z, PAL_DIM);
    }
  }
}
