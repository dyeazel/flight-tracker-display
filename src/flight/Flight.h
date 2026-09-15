#pragma once

#include <Arduino.h>

// One tracked aircraft. Fixed-size char[] fields (not String) throughout -
// this struct lives in a per-loop hot path, and Arduino's String does its
// own heap alloc/realloc, which is exactly the kind of fragmentation this
// whole port exists to avoid. See config.h / MAX_ROWS for the fixed-size
// array this is stored in.
struct Flight {
    char callsign[12] = "";

    // Current position/altitude - live OpenSky value right after a real
    // fetch, dead-reckoned (extrapolated) from the fix below in between.
    float lat = 0, lon = 0;
    int altitudeFt = 0;

    // Position/altitude AT THE LAST REAL FETCH - the dead-reckoning base.
    // Kept separate from the live lat/lon/altitudeFt above so repeated
    // extrapolation calls never compound error against a moving target.
    float fixLat = 0, fixLon = 0;
    int fixAltitudeFt = 0;

    float distanceMi = 0;      // rounded to 1dp when set
    bool onGround = false;
    float speedKt = 0;         // rounded to 1dp when set, 0 if OpenSky gave none

    bool hasHeading = false;   // replaces Python's "heading or None"
    float heading = 0;         // degrees true track; valid only if hasHeading

    float vrateFpm = 0;        // rounded to whole number when set

    char airline[24] = "";
    char airlineCode[8] = "";
    char origin[8] = "";       // may carry ROUTE_FLAG '*' suffix
    char destination[8] = "";  // may carry ROUTE_FLAG '*' suffix

    // Render-time cache so render_values()-equivalent can redraw just the
    // value line without a full clear. -1 = "not currently drawn".
    int16_t valueY = -1;
    uint16_t valueColor = 0;

    bool isCommercial() const { return airline[0] != '\0'; }
};
