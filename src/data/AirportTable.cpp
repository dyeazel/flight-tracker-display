#include "AirportTable.h"

#include <string.h>

#include "flight/FlightMath.h"

namespace AirportTable {

const AirportEntry *findNearest(float lat, float lon, float maxDistanceMi) {
    const AirportEntry *best = nullptr;
    float bestDist = 0;

    for (int i = 0; i < kAirportCount; i++) {
        float dist = FlightMath::haversineMiles(lat, lon, kAirportTable[i].lat, kAirportTable[i].lon);
        if (best == nullptr || dist < bestDist) {
            best = &kAirportTable[i];
            bestDist = dist;
        }
    }

    if (best == nullptr || bestDist > maxDistanceMi) return nullptr;
    return best;
}

const AirportEntry *findByCode(const char *code) {
    if (code == nullptr || code[0] == '\0') return nullptr;
    for (int i = 0; i < kAirportCount; i++) {
        if (strcmp(kAirportTable[i].code, code) == 0) return &kAirportTable[i];
    }
    return nullptr;
}

}  // namespace AirportTable
