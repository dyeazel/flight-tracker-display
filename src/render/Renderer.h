#pragma once

#include "display/DisplayManager.h"
#include "flight/Flight.h"

// Screen rendering, ported from cyd_nearby_flights.py's
// render()/render_values()/draw_value_line()/draw_trend_icon()/draw_heartbeat().
namespace Renderer {

// Full redraw (clears the screen first): missing-targets line and/or
// "No aircraft in range", then each flight - commercial (has airline data)
// flights get airline name+flight number on their own line, then
// route+values sharing a second line; everything else gets a single line
// (callsign + values together). Stores each flight's value-line y/color
// (Flight::valueY/valueColor) so renderValues() can redraw just that line
// later without a full clear - see there for why that matters.
void render(DisplayManager &display, Flight *flights, int count, char missing[][12],
            int missingCount, bool anyFlagged);

// Partial redraw for dead-reckoning updates between real polls: overwrites
// just each flight's distance/altitude/bearing line, at fixed width, in
// place - no full clear, so the rest of the screen never flickers just
// because a number changed.
void renderValues(DisplayManager &display, Flight *flights, int count);

}  // namespace Renderer
