#include "RadarMath.h"

#include <math.h>

namespace RadarMath {

namespace {
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
}

Point project(float bearingDeg, float distanceMi, float pxPerMile, float maxRadiusPx) {
    const float rad = bearingDeg * kDegToRad;
    float x = distanceMi * pxPerMile * sinf(rad);
    float y = -(distanceMi * pxPerMile * cosf(rad));  // bearing 0 (north) is "up" on screen, i.e. -y

    Point p;
    const float r = sqrtf(x * x + y * y);
    if (r > maxRadiusPx && r > 0.0f) {
        const float scale = maxRadiusPx / r;
        x *= scale;
        y *= scale;
        p.clipped = true;
    }
    p.x = x;
    p.y = y;
    return p;
}

Point headingVector(bool hasHeading, float headingDeg, float speedKt, float pxPerKt, float maxLenPx) {
    Point v;
    if (!hasHeading) return v;
    float len = speedKt * pxPerKt;
    if (len > maxLenPx) len = maxLenPx;
    if (len < 0) len = 0;
    const float rad = headingDeg * kDegToRad;
    v.x = len * sinf(rad);
    v.y = -(len * cosf(rad));
    return v;
}

namespace {

constexpr int kMaxFlights = 16;  // placeLabels() working-set cap; callers pass at most MAX_ROWS (6)
constexpr int kCandidates = 8;
constexpr float kGap = 4.0f;  // marker square edge to label edge

float overlapArea(const Rect &a, const Rect &b) {
    const float w = (a.x2 < b.x2 ? a.x2 : b.x2) - (a.x1 > b.x1 ? a.x1 : b.x1);
    const float h = (a.y2 < b.y2 ? a.y2 : b.y2) - (a.y1 > b.y1 ? a.y1 : b.y1);
    return (w > 0 && h > 0) ? w * h : 0.0f;
}

// Liang-Barsky: does the segment (x0,y0)-(x1,y1) touch rect r?
bool segmentHitsRect(float x0, float y0, float x1, float y1, const Rect &r) {
    float t0 = 0.0f, t1 = 1.0f;
    const float dx = x1 - x0, dy = y1 - y0;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0 - r.x1, r.x2 - x0, y0 - r.y1, r.y2 - y0};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return false;
        } else {
            const float t = q[i] / p[i];
            if (p[i] < 0.0f) {
                if (t > t1) return false;
                if (t > t0) t0 = t;
            } else {
                if (t < t0) return false;
                if (t < t1) t1 = t;
            }
        }
    }
    return true;
}

// k = 0 preferred side, 1 opposite side, 2 above, 3 below, 4..7 the four corners.
Rect candidate(const LabelInput &f, int k, float areaW, float markerHalf) {
    const bool preferLeft = f.cx > areaW / 2;
    const float off = markerHalf + kGap;
    const float left = f.cx - off - f.w, right = f.cx + off;
    const float top = f.cy - off - f.h, bottom = f.cy + off;
    float x = 0, y = 0;
    switch (k) {
        case 0: x = preferLeft ? left : right; y = f.cy - f.h / 2; break;
        case 1: x = preferLeft ? right : left; y = f.cy - f.h / 2; break;
        case 2: x = f.cx - f.w / 2; y = top; break;
        case 3: x = f.cx - f.w / 2; y = bottom; break;
        case 4: x = preferLeft ? left : right; y = top; break;
        case 5: x = preferLeft ? left : right; y = bottom; break;
        case 6: x = preferLeft ? right : left; y = top; break;
        default: x = preferLeft ? right : left; y = bottom; break;
    }
    Rect r;
    r.x1 = x;
    r.y1 = y;
    r.x2 = x + f.w;
    r.y2 = y + f.h;
    return r;
}

float cost(int self, const Rect &r, int k, const LabelInput *in, int n, const Rect *cur, float areaW, float areaH,
           float markerHalf, const Rect *obstacles, int nObstacles) {
    float c = 2.0f * k;  // tie-breaker toward the original preferred spot
    const float overflow = (r.x1 < 0 ? -r.x1 : 0) + (r.x2 > areaW ? r.x2 - areaW : 0) + (r.y1 < 0 ? -r.y1 : 0) +
                           (r.y2 > areaH ? r.y2 - areaH : 0);
    c += 1000.0f * overflow;
    for (int j = 0; j < n; j++) {
        const LabelInput &o = in[j];
        if (o.vx != 0.0f || o.vy != 0.0f) {
            if (segmentHitsRect(o.cx, o.cy, o.cx + o.vx, o.cy + o.vy, r)) c += (j == self) ? 500.0f : 300.0f;
        }
        if (j == self) continue;
        const float a = overlapArea(r, cur[j]);
        if (a > 0) c += 200.0f + a;
        Rect m;
        m.x1 = o.cx - markerHalf;
        m.y1 = o.cy - markerHalf;
        m.x2 = o.cx + markerHalf;
        m.y2 = o.cy + markerHalf;
        if (overlapArea(r, m) > 0) c += 400.0f;
    }
    for (int j = 0; j < nObstacles; j++) {
        const float a = overlapArea(r, obstacles[j]);
        if (a > 0) c += 100.0f + a;
    }
    return c;
}

}  // namespace

void placeLabels(const LabelInput *in, int n, float areaW, float areaH, float markerHalf, const Rect *obstacles,
                 int nObstacles, Rect *out) {
    if (n > kMaxFlights) n = kMaxFlights;
    for (int i = 0; i < n; i++) out[i] = candidate(in[i], 0, areaW, markerHalf);
    for (int pass = 0; pass < 3; pass++) {
        bool changed = false;
        for (int i = 0; i < n; i++) {
            int best = 0;
            float bestCost = 0;
            for (int k = 0; k < kCandidates; k++) {
                const Rect r = candidate(in[i], k, areaW, markerHalf);
                const float c = cost(i, r, k, in, n, out, areaW, areaH, markerHalf, obstacles, nObstacles);
                if (k == 0 || c < bestCost) {
                    bestCost = c;
                    best = k;
                }
            }
            const Rect r = candidate(in[i], best, areaW, markerHalf);
            if (r.x1 != out[i].x1 || r.y1 != out[i].y1) changed = true;
            out[i] = r;
        }
        if (!changed) break;
    }
}

}  // namespace RadarMath
