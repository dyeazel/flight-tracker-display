#pragma once

// adsbdb.com route lookup, ported from cyd_nearby_flights.py's
// lookup_route(). Looks up airline name + origin/destination airport
// (including coordinates, for route_is_plausible's sanity check) for a
// commercial callsign.
namespace AdsbdbClient {

struct RouteInfo {
    char airline[24] = "";
    char airlineCode[8] = "";
    char originCode[8] = "";
    char destinationCode[8] = "";
    char originName[40] = "";
    char destinationName[40] = "";
    bool haveOriginCoords = false;
    float originLat = 0, originLon = 0;
    bool haveDestCoords = false;
    float destLat = 0, destLon = 0;
};

// Returns true and fills info on success; false if there's no filed route
// (private/GA flights, unrecognized callsign, network failure, etc.) - the
// caller should treat false the same as Python's lookup_route() returning
// None (just skip enrichment for this flight, not a fatal error).
bool lookupRoute(const char *callsign, RouteInfo *info);

}  // namespace AdsbdbClient
