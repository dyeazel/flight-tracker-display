#pragma once

#include <time.h>

// Abstract interface for a secondary (paid or free) flight-route data
// source, consulted by FlightSelector::enrich() only when adsbdb has no
// route or FlightMath::routeIsPlausible() flags its route as suspect.
// Deliberately provider-agnostic: which concrete API to use is a decision
// made per .cpp file, not runtime polymorphism - see the README's
// "Secondary route provider" section. There is exactly one definition of
// fetchRouteLegs() linked into the build at a time - currently
// AeroDataBoxClient.cpp (via RapidAPI) - switching providers means
// swapping which .cpp file implements this function.

// One scheduled leg (city pair + times) for a callsign on a given day. A
// callsign can have more than one of these in a day - see RouteCache's
// handling of multi-leg/multi-city flight numbers.
struct RouteLeg {
    char originCode[8] = "";
    char destinationCode[8] = "";
    bool haveOriginCoords = false;
    float originLat = 0, originLon = 0;
    bool haveDestCoords = false;
    float destLat = 0, destLon = 0;
    time_t depUtc = 0;  // scheduled departure, UTC epoch, 0 if unknown
    time_t arrUtc = 0;  // scheduled arrival, UTC epoch, 0 if unknown
    // "" if this provider/leg doesn't supply one (e.g. adsbdb's fallback
    // path never did, until AeroDataBox - see FlightSelector::enrich()).
    char airline[24] = "";
    char airlineCode[8] = "";
};

// Fills outLegs (up to maxLegs) with every leg this callsign is scheduled
// to fly today, and sets *outLegCount. Returns false on any failure
// (network, auth, no data, budget exhausted, etc.) - callers must treat
// false exactly like AdsbdbClient::lookupRoute's false: skip enrichment
// for this flight, not a fatal error.
bool fetchRouteLegs(const char *callsign, RouteLeg *outLegs, int maxLegs, int *outLegCount);
