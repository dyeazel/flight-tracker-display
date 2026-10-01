#pragma once

// Polar (bearing/distance/heading/speed) to screen-cartesian conversion for the radar view (Phase 10).
// Pure logic (no Arduino/LVGL) so it's unit-tested on the host: pio test -e native. North-up, fixed
// orientation (not track-up) - bearing 0 = up (-y), 90 = right (+x), matching every other bearing use in
// this app (FlightMath::bearingDeg/cardinalDirection).
namespace RadarMath {

struct Point {
    float x = 0, y = 0;  // pixels, relative to the radar's center (home location)
    bool clipped = false;  // true if the real position was beyond maxRadiusPx and got pulled in to the edge
};

// Projects a flight at `bearingDeg`/`distanceMi` from home onto the screen, scaled by `pxPerMile`. If the
// scaled distance would exceed `maxRadiusPx` (the outermost ring - a flight can be farther than 50mi
// whenever the configured search radius is wider), the point is clipped to the ring's edge along the same
// bearing instead of drawn off-screen, with `clipped` set so the caller can style it distinctly.
Point project(float bearingDeg, float distanceMi, float pxPerMile, float maxRadiusPx);

// The endpoint (relative to the square's own center, i.e. a delta) of the heading line, pointing along
// `headingDeg` with a length proportional to `speedKt` (capped at `maxLenPx` so a fast jet doesn't draw
// off past the rings). {0, 0} if `hasHeading` is false - caller should skip drawing the line entirely.
Point headingVector(bool hasHeading, float headingDeg, float speedKt, float pxPerKt, float maxLenPx);

}  // namespace RadarMath
