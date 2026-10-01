#pragma once

#include <stddef.h>

// Airline logos are fetched on demand (Phase 9), so nothing has to be flashed except the firmware - ported
// from ../../mlb-board's LogoStore (team logos), simplified: no MLB-style disc-colour sampling (airline
// logos aren't composited onto anything but a plain white tile) and no pruning (an airline logo is ~9.8 KB;
// even a few dozen distinct carriers cached is a rounding error against the ~1.9 MiB LittleFS partition -
// not worth the extra bookkeeping mlb-board's team-logo cache needs).
//
// For each airline code a screen needs, the board downloads a 70x70 PNG (Google's flight-logo CDN - see
// FlightScreen.cpp), decodes it with RgbaPng (now shared via ../../cyd-common, extended there with RGB
// colour-type support for exactly this real-world coverage gap), composites it onto white, and stores the
// result in LittleFS as an LVGL RGB565 .bin ("/logos/{code}.bin", ~9.8 KB). Until a logo exists (or an
// airline code gstatic doesn't recognize, which fails to decode - see RgbaPng.h's comment), the caller
// falls back to text-only (see FlightScreen.cpp).
namespace LogoStore {

constexpr int kMaxWanted = 8;  // comfortably more distinct airlines than fit on screen across both cards

// "L:/logos/{code}.bin" - ready for lv_image_set_src().
void fsPath(const char *code, char *out, size_t n);

// True if the logo file for this airline is already in flash.
bool have(const char *code);

// Adds `code` to the wanted set (cheap, dedups; call every time a card is about to show this code -
// e.g. every FlightScreen layout, not just once). Call once from setup() after LittleFS is mounted.
void want(const char *code);
void begin();

// Called from the network task (a download blocks for ~1-2 s, longer on a bad network): installs at most
// ONE missing logo per call, only while WiFi is connected. Failed downloads back off exponentially.
// Returns true when a logo was just installed.
bool service();

// Main task: true once after a logo was installed, so the UI should re-check have()/re-set image sources.
bool takeInstalled();

#ifdef TEST_HOOKS
// Bench tests: make installs of one code fail (nullptr/empty = none), and delete every stored logo.
void testFailCode(const char *code);
void testWipe();
#endif

}  // namespace LogoStore
