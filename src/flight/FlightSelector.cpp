#include "FlightSelector.h"

#include <algorithm>
#include <string.h>

// enrich() (the network-heavy half: adsbdb/RouteCache/DepartureVerifier lookups) lives in
// FlightSelectorEnrich.cpp instead of here, so this file has no Arduino/network dependency and can be
// compiled into the native Unity test env (see [env:native] in platformio.ini) - findMissing() and
// sortCommercialFirst() are pure functions over an already-fetched Flight array.
namespace FlightSelector {

int findMissing(const Flight *flights, int count, char missingOut[][12], int maxMissing,
                 const CallsignList::Buf *targetCallsigns, int targetCallsignCount) {
    int found = 0;
    for (int i = 0; i < targetCallsignCount && found < maxMissing; i++) {
        bool matched = false;
        for (int j = 0; j < count; j++) {
            if (strcmp(flights[j].callsign, targetCallsigns[i]) == 0) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            strncpy(missingOut[found], targetCallsigns[i], 11);
            missingOut[found][11] = '\0';
            found++;
        }
    }
    return found;
}

void sortCommercialFirst(Flight *flights, int count) {
    std::stable_sort(flights, flights + count,
                      [](const Flight &a, const Flight &b) { return a.isCommercial() && !b.isCommercial(); });
}

}  // namespace FlightSelector
