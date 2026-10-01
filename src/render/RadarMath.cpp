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

}  // namespace RadarMath
