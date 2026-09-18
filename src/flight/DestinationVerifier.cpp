#include "DestinationVerifier.h"

#include <string.h>

#include "FlightMath.h"
#include "config.h"
#include "data/AirportTable.h"

namespace DestinationVerifier {

namespace {

void applyCorrection(Flight &f, const AirportTable::AirportEntry &match, bool guessed) {
    const char *code = match.code;
    strncpy(f.destination, code, sizeof(f.destination) - 1);
    f.destination[sizeof(f.destination) - 1] = '\0';
    strncpy(f.destinationName, match.name, sizeof(f.destinationName) - 1);
    f.destinationName[sizeof(f.destinationName) - 1] = '\0';
    f.destinationGuessed = guessed;
}

// f.destination may carry a trailing ROUTE_FLAG ('*') suffix from
// FlightSelector's plausibility flagging - strip it before a code lookup.
void strippedCode(const Flight &f, char *out, size_t outSize) {
    strncpy(out, f.destination, outSize - 1);
    out[outSize - 1] = '\0';
    char *flag = strstr(out, ROUTE_FLAG);
    if (flag != nullptr) *flag = '\0';
}

}  // namespace

void verifyOrCorrectDestination(Flight &f) {
    f.destinationGuessed = false;
    if (!f.isCommercial() || f.destination[0] == '\0') return;

    char code[8];
    strippedCode(f, code, sizeof(code));

    if (f.onGround) {
        const AirportTable::AirportEntry *claimed = AirportTable::findByCode(code);
        if (claimed != nullptr) {
            float distMi = FlightMath::haversineMiles(f.lat, f.lon, claimed->lat, claimed->lon);
            if (distMi <= DESTINATION_MATCH_RADIUS_MI) return;  // matches - confirmed, no change
        }
        const AirportTable::AirportEntry *actual =
            AirportTable::findNearest(f.lat, f.lon, DESTINATION_MATCH_RADIUS_MI);
        if (actual != nullptr) applyCorrection(f, *actual, /*guessed=*/false);
        return;
    }

    if (f.vrateFpm >= -ALT_TREND_THRESHOLD_FPM) return;  // not descending

    const AirportTable::AirportEntry *claimed = AirportTable::findByCode(code);
    if (claimed != nullptr) {
        float aglFt = f.altitudeFt - claimed->elevationFt;
        float distMi = FlightMath::haversineMiles(f.lat, f.lon, claimed->lat, claimed->lon);
        if (aglFt <= DESCENT_GUESS_MAX_AGL_FT && distMi <= DESCENT_GUESS_MAX_RADIUS_MI) {
            return;  // already looks like final approach into the claimed destination
        }
    }

    const AirportTable::AirportEntry *nearest =
        AirportTable::findNearest(f.lat, f.lon, DESCENT_GUESS_MAX_RADIUS_MI);
    if (nearest == nullptr) return;
    float aglFt = f.altitudeFt - nearest->elevationFt;
    if (aglFt > DESCENT_GUESS_MAX_AGL_FT) return;
    applyCorrection(f, *nearest, /*guessed=*/true);
}

}  // namespace DestinationVerifier
