#pragma once

#include "Flight.h"
#include "config/CallsignList.h"

// Route enrichment, missing-target tracking, and sort order - ported from
// cyd_nearby_flights.py's select_flights()/enrich()/the commercial-first
// sort in main(). (The "keep only the closest MAX_ROWS, or only the
// tracked-callsign matches" half of select_flights() is instead folded
// into OpenSkyClient::fetchNearby's single-pass JSON scan, to avoid
// needing a large intermediate candidate buffer - see OpenSkyClient.cpp.)
namespace FlightSelector {

// Fills missingOut with any targetCallsigns entries not present in flights (up to maxMissing entries of
// up to 11 chars + nul), in targetCallsigns' own order. Returns the count written. Only meaningful when
// targetCallsignCount > 0. Takes the tracked list as a parameter (ConfigStore::callsigns()) rather than
// reading ConfigStore directly, so this stays a pure, native-testable function.
int findMissing(const Flight *flights, int count, char missingOut[][12], int maxMissing,
                 const CallsignList::Buf *targetCallsigns, int targetCallsignCount);

// Why a tracked-but-absent flight isn't showing (Phase 8) - lets the display say more than "not found"
// for a friend's itinerary leg that just hasn't happened yet, or already has.
enum class MissingStatus : uint8_t { Unknown, NotYetDeparted, InProgressNotVisible, Landed };

// Classifies against a known schedule window (0 = that bound is unknown) - pure, native-tested. Unknown
// only when NEITHER bound is known (classifyMissing(), below, is what actually looks the schedule up).
MissingStatus classifyMissingStatus(time_t depUtc, time_t arrUtc, time_t now);

// "unknown" / "not_yet_departed" / "in_progress" / "landed" - for the serial "diag" reply.
const char *missingStatusName(MissingStatus s);

// For each of missingCount callsigns in `missing` (as produced by findMissing()), looks up its schedule
// via RouteCache::lookupActiveLeg() and fills outStatus[i]/outWhenUtc[i] (the relevant time for that
// status - departure for NotYetDeparted, arrival for Landed, 0 otherwise). Network-touching (RouteCache),
// so - like enrich() - this lives in FlightSelectorEnrich.cpp, not compiled into the native test env.
void classifyMissing(const char missing[][12], int missingCount, MissingStatus *outStatus, time_t *outWhenUtc);

// Looks up route info (via AdsbdbClient) for scheduled-callsign flights,
// flags geometrically-implausible routes with ROUTE_FLAG, and fills each
// flight's airline/airlineCode/origin/destination. Returns true if any
// flight was flagged, so the caller can show the legend line - mirrors
// enrich()'s Python return value. No-op (returns false) if LOOKUP_ROUTES
// is false. A flight tracked with an origin/destination pin (ConfigStore::pinFor(), Phase 8) always shows
// the pinned airport(s) instead of whatever adsbdb/RouteCache returned, never flagged - see this file's
// FlightSelectorEnrich.cpp for the full rationale.
bool enrich(Flight *flights, int count);

// Stable sort: commercial (has airline data) flights first, preserving
// each group's existing relative order otherwise.
void sortCommercialFirst(Flight *flights, int count);

}  // namespace FlightSelector
