#include "Gesture.h"

#include <stdlib.h>

namespace Gesture {

namespace {
Action classifyZone(int x, int y) {
    if (x < kEdgeZone) return Action::ModePrev;
    if (x >= kScreenW - kEdgeZone) return Action::ModeNext;
    if (y < kTopBottomZone) return Action::PagePrev;
    if (y >= kScreenH - kTopBottomZone) return Action::PageNext;
    return Action::None;  // center tap - ignored
}
}  // namespace

Action Tracker::feed(bool pressed, int x, int y, uint32_t nowMs) {
    if (pressed) {
        if (!down_) {  // discard the settling sample
            down_ = true;
            started_ = false;
            return Action::None;
        }
        if (!started_) {
            started_ = true;
            x0_ = x;
            y0_ = y;
            t0_ = nowMs;
        }
        x1_ = x;
        y1_ = y;
        return Action::None;
    }
    if (!down_) return Action::None;
    down_ = false;
    if (!started_) return Action::None;

    const int dx = x1_ - x0_;
    const int dy = y1_ - y0_;
    const uint32_t dt = nowMs - t0_;
    if (abs(dx) > kTapMaxMove || abs(dy) > kTapMaxMove || dt > kTapMaxMs) return Action::None;

    return classifyZone(x0_, y0_);
}

}  // namespace Gesture
