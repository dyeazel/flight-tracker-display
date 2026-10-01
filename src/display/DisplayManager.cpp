#include "DisplayManager.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "display/PanelInit.h"
#ifdef SCREEN_CAPTURE
#include "watchdog/WatchdogManager.h"
#endif

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kBufLines = 16;  // trimmed from mlb-board's 24 - DRAM is tighter here (4 density modes + touch + logos)

TFT_eSPI tft;
#ifdef SCREEN_CAPTURE
bool gCapture = false;
#endif

// Static draw buffer: no heap use for rendering. RGB565 = 2 bytes/pixel.
alignas(4) uint8_t drawBuf[kWidth * kBufLines * 2];

void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
    const uint32_t w = lv_area_get_width(area);
    const uint32_t h = lv_area_get_height(area);
#ifdef SCREEN_CAPTURE
    // Captures the pixels LVGL is actually sending, not a panel readback (see dumpScreen()'s comment for
    // why this board needs that distinction) - the same technique ../../mlb-board's DisplayManager uses.
    if (gCapture) {
        Serial.printf("#S %d %d %u %u ", (int)area->x1, (int)area->y1, (unsigned)w, (unsigned)h);
        static const char *hex = "0123456789abcdef";
        char chunk[128];
        const uint8_t *b = pxMap;
        for (uint32_t i = 0; i < w * h * 2; i += 64) {
            const uint32_t n = (w * h * 2 - i) < 64 ? (w * h * 2 - i) : 64;
            for (uint32_t k = 0; k < n; k++) {
                chunk[2 * k] = hex[b[i + k] >> 4];
                chunk[2 * k + 1] = hex[b[i + k] & 15];
            }
            Serial.write((const uint8_t *)chunk, n * 2);
        }
        Serial.print('\n');
        if (area->y2 >= kHeight - 1) gCapture = false;  // the last strip of a full redraw
    }
#endif
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors(reinterpret_cast<uint16_t *>(pxMap), w * h, true);  // true = swap bytes
    tft.endWrite();
    lv_display_flush_ready(disp);
}

uint32_t tickCb() { return millis(); }

}  // namespace

namespace DisplayManager {

void begin() {
    // ../../cyd-common's shared panel bring-up (init/rotation/backlight) - see PanelInit.h for the
    // empirical rationale (ILI9341_2_DRIVER + TFT_INVERSION_ON + rotation 1).
    PanelInit::begin(tft);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb(tickCb);
    lv_display_t *disp = lv_display_create(kWidth, kHeight);
    lv_display_set_flush_cb(disp, flushCb);
    lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void tick() { lv_timer_handler(); }

#ifdef SCREEN_CAPTURE
// Captures at LVGL's flush time (flushCb() above), not via a panel readback the way the pre-LVGL version
// of this file did (tft.readRect() + an empirically-corrected red-channel-off-by-one quirk). Switched
// during Phase 9's bring-up: that readback path silently returned stale/wrong data specifically for
// LVGL's chunked partial-render flushes (a solid full-screen fill still read back fine - the quirk's
// correction assumed the old code's own draw/readback timing - but real content never appeared, with no
// error - only found by comparing against a flush-time capture, which showed the correct pixels). Forces
// a full redraw so every strip actually flushes.
void dumpScreen() {
    gCapture = true;
    lv_obj_invalidate(lv_screen_active());
    const uint32_t start = millis();
    while (gCapture && millis() - start < 10000) {
        lv_timer_handler();
        WatchdogManager::markProgress();
        delay(5);
    }
}
#endif

}  // namespace DisplayManager
