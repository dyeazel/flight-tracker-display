#pragma once

// TFT_eSPI panel init + LVGL 9 display glue (partial rendering, single buffer) - mirrors ../../mlb-board's
// DisplayManager.cpp (Phase 9: this app moved from raw TFT_eSPI draws to LVGL, for the density-layout/
// touch/logo work - see FlightScreen.h). Rotation is applied only here (setRotation(1) via PanelInit);
// LVGL is not rotated.
namespace DisplayManager {

void begin();

// Call every loop(); drives lv_timer_handler().
void tick();

#ifdef SCREEN_CAPTURE
// Debug builds: dumps the panel's contents over serial as hex lines "#S x y w h <RGB565 LE hex>"
// (tools/screenshot.py turns that into a PNG). Captures at LVGL's flush time (mirrors ../../mlb-board's
// approach) rather than reading the physical panel back afterward - see the .cpp for why: the pre-LVGL
// version of this file used a readRect()-based readback with an empirically-corrected red-channel quirk,
// which turned out not to survive the move to LVGL's chunked partial-render flushes (silently wrong, no
// error). Takes ~30s at 115200 baud.
void dumpScreen();
#endif

}  // namespace DisplayManager
