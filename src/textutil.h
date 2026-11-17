#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>

// The display fonts only contain printable ASCII, so fold UTF-8 text
// (smart quotes, dashes, accents) to ASCII and drop anything else (emoji).
inline String asciiFold(const String& in) {
  static const char LATIN1[] =
      "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPs"   // U+00C0..U+00DF
      "aaaaaaaceeeeiiiidnooooo/ouuuuypy";  // U+00E0..U+00FF
  String out;
  out.reserve(in.length());
  const uint8_t* p = (const uint8_t*)in.c_str();
  size_t n = in.length(), i = 0;
  while (i < n) {
    uint8_t c = p[i];
    uint32_t cp;
    int len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xE0) == 0xC0 && i + 1 < n) { cp = ((c & 0x1F) << 6) | (p[i + 1] & 0x3F); len = 2; }
    else if ((c & 0xF0) == 0xE0 && i + 2 < n) { cp = ((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F); len = 3; }
    else if ((c & 0xF8) == 0xF0 && i + 3 < n) { cp = 0x1F000; len = 4; }  // emoji etc: dropped
    else { i++; continue; }
    i += len;

    if (cp == '\n') { out += '\n'; continue; }
    if (cp == '\t' || cp == '\r') { out += ' '; continue; }
    if (cp >= 0x20 && cp < 0x7F) { out += (char)cp; continue; }
    if (cp >= 0xC0 && cp <= 0xFF) { out += LATIN1[cp - 0xC0]; continue; }
    switch (cp) {
      case 0x00A0: out += ' '; break;
      case 0x2018: case 0x2019: case 0x201B: case 0x2032: out += '\''; break;
      case 0x201C: case 0x201D: case 0x2033: out += '"'; break;
      case 0x2013: case 0x2014: case 0x2212: out += '-'; break;
      case 0x2026: out += "..."; break;
      case 0x2022: case 0x00B7: out += '*'; break;
      case 0x00A3: out += "GBP"; break;
      case 0x20AC: out += "EUR"; break;
      case 0x00B0: out += "deg"; break;
      default: break;  // unsupported symbol
    }
  }
  // collapse runs of spaces
  String clean;
  clean.reserve(out.length());
  char prev = 0;
  for (size_t k = 0; k < out.length(); k++) {
    char ch = out[k];
    if (ch == ' ' && (prev == ' ' || prev == '\n')) continue;
    clean += ch;
    prev = ch;
  }
  clean.trim();
  return clean;
}

// Trim a string with "..." so it fits maxW pixels in the current font.
inline String fitWidth(TFT_eSPI& tft, String s, int maxW) {
  if (tft.textWidth(s) <= maxW) return s;
  while (s.length() > 1 && tft.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
  s.trim();
  return s + "...";
}

// Word-wrap text in the current font. Returns at most maxLines lines;
// the last line gets "..." if the text did not fit.
inline std::vector<String> wrapText(TFT_eSPI& tft, const String& text, int maxW, int maxLines, bool* overflow = nullptr) {
  std::vector<String> lines;
  String line;
  bool over = false;
  size_t i = 0, n = text.length();

  auto pushLine = [&](const String& l) -> bool {
    if ((int)lines.size() >= maxLines) { over = true; return false; }
    lines.push_back(l);
    return true;
  };

  while (i <= n) {
    // read next word (or newline)
    if (i < n && text[i] == '\n') {
      if (!pushLine(line)) break;
      line = "";
      i++;
      continue;
    }
    size_t j = i;
    while (j < n && text[j] != ' ' && text[j] != '\n') j++;
    String word = text.substring(i, j);
    i = (j < n && text[j] == ' ') ? j + 1 : j;
    if (word.length() == 0) { if (j >= n) break; continue; }

    String candidate = line.length() ? line + " " + word : word;
    if (tft.textWidth(candidate) <= maxW) {
      line = candidate;
    } else {
      if (line.length()) {
        if (!pushLine(line)) break;
        line = "";
      }
      // word longer than a full line: hard-break it
      while (tft.textWidth(word) > maxW && word.length() > 1) {
        size_t k = word.length();
        while (k > 1 && tft.textWidth(word.substring(0, k)) > maxW) k--;
        if (!pushLine(word.substring(0, k))) { word = ""; break; }
        word = word.substring(k);
      }
      if (over) break;
      line = word;
    }
    if (j >= n) break;
  }
  if (!over && line.length()) pushLine(line);

  if (over && !lines.empty()) {
    String l = lines.back();
    while (l.length() && tft.textWidth(l + "...") > maxW) l.remove(l.length() - 1);
    l.trim();
    lines.back() = l + "...";
  }
  if (overflow) *overflow = over;
  return lines;
}
