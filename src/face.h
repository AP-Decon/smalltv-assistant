#pragma once
// Animated robot-eyes face, drawn into a 4-bit 240x104 sprite.
#if __has_include(<TFT_eSPI.h>)
#include <TFT_eSPI.h>
#else
#include "c2_shim.h"  // ESP32-C2 (ESPHome) build
#endif
#include <algorithm>
#include "common.h"

struct FaceAnim {
  float gx = 0, gy = 0;      // current gaze offset
  float tx = 0, ty = 0;      // target gaze offset
  uint32_t nextGaze = 0;
  uint32_t nextBlink = 0;
  uint32_t blinkStart = 0;
  bool blinking = false;
};

// 1.0 = open, ~0 = closed
inline float blinkFactor(FaceAnim& a, uint32_t now, uint8_t mood) {
  if (!a.blinking && (int32_t)(now - a.nextBlink) >= 0) {
    a.blinking = true;
    a.blinkStart = now;
  }
  if (!a.blinking) return 1.0f;
  uint32_t dur = (mood == MOOD_SLEEPY) ? 700 : 170;
  uint32_t t = now - a.blinkStart;
  if (t >= dur) {
    a.blinking = false;
    a.nextBlink = now + (random(0, 7) == 0 ? 160 : random(1800, 5500));  // sometimes a double blink
    return 1.0f;
  }
  float p = (float)t / dur;
  float f = p < 0.5f ? 1.0f - p * 2.0f : (p - 0.5f) * 2.0f;
  return f < 0.08f ? 0.08f : f;
}

inline void updateGaze(FaceAnim& a, uint32_t now, uint8_t mood, bool small) {
  float maxX = small ? 14 : 22, maxY = small ? 5 : 8;
  if (mood == MOOD_THINKING) { a.tx = maxX * 0.8f; a.ty = -maxY; }
  else if (mood == MOOD_SLEEPY) { a.tx = 0; a.ty = maxY * 0.6f; }
  else if ((int32_t)(now - a.nextGaze) >= 0) {
    a.nextGaze = now + random(900, 3500);
    if (random(0, 3) == 0) { a.tx = 0; a.ty = 0; }
    else { a.tx = random(-100, 101) / 100.0f * maxX; a.ty = random(-100, 101) / 100.0f * maxY; }
    if (small) a.ty = maxY * 0.7f;  // glance down at the speech bubble
  }
  a.gx += (a.tx - a.gx) * 0.25f;
  a.gy += (a.ty - a.gy) * 0.25f;
}

inline void drawHeart(TFT_eSprite& s, int x, int y, int size, uint8_t col) {
  int r = size / 4 + 1;
  s.fillCircle(x - size / 4, y - size / 6, r, col);
  s.fillCircle(x + size / 4, y - size / 6, r, col);
  s.fillTriangle(x - size / 2, y - size / 8, x + size / 2, y - size / 8, x, y + size / 2, col);
}

inline void drawEye(TFT_eSprite& s, int cx, int cy, int ew, int eh, uint8_t mood, float blink, bool left, uint32_t now) {
  uint8_t col = PAL_EYE;
  if (mood == MOOD_ANGRY) col = PAL_RED;

  if (mood == MOOD_LOVE) {
    float pulse = 1.0f + 0.08f * sinf(now / 120.0f);
    int size = (int)(ew * 1.05f * pulse);
    if (blink < 0.5f) { s.fillRoundRect(cx - ew / 2, cy - 3, ew, 6, 3, PAL_PINK); return; }
    drawHeart(s, cx, cy, size, PAL_PINK);
    return;
  }
  if (mood == MOOD_SURPRISED) { ew = ew * 110 / 100; eh = eh * 115 / 100; }
  if (mood == MOOD_SLEEPY) eh = eh / 4;
  if (mood == MOOD_THINKING && left) eh = eh * 85 / 100;

  int h = (int)(eh * blink);
  if (h < 5) h = 5;
  int x = cx - ew / 2, y = cy - h / 2;
  int r = (mood == MOOD_SURPRISED) ? std::min(ew, h) / 2 : std::min(ew, h) / 4;
  s.fillRoundRect(x, y, ew, h, r, col);

  if (blink < 0.6f) return;  // shapes below only matter when the eye is open

  if (mood == MOOD_HAPPY) {
    // cut the bottom away with a circle -> "^" smiling eyes
    s.fillCircle(cx, cy + h * 55 / 100, ew * 62 / 100, PAL_BG);
  } else if (mood == MOOD_SAD || mood == MOOD_ANGRY) {
    // slanted lid: sad droops at the outer corner, angry at the inner corner
    bool cutLeftLow = (mood == MOOD_SAD) ? left : !left;
    int top = y - 1, l = x - 1, rr = x + ew + 1, drop = h * 45 / 100;
    if (cutLeftLow) s.fillTriangle(l, top, rr, top, l, top + drop, PAL_BG);
    else s.fillTriangle(l, top, rr, top, rr, top + drop, PAL_BG);
  }
}

// small = message mode (face shrinks to make room for the speech bubble)
inline void drawFace(TFT_eSprite& s, FaceAnim& a, uint8_t mood, bool small, uint32_t now) {
  s.fillSprite(PAL_BG);
  updateGaze(a, now, mood, small);
  float blink = blinkFactor(a, now, mood);

  int ew = small ? 50 : 66;
  int eh = small ? 56 : 78;
  int gap = small ? 34 : 40;
  int cx = 120 + (int)a.gx;
  int cy = 52 + (int)a.gy;
  int lx = cx - gap / 2 - ew / 2;
  int rx = cx + gap / 2 + ew / 2;

  drawEye(s, lx, cy, ew, eh, mood, blink, true, now);
  drawEye(s, rx, cy, ew, eh, mood, blink, false, now);

  if (mood == MOOD_SLEEPY) {
    // little floating z's
    int phase = (now / 400) % 3;
    for (int i = 0; i <= phase; i++) {
      int zx = 196 + i * 10, zy = 30 - i * 10, z = 6 + i * 2;
      s.drawLine(zx, zy, zx + z, zy, PAL_DIM);
      s.drawLine(zx + z, zy, zx, zy + z, PAL_DIM);
      s.drawLine(zx, zy + z, zx + z, zy + z, PAL_DIM);
    }
  }
}
