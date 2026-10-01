#pragma once

#include <stdint.h>

// Turns a stream of touch samples (see input/TouchPanel.h) into a density-mode/page action for
// FlightScreen (Phase 9/10). Pure logic (no Arduino/LVGL) so it is unit-tested on the host: pio test
// -e native. Confirmed with the user: left/right screen edge cycles the density mode itself (max
// density/two-per/one-per/radar); top/bottom edge pages through whichever flights don't fit on the
// current mode's screen. A center tap is ignored.
//
// Recognised: a short (<= kTapMaxMs), nearly stationary (<= kTapMaxMove px) touch, classified by where it
// started - not mlb-board's Gesture module, which instead recognises swipes for a previous/next action;
// this app's zones are a 2D (mode x page) grid, not a single previous/next axis, so it's a new, smaller
// module rather than a variant of that one. A tap in a corner (inside both an edge band and a top/bottom
// band) resolves to the left/right (mode) action - checked first, arbitrarily but consistently.
//
// The first sample of each touch is discarded (the XPT2046 reports a bogus extreme position while its
// pressure reading settles - seen on hardware, same as mlb-board's Gesture).
namespace Gesture {

enum class Action : uint8_t { None, ModePrev, ModeNext, PagePrev, PageNext };

constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
constexpr int kTapMaxMove = 15;
constexpr uint32_t kTapMaxMs = 400;
constexpr int kEdgeZone = 56;       // left/right screen-edge band width - mode cycling
constexpr int kTopBottomZone = 40;  // top/bottom screen-edge band height - paging

class Tracker {
public:
    // Feed each poll. pressed = finger currently down; x, y in screen pixels (ignored when !pressed).
    // Returns the action when the finger lifts, else None.
    Action feed(bool pressed, int x, int y, uint32_t nowMs);

private:
    bool down_ = false;
    bool started_ = false;  // false until the 2nd sample: the XPT2046's first reading after touch-down is unreliable
    int x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0;
    uint32_t t0_ = 0;
};

}  // namespace Gesture
