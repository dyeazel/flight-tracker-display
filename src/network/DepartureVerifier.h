#pragma once

#include "flight/Flight.h"

// Last-resort check for a flight whose route already looks geometrically
// suspect (FlightMath::routeIsPlausible() said no) and that RouteCache/
// AeroDataBox couldn't resolve to a trusted replacement: ask OpenSky where
// this aircraft's live track actually started, and compare that against
// the claimed origin. See config.h's "Departure verification" section for
// the full rationale and tuning constants.
namespace DepartureVerifier {

// Compares f's claimed origin (via claimedOriginLat/claimedOriginLon, only
// meaningful when haveClaimedOriginCoords) against OpenSky's live track
// for f.icao24. If they disagree, corrects f.origin/f.originName in place
// to the nearest real airport (AirportTable::findNearest()) found near the
// track's actual first waypoint.
//
// Returns true if f.origin should now be considered verified - either
// confirmed as-is, or corrected - so the caller should NOT flag it as
// suspect. Returns false if nothing could be determined this call (no
// track data yet, no coordinates to compare, feature disabled via
// VERIFY_DEPARTURE_VIA_TRACK) - the caller's existing flagging behavior
// should apply as if this check never happened.
//
// Caches its outcome per callsign per local day (see
// DEPARTURE_VERIFY_CACHE_SIZE) so a still-flagged flight isn't re-checked
// against OpenSky on every ~30s poll for the rest of its flight - a
// "couldn't determine" outcome is deliberately NOT cached, so a later poll
// can retry once track data may exist.
bool verifyOrCorrectOrigin(Flight &f, float claimedOriginLat, float claimedOriginLon,
                            bool haveClaimedOriginCoords);

}  // namespace DepartureVerifier
