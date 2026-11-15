// Small UI toolkit for the workshop harness: a status bar, a scrolling log canvas and a
// row of touch buttons. Portrait layout (rotation 1): USB at the bottom, mics on the right.
#pragma once
#include <M5Unified.h>
#include <stdarg.h>

namespace ui {

static const int BAR_H = 28;     // status bar height
static const int BTN_H = 52;     // button row height
static int W = 240, H = 320;

// Button colours chosen to read well on the CoreS3 panel (pure TFT_BLUE is nearly black).
static const uint32_t COL_GREEN = 0x0560;   // rgb(0,172,0)
static const uint32_t COL_BLUE  = 0x2D7F;   // rgb(40,175,255)
static const uint32_t COL_RED   = 0xD000;   // rgb(208,0,0)
static const uint32_t COL_GREY  = 0x632C;   // rgb(96,100,96)
static const uint32_t COL_PURPLE= 0x9015;   // rgb(144,0,168)

// ---- status bar --------------------------------------------------------------
inline void status(const char *text, uint32_t bg = TFT_NAVY) {
  M5.Display.fillRect(0, 0, W, BAR_H, bg);
  M5.Display.setTextColor(TFT_WHITE, bg);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(6, 6);
  M5.Display.print(text);
}

// ---- scrolling log -----------------------------------------------------------
static M5Canvas logCanvas(&M5.Display);
static uint32_t logColor = TFT_LIGHTGREY;   // set before ui::log() to colour a line

inline void logInit() {
  logCanvas.setPsram(true);
  logCanvas.setColorDepth(8);
  logCanvas.createSprite(W, H - BAR_H - BTN_H);
  logCanvas.setTextSize(1);
  logCanvas.setTextWrap(true, true);
  logCanvas.setTextScroll(true);
  logCanvas.setScrollRect(0, 0, W, H - BAR_H - BTN_H, TFT_BLACK);
  logCanvas.fillScreen(TFT_BLACK);
  logCanvas.setCursor(2, 2);
  logCanvas.pushSprite(0, BAR_H);
}

inline void logClear() {
  logCanvas.fillScreen(TFT_BLACK);
  logCanvas.setCursor(2, 2);
  logCanvas.pushSprite(0, BAR_H);
}

// Print a line to the screen log and to serial.
inline void log(const char *fmt, ...) {
  char buf[700];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.println(buf);
  logCanvas.setTextColor(logColor, TFT_BLACK);
  logCanvas.println(buf);
  logCanvas.pushSprite(0, BAR_H);
}

// ---- buttons -----------------------------------------------------------------
struct Button {
  const char *label;
  int x, y, w, h;
  uint32_t color;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
  void draw(bool pressed = false) const {
    uint32_t bg = pressed ? TFT_WHITE : color;
    M5.Display.fillRoundRect(x, y, w, h, 8, bg);
    M5.Display.setTextColor(pressed ? TFT_BLACK : TFT_WHITE, bg);
    M5.Display.setFont(&fonts::DejaVu12);
    M5.Display.setTextSize(1);
    int tw = M5.Display.textWidth(label);
    int th = M5.Display.fontHeight();
    M5.Display.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    M5.Display.print(label);
    M5.Display.setFont(&fonts::Font0);
  }
};

// Lay out n buttons evenly across the bottom row.
inline void layoutButtons(Button *btns, int n) {
  int gap = 6;
  int bw = (W - gap * (n + 1)) / n;
  for (int i = 0; i < n; i++) {
    btns[i].x = gap + i * (bw + gap);
    btns[i].y = H - BTN_H + 4;
    btns[i].w = bw;
    btns[i].h = BTN_H - 8;
    btns[i].draw();
  }
}

// Returns the index of the button that was just tapped, or -1. Call after M5.update().
inline int tappedButton(const Button *btns, int n) {
  auto t = M5.Touch.getDetail();
  if (!t.wasClicked()) return -1;
  for (int i = 0; i < n; i++)
    if (btns[i].contains(t.x, t.y)) return i;
  return -1;
}

inline void begin() {
  M5.Display.setRotation(1);
  W = M5.Display.width();
  H = M5.Display.height();
  M5.Display.fillScreen(TFT_BLACK);
  logInit();
}

} // namespace ui
