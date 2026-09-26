#pragma once

#include "display/DisplayManager.h"
#include "flight/Flight.h"
#include "flight/FlightSelector.h"

// Screen rendering, ported from cyd_nearby_flights.py's
// render()/render_values()/draw_value_line()/draw_trend_icon()/draw_heartbeat().
namespace Renderer {

// Full redraw (clears the screen first): missing-targets line(s) and/or
// "No aircraft in range", then each flight - commercial (has airline data)
// flights get airline name+flight number on their own line, then
// route+values sharing a second line, then a third line with the full
// departure/destination airport names (omitted when the provider supplied
// no names at all; if only one is missing, that side shows its code);
// everything else gets a single line (callsign + values together). Stores
// each flight's value-line y/color
// (Flight::valueY/valueColor) so renderValues() can redraw just that line
// later without a full clear - see there for why that matters.
// missingStatus/missingWhenUtc (Phase 8, parallel to missing[]/missingCount) say WHY a tracked flight
// isn't showing - e.g. "departs 3:45" instead of a bare "not found" - see FlightSelector::classifyMissing().
void render(DisplayManager &display, Flight *flights, int count, char missing[][12],
            const FlightSelector::MissingStatus *missingStatus, const time_t *missingWhenUtc, int missingCount,
            bool anyFlagged);

// Partial redraw for dead-reckoning updates between real polls: overwrites
// just each flight's distance/altitude/bearing line, at fixed width, in
// place - no full clear, so the rest of the screen never flickers just
// because a number changed.
void renderValues(DisplayManager &display, Flight *flights, int count);

// Marks the heartbeat square yellow to signal an in-progress real poll -
// call once, right before OpenSkyClient::fetchNearby(). Both fetchNearby()
// and FlightSelector::enrich() (adsbdb/AeroDataBox lookups) are blocking
// network calls that can take anywhere from under a second to tens of
// seconds (retries, a slow lookup), during which neither render() nor
// renderValues() runs to update the heartbeat - without this, the square
// just sits frozen at whatever it was for that whole stretch, which reads
// as "stuck," not "busy." The next render()/renderValues() call overwrites
// this with the normal green/black toggle once the poll actually
// completes.
void drawRefreshing(DisplayManager &display);

// Marks the heartbeat square steady red - the on-screen flights are real but stale (no successful fetch
// in a while). See main.cpp's staleness check and NetTask.h.
void drawStale(DisplayManager &display);

}  // namespace Renderer
