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

// AeroDataBox requests spent this local calendar month (see config.h's ROUTE_PROVIDER_MONTHLY_BUDGET)
// and which month that count is for (yyyymm local, 0 if never loaded - e.g. TimeSync not yet synced,
// so budgetMonthNumber() can't be compared against "the current month" meaningfully yet either). Loads
// the persisted NVS counter on first call if lookupActiveLeg() hasn't already. For the serial "diag"
// reply, so a long-running board can be checked for budget headroom without waiting for it to actually
// run out and start silently failing new lookups.
uint32_t budgetUsedThisMonth();
uint32_t budgetMonthNumber();

}  // namespace RouteCache
