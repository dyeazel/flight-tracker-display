#include "FlightSelector.h"

#include <algorithm>
#include <string.h>

#include "FlightMath.h"
#include "config.h"
#include "network/AdsbdbClient.h"
#include "network/RouteCache.h"

namespace FlightSelector {

int findMissing(const Flight *flights, int count, char missingOut[][12], int maxMissing) {
    int found = 0;
    for (int i = 0; i < TARGET_CALLSIGNS_COUNT && found < maxMissing; i++) {
        bool matched = false;
        for (int j = 0; j < count; j++) {
            if (strcmp(flights[j].callsign, TARGET_CALLSIGNS[i]) == 0) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            strncpy(missingOut[found], TARGET_CALLSIGNS[i], 11);
            missingOut[found][11] = '\0';
            found++;
        }
    }
    return found;
}

bool enrich(Flight *flights, int count) {
    if (!LOOKUP_ROUTES) return false;
    bool anyFlagged = false;

    for (int i = 0; i < count; i++) {
        Flight &f = flights[i];
        if (strcmp(f.callsign, "N/A") == 0 || !FlightMath::isScheduledCallsign(f.callsign)) {
            continue;
        }

        AdsbdbClient::RouteInfo info;
        bool haveRoute = AdsbdbClient::lookupRoute(f.callsign, &info);

        bool plausible = true;
        if (haveRoute && info.haveOriginCoords && info.haveDestCoords) {
            plausible = FlightMath::routeIsPlausible(f.lat, f.lon, info.originLat, info.originLon,
                                                       info.destLat, info.destLon);
        }

        // adsbdb's route table is static and can be stale/wrong - if it
        // has nothing, or routeIsPlausible() flags what it has as
        // geometrically implausible, check the secondary provider cache
        // before falling back to a suspect/blank route. See
        // RouteCache/RouteProvider (currently a no-op stub - no concrete
        // provider is wired in yet, see README).
        if (!haveRoute || !plausible) {
            RouteLeg leg;
            if (RouteCache::lookupActiveLeg(f.callsign, &leg)) {
                // Trusted secondary source - not flagged. Note: RouteLeg
                // carries no airline name, so if adsbdb had no route at
                // all (haveRoute false), this flight still won't render
                // as "commercial" (see Flight::isCommercial()) until a
                // real provider that also supplies an airline name is
                // wired in.
                strncpy(f.origin, leg.originCode, sizeof(f.origin) - 1);
                f.origin[sizeof(f.origin) - 1] = '\0';
                strncpy(f.destination, leg.destinationCode, sizeof(f.destination) - 1);
                f.destination[sizeof(f.destination) - 1] = '\0';
                continue;
            }
        }
        if (!haveRoute) continue;

        strncpy(f.airline, info.airline, sizeof(f.airline) - 1);
        strncpy(f.airlineCode, info.airlineCode, sizeof(f.airlineCode) - 1);
        strncpy(f.origin, info.originCode, sizeof(f.origin) - 1);
        strncpy(f.destination, info.destinationCode, sizeof(f.destination) - 1);

        if (!plausible) {
            anyFlagged = true;
            if (f.origin[0]) strncat(f.origin, ROUTE_FLAG, sizeof(f.origin) - strlen(f.origin) - 1);
            if (f.destination[0]) {
                strncat(f.destination, ROUTE_FLAG, sizeof(f.destination) - strlen(f.destination) - 1);
            }
        }
    }
    return anyFlagged;
}

void sortCommercialFirst(Flight *flights, int count) {
    std::stable_sort(flights, flights + count,
                      [](const Flight &a, const Flight &b) { return a.isCommercial() && !b.isCommercial(); });
}

}  // namespace FlightSelector
