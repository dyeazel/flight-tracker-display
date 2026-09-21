#pragma once

#include "Flight.h"

// Pure math helpers - direct ports of the corresponding functions in
// cyd_nearby_flights.py, same formulas, same constants.
namespace FlightMath {

float haversineMiles(float lat1, float lon1, float lat2, float lon2);

// Great-circle destination-point projection: given a last known fix,
// heading, and speed, where would the aircraft be after elapsedS seconds
// of straight-line travel? Only valid for the few seconds/tens of seconds
// between real OpenSky polls - not accurate for long extrapolation.
void deadReckon(float lat, float lon, float headingDeg, float speedKt,
                 float elapsedS, float *outLat, float *outLon);

// Initial compass bearing from (lat1,lon1) to (lat2,lon2) - i.e. which way
// to look from home to spot the aircraft, not the aircraft's own heading.
float bearingDeg(float lat1, float lon1, float lat2, float lon2);

// Approximate lat/lon bounding box for a given radius around (lat, lon).
void boundingBox(float lat, float lon, float radiusMi, float *outLamin,
                  float *outLamax, float *outLomin, float *outLomax);

// RADIUS_MI override, else CALLSIGN_SEARCH_RADIUS_MI if TARGET_CALLSIGNS is
// non-empty, else DEFAULT_RADIUS_MI.
float effectiveRadius();

// True for callsigns that look like a scheduled flight number (3-letter
// code + digit) AND aren't a known charter/fractional-ownership prefix.
bool isScheduledCallsign(const char *callsign);

// Sanity-check a resolved route (from adsbdb) against the aircraft's live
// position - see cyd_nearby_flights.py's route_is_plausible for the
// rationale (adsbdb's table is static and can be stale/wrong). Two
// independent checks, either of which can flag the route:
//  - distance: does going origin->current->destination cost much more
//    total distance than a direct flight (the original, ported check).
//  - heading (added after a live case - UA1217 claiming IAH->BOS while
//    actually cruising WNW over Wisconsin, heading almost exactly AWAY
//    from Boston - passed the distance check alone on slack, since IAH-BOS
//    is a long enough trip that a Wisconsin detour didn't blow the
//    threshold): does the aircraft's actual track point roughly toward the
//    claimed destination, not away from it. Only checked when hasHeading
//    and moving fast enough for heading to mean anything (same
//    MIN_DEAD_RECKON_KT floor dead-reckoning already uses) - skipped
//    entirely otherwise, same as the distance check already skips a
//    zero-distance route.
bool routeIsPlausible(float curLat, float curLon, bool hasHeading, float heading, float speedKt,
                       float originLat, float originLon, float destLat, float destLon);

// 16-point compass abbreviation (e.g. "NNE") for a bearing in degrees.
const char *cardinalDirection(float brg);

// Extrapolate each flight's live lat/lon/altitude from its fix (captured
// at the last real OpenSky poll) forward by elapsedS seconds, using its
// heading/speed/vertical-rate at that fix. Skips aircraft on the ground,
// too slow for a heading to be meaningful (MIN_DEAD_RECKON_KT), or with
// no heading. Mutates flights in place.
void applyDeadReckoning(Flight *flights, int count, float elapsedS);

}  // namespace FlightMath
