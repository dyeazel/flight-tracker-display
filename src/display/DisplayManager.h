#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

// Thin wrapper around TFT_eSPI, matching the small set of drawing
// primitives cyd_nearby_flights.py's rdagger ili9341 driver used
// (clear/draw_text8x8/fill_rectangle/fill_polygon), so Renderer can port
// 1:1 without caring about the underlying library.
//
// IMPORTANT: this board's panel is a mislabeled ILI9342 clone (see
// HANDOFF.md in the MicroPython project for the forensic history) - the
// TFT_eSPI driver/rotation/RGB-order combination that produces a correct,
// non-mirrored, correctly-colored image must be re-verified empirically on
// real hardware (Milestone 2's whole purpose), not assumed from the
// MicroPython config's rotation=0/bgr=False values, which don't map 1:1
// onto TFT_eSPI's own MADCTL handling.
class DisplayManager {
public:
    void begin();

    int width() { return tft_.width(); }
    int height() { return tft_.height(); }

    void clear(uint16_t color);

    // Draws text with a fixed 8px advance per character (regardless of the
    // underlying font's natural glyph width) so the column-layout math
    // ported from draw_value_line/render (VALUE_X, CALLSIGN_COL_CHARS,
    // etc. - all built around an assumed 8px/char grid) lines up exactly
    // as it did under the MicroPython 8x8 bitmap font.
    void drawText8x8(int x, int y, const char *text, uint16_t fg, uint16_t bg);

    void fillRect(int x, int y, int w, int h, uint16_t color);

    // Small filled triangle pointing up (pointingUp=true) or down, centered
    // at (cx, cy) with the given circumradius - used for the climb/descend
    // trend icon. Replaces the Python driver's fill_polygon(3, ...).
    void fillTriangle(int cx, int cy, int radius, bool pointingUp, uint16_t color);

    // RGB565 colors, computed once in begin().
    uint16_t white, black, green, gray, red, yellow;

#ifdef SCREEN_CAPTURE
    // Streams the panel's current contents over serial as "#S x y w h <hex>" lines (RGB565, low byte
    // first), 8 rows per line, ~27 s at 115200 baud. Reads the actual panel back, so it shows exactly what
    // is on the glass. Blocks the caller for the duration (marks watchdog progress per strip).
    void dumpScreen();
#endif

private:
    TFT_eSPI tft_;
};
