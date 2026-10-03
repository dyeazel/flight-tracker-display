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

struct Rect {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // inclusive-ish pixel bounds, screen coordinates
};

// One flight's label-placement inputs: its marker center, its heading-vector delta ({0,0} = no vector),
// and the size of its (already measured) text block.
struct LabelInput {
    float cx = 0, cy = 0;
    float vx = 0, vy = 0;
    float w = 0, h = 0;
};

// Picks a label rectangle for each of `n` flights so that, as far as the 8 candidate spots around each marker
// allow (preferred side, opposite side, above, below, four corners), a label never sits on its own or any
// other flight's heading vector, never overlaps another flight's label or marker square (`markerHalf` is the
// square's half-size), never overlaps a fixed `obstacles` rect (e.g. an airport's "+" and code), and stays
// inside the `areaW` x `areaH` screen. The preferred side is toward the screen's center-line (left for a
// marker right of center), matching the original fixed rule - candidates only move away from it when
// that spot actually collides with something. Greedy start, then a few coordinate-descent passes so an
// early flight's choice can yield to a later one's. Best-effort: if nothing fits, the least-bad spot wins.
void placeLabels(const LabelInput *in, int n, float areaW, float areaH, float markerHalf, const Rect *obstacles,
                 int nObstacles, Rect *out);

}  // namespace RadarMath
