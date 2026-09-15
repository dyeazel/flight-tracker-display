#pragma once

#include "Flight.h"

// Route enrichment, missing-target tracking, and sort order - ported from
// cyd_nearby_flights.py's select_flights()/enrich()/the commercial-first
// sort in main(). (The "keep only the closest MAX_ROWS, or only the
// TARGET_CALLSIGNS matches" half of select_flights() is instead folded
// into OpenSkyClient::fetchNearby's single-pass JSON scan, to avoid
// needing a large intermediate candidate buffer - see OpenSkyClient.cpp.)
namespace FlightSelector {

// Fills missingOut with any TARGET_CALLSIGNS entries not present in
// flights (up to maxMissing entries of up to 11 chars + nul). Returns the
// count written. Only meaningful when TARGET_CALLSIGNS_COUNT > 0.
int findMissing(const Flight *flights, int count, char missingOut[][12], int maxMissing);

// Looks up route info (via AdsbdbClient) for scheduled-callsign flights,
// flags geometrically-implausible routes with ROUTE_FLAG, and fills each
// flight's airline/airlineCode/origin/destination. Returns true if any
// flight was flagged, so the caller can show the legend line - mirrors
// enrich()'s Python return value. No-op (returns false) if LOOKUP_ROUTES
// is false.
bool enrich(Flight *flights, int count);

// Stable sort: commercial (has airline data) flights first, preserving
// each group's existing relative order otherwise.
void sortCommercialFirst(Flight *flights, int count);

}  // namespace FlightSelector
