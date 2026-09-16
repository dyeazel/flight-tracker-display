#include "Renderer.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "flight/FlightMath.h"

namespace Renderer {

namespace {

bool heartbeatOn = false;

// Toggles a small square in the top-right corner every time the screen
// updates (real fetch or dead-reckoning redraw) - a way to confirm at a
// glance that it's still alive without having to read the numbers.
// IMPORTANT: must be called from BOTH render() and renderValues(), AND
// from the caller's error/exception path (see main.cpp) - the MicroPython
// version had a real bug this session where the error path forgot to call
// this, so it silently stopped blinking during runs of frequent failures.
void drawHeartbeat(DisplayManager &display) {
    heartbeatOn = !heartbeatOn;
    uint16_t color = heartbeatOn ? display.green : display.black;
    display.fillRect(display.width() - HEARTBEAT_SIZE - 2, 2, HEARTBEAT_SIZE, HEARTBEAT_SIZE, color);
}

// A small filled triangle pointing up (climbing), down (descending), or
// nothing (level - see ALT_TREND_THRESHOLD_FPM). Always clears its own
// footprint first, so a partial redraw correctly erases a stale icon when
// the trend flips to level, not just when it flips direction.
void drawTrendIcon(DisplayManager &display, int x, int y, float vrateFpm) {
    display.fillRect(x, y, TREND_ICON_SIZE, TREND_ICON_SIZE, display.black);
    int cx = x + TREND_ICON_SIZE / 2;
    int cy = y + TREND_ICON_SIZE / 2;
    if (vrateFpm > ALT_TREND_THRESHOLD_FPM) {
        display.fillTriangle(cx, cy, TREND_ICON_RADIUS, /*pointingUp=*/true, display.green);
    } else if (vrateFpm < -ALT_TREND_THRESHOLD_FPM) {
        display.fillTriangle(cx, cy, TREND_ICON_RADIUS, /*pointingUp=*/false, display.red);
    }
}

// Columns, left to right: distance, bearing (direction to look from home),
// altitude, its climb/descend icon, then ground speed. Each field is a
// fixed width (dist 9 chars, bearing 3 chars, altitude 8 chars, icon 8px,
// speed 6 chars) regardless of value, via printf field-width specifiers
// (%9s/%-9s - the C equivalent of the Python version's hand-rolled
// rjust/ljust, which MicroPython's str lacks), so a later partial redraw
// can overwrite old text/icon in place without a clear first. The speed
// column was added last and placed rightmost since that's the only
// unused space on the 320px-wide display (verified: ends at x=308).
void drawValueLine(DisplayManager &display, const Flight &f, int x, int y, uint16_t color) {
    char distRaw[16], distField[16];
    snprintf(distRaw, sizeof(distRaw), "%.1fmi", f.distanceMi);
    snprintf(distField, sizeof(distField), "%9s", distRaw);
    display.drawText8x8(x, y, distField, color, display.black);

    float brg = FlightMath::bearingDeg(HOME_LAT, HOME_LON, f.lat, f.lon);
    char brgField[8];
    snprintf(brgField, sizeof(brgField), "%3s", FlightMath::cardinalDirection(brg));
    int brgX = x + (int)strlen(distField) * 8 + TREND_ICON_GAP;
    display.drawText8x8(brgX, y, brgField, color, display.black);

    char altRaw[16], altField[16];
    snprintf(altRaw, sizeof(altRaw), "%dft", f.altitudeFt);
    snprintf(altField, sizeof(altField), "%8s", altRaw);
    int altX = brgX + (int)strlen(brgField) * 8 + TREND_ICON_GAP;
    display.drawText8x8(altX, y, altField, color, display.black);

    int iconX = altX + (int)strlen(altField) * 8 + TREND_ICON_GAP;
    drawTrendIcon(display, iconX, y, f.vrateFpm);

    char spdRaw[16], spdField[16];
    snprintf(spdRaw, sizeof(spdRaw), "%.0fkt", f.speedKt);
    snprintf(spdField, sizeof(spdField), "%6s", spdRaw);
    int spdX = iconX + TREND_ICON_SIZE + TREND_ICON_GAP;
    display.drawText8x8(spdX, y, spdField, color, display.black);
}

}  // namespace

void render(DisplayManager &display, Flight *flights, int count, char missing[][12],
            int missingCount, bool anyFlagged) {
    display.clear(display.black);
    drawHeartbeat(display);

    int y = 4;
    if (missingCount > 0) {
        char text[40] = "Not found: ";
        for (int i = 0; i < missingCount; i++) {
            if (i > 0) strncat(text, ", ", sizeof(text) - strlen(text) - 1);
            strncat(text, missing[i], sizeof(text) - strlen(text) - 1);
        }
        display.drawText8x8(4, y, text, display.red, display.black);
        y += 12;
    }

    if (count == 0) {
        display.drawText8x8(4, y, "No aircraft in range", display.gray, display.black);
        return;
    }

    for (int i = 0; i < count; i++) {
        Flight &f = flights[i];
        f.valueY = -1;
        uint16_t statusColor = f.onGround ? display.gray : display.green;
        bool isCommercial = f.isCommercial();

        if (isCommercial) {
            // is_scheduled_callsign() (checked upstream in enrich()) already
            // confirmed callsign[:3] is the airline prefix and callsign[3:]
            // is the flight number.
            char line1[40];
            snprintf(line1, sizeof(line1), "%s %s", f.airline, f.callsign + 3);
            display.drawText8x8(4, y, line1, display.white, display.black);
            y += 9;
            if (f.origin[0] || f.destination[0]) {
                char routeRaw[24], routeField[24];
                snprintf(routeRaw, sizeof(routeRaw), "%s->%s", f.origin[0] ? f.origin : "?",
                          f.destination[0] ? f.destination : "?");
                snprintf(routeField, sizeof(routeField), "%-*.*s", CALLSIGN_COL_CHARS,
                          CALLSIGN_COL_CHARS, routeRaw);
                bool flagged = strstr(f.origin, ROUTE_FLAG) || strstr(f.destination, ROUTE_FLAG);
                display.drawText8x8(4, y, routeField, flagged ? display.red : display.gray,
                                     display.black);
            }
        } else {
            char callsignField[16];
            snprintf(callsignField, sizeof(callsignField), "%-*.*s", CALLSIGN_COL_CHARS,
                      CALLSIGN_COL_CHARS, f.callsign);
            display.drawText8x8(4, y, callsignField, display.white, display.black);
        }

        f.valueY = y;
        f.valueColor = statusColor;
        drawValueLine(display, f, VALUE_X, y, statusColor);
        y += 12;
        if (y > display.height() - 24) break;
    }

    if (anyFlagged) {
        char legend[40];
        snprintf(legend, sizeof(legend), "%s = route may be stale", ROUTE_FLAG);
        display.drawText8x8(4, display.height() - 10, legend, display.red, display.black);
    }
}

void renderValues(DisplayManager &display, Flight *flights, int count) {
    for (int i = 0; i < count; i++) {
        if (flights[i].valueY >= 0) {
            drawValueLine(display, flights[i], VALUE_X, flights[i].valueY, flights[i].valueColor);
        }
    }
    drawHeartbeat(display);
}

}  // namespace Renderer
