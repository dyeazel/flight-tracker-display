#include "FlightSelector.h"

#include <algorithm>
#include <string.h>

#include "FlightMath.h"
#include "config.h"
#include "network/AdsbdbClient.h"

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
        if (!AdsbdbClient::lookupRoute(f.callsign, &info)) continue;

        bool plausible = true;
        if (info.haveOriginCoords && info.haveDestCoords) {
            plausible = FlightMath::routeIsPlausible(f.lat, f.lon, info.originLat, info.originLon,
                                                       info.destLat, info.destLon);
        }

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
