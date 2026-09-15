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
