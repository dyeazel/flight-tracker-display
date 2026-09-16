#pragma once

#include "RouteProvider.h"

// Per-callsign cache in front of RouteProvider::fetchRouteLegs() - the
// generic caching/budget/rate-limit layer, independent of which concrete
// provider is wired in (currently AeroDataBoxClient). Consulted by
// FlightSelector::enrich() only as a fallback when adsbdb has no route
// or flags one as implausible - see the README's "Secondary route
// provider" section for the full design rationale (why callsign not tail
// number, why local-day not UTC-day, how multi-leg/multi-city callsigns
// are resolved).
namespace RouteCache {

// Returns (in *out) the leg of `callsign`'s cached schedule whose
// [depUtc, arrUtc] window contains the current time - or, if none
// matches exactly, the leg closest in time - fetching fresh data via
// RouteProvider::fetchRouteLegs() first if nothing usable is cached yet
// for today. Returns false if TimeSync isn't synced yet, the provider has
// nothing for this callsign, or the request budget/rate limit blocks a
// fresh fetch and nothing is cached yet either.
bool lookupActiveLeg(const char *callsign, RouteLeg *out);

}  // namespace RouteCache
