#include "DisplayManager.h"

void DisplayManager::begin() {
    tft_.init();
    // Found empirically on real hardware (interactive rotation/color test,
    // see git history for src/main.cpp's milestone-2c test harness): this
    // panel needs TFT_eSPI's ILI9341_2_DRIVER (not plain ILI9341_DRIVER -
    // the plain one reported correct width/height for rotation=1 but the
    // physical chip didn't actually reconfigure its scan order, same class
    // of ILI9342-clone MADCTL mismatch as the MicroPython version's ili9341
    // driver hit) plus TFT_INVERSION_ON (colors were exact bitwise inverses
    // without it - not an RGB/BGR order issue) plus rotation=1 for
    // landscape. Both driver/inversion flags live in platformio.ini's
    // build_flags. Confirmed to match the MicroPython version's on-screen
    // orientation.
    tft_.setRotation(1);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    white = tft_.color565(255, 255, 255);
    black = tft_.color565(0, 0, 0);
    green = tft_.color565(0, 255, 0);
    gray = tft_.color565(150, 150, 150);
    red = tft_.color565(255, 60, 60);
    yellow = tft_.color565(255, 210, 0);

    tft_.fillScreen(black);
}

void DisplayManager::clear(uint16_t color) { tft_.fillScreen(color); }

void DisplayManager::drawText8x8(int x, int y, const char *text, uint16_t fg, uint16_t bg) {
    tft_.setTextFont(1);
    for (int i = 0; text[i] != '\0'; i++) {
        tft_.drawChar(x + i * 8, y, text[i], fg, bg, 1);
    }
}

void DisplayManager::fillRect(int x, int y, int w, int h, uint16_t color) {
    tft_.fillRect(x, y, w, h, color);
}

void DisplayManager::fillTriangle(int cx, int cy, int radius, bool pointingUp, uint16_t color) {
    if (pointingUp) {
        tft_.fillTriangle(cx, cy - radius, cx - radius, cy + radius, cx + radius, cy + radius, color);
    } else {
        tft_.fillTriangle(cx, cy + radius, cx - radius, cy - radius, cx + radius, cy - radius, color);
    }
}

#ifdef SCREEN_CAPTURE
#include "watchdog/WatchdogManager.h"

// Panel readback on this board (ILI9341_2 clone) reports each pixel's RED channel one pixel late: the value
// read at position n carries the green/blue of pixel n but the red of pixel n-1. Found with a bench test
// (1,224 of 1,280 random-colour pixels wrong raw, 0 wrong once corrected, twice). It also explains why
// readPixel() returned wrong reds. So pixel n's true red is taken from the read at n+1: each strip reads
// one extra row (a linear burst, so the first pixel of the next row supplies the last pixel's red).
// readRect() also returns byte-swapped RGB565 (TFT_eSPI's pushImage convention), swapped back here.
//
// Known limit: the very last pixel of the screen (bottom-right) has no following pixel to borrow red
// from, so it takes its red from the previous pixel - the corner is background black in every screen.
void DisplayManager::dumpScreen() {
    constexpr int kStrip = 8;
    constexpr int kMaxW = 320;
    static uint16_t buf[kMaxW * (kStrip + 1)];
    auto swap16 = [](uint16_t v) -> uint16_t { return (uint16_t)((v >> 8) | (v << 8)); };
    const int W = tft_.width() < kMaxW ? tft_.width() : kMaxW;
    const int H = tft_.height();
    static const char *hex = "0123456789abcdef";

    for (int y = 0; y < H; y += kStrip) {
        const int rows = (H - y) < kStrip ? (H - y) : kStrip;
        const bool lastStrip = (y + rows >= H);
        const int readRows = lastStrip ? rows : rows + 1;
        tft_.readRect(0, y, W, readRows, buf);

        Serial.printf("#S 0 %d %d %d ", y, W, rows);
        char chunk[128];
        int len = 0;
        const int count = W * rows, total = W * readRows;
        for (int n = 0; n < count; n++) {
            const uint16_t raw = swap16(buf[n]);
            const uint16_t next = (n + 1 < total) ? swap16(buf[n + 1]) : raw;
            const uint16_t px = (uint16_t)((next & 0xF800) | (raw & 0x07FF));
            const uint8_t lo = px & 0xFF, hi = px >> 8;  // low byte first
            chunk[len++] = hex[lo >> 4];
            chunk[len++] = hex[lo & 15];
            chunk[len++] = hex[hi >> 4];
            chunk[len++] = hex[hi & 15];
            if (len >= (int)sizeof(chunk) - 4) {
                Serial.write((const uint8_t *)chunk, len);
                len = 0;
            }
        }
        if (len) Serial.write((const uint8_t *)chunk, len);
        Serial.print('\n');
        WatchdogManager::markProgress();
    }
}
#endif
