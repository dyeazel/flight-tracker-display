#pragma once

#include "config/ConfigStore.h"
#include "flight/Flight.h"
#include "flight/FlightSelector.h"
#include "input/Gesture.h"

// LVGL flight display (Phase 9/10): replaces Renderer.cpp's raw TFT_eSPI draws. All widgets for all four
// modes are created once in begin(); update() only restyles/retexts/repositions them (mirrors
// ../../mlb-board's Scoreboard.h "create once" rule) - never created/destroyed per poll, so switching modes
// is just showing/hiding each mode's root container.
//
// Four modes (ConfigStore::DisplayMode), cycled with a left/right-edge tap (Gesture::Action::ModePrev/
// ModeNext - see applyGesture()); a top/bottom-edge tap pages through flights that don't fit the current
// mode's screen (PagePrev/PageNext):
//   - MaxDensity: same MAX_ROWS (6) layout as the old Renderer.cpp, ported field-for-field (UNSCII_8 is a
//     true monospace bitmap font, so the original 8px-per-character column math still applies exactly).
//   - TwoUp: 2 flights per page, larger text, each with its airline logo (see LogoStore.h).
//   - OneUp: 1 flight per page, biggest/most detailed view.
//   - Radar: bearing/distance polar view (see RadarScreen.h) - no paging (shows everything at once).
// A tracked flight not currently visible (Phase 8's classifyMissing()) is folded in wherever that mode has
// room: MaxDensity gets a compact status line; TwoUp/OneUp get trailing pages, one entry per page.
namespace FlightScreen {

void begin();

// Full-screen status text (Setup needed / Connecting / Syncing time / Error / Loading), replacing whatever
// mode is showing - mirrors ../../mlb-board's Scoreboard::showMessage. line2 is optional (nullptr to omit),
// shown in `line2Color` (0 = default white).
void showMessage(const char *line1, const char *line2 = nullptr, uint32_t line2Color = 0);

// Adopts a fresh published snapshot and redraws whichever mode/page is current. Cheap to call every
// redraw tick (both real polls and dead-reckoning ticks) - widget text is only as expensive to update as
// the strings themselves; there's no separate "just the value line" fast path the way the old Renderer
// needed (LVGL only re-flushes a label's own invalidated area, not the whole screen).
void update(Flight *flights, int count, const char missing[][12], const FlightSelector::MissingStatus *missingStatus,
            const time_t *missingWhenUtc, int missingCount, bool anyFlagged);

// ModePrev/ModeNext switches (and persists, via ConfigStore::setDisplayMode) the density mode; PagePrev/
// PageNext moves the current page within it (wrapping). None is a no-op. Redraws immediately using the
// last data passed to update().
void applyGesture(Gesture::Action action);

// Heartbeat overlay, top-right corner of whichever mode is showing - same meaning as the old Renderer's:
// toggling green/black normally, steady yellow while a poll is in flight, steady red when the on-screen
// data is real but stale. drawHeartbeat() toggles and is called from the same place the old Renderer's
// drawHeartbeat() was (every update()); drawRefreshing()/drawStale() override it until the next update().
void drawRefreshing();
void drawStale();

// WiFi link status icon, top-right (../../mlb-board's Scoreboard::setWifiStatus port): green while
// connected, red with a slash through it while not. Cheap to call every redraw tick - a no-op restyle
// unless the state actually changed.
void setWifiStatus(bool connected);

#ifdef TEST_HOOKS
// Raw text of the TwoUp/OneUp card detail label currently on screen (slot 0/1 for TwoUp, the sole card for
// OneUp) - lets a bench script verify the composed markup without OCR'ing a screenshot. Returns "" if that
// mode/slot isn't built or has nothing shown.
const char *debugCardText(int slot);
#endif

}  // namespace FlightScreen
