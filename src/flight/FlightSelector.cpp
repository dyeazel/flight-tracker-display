#include "FlightSelector.h"

#include <algorithm>
#include <string.h>

#include <Arduino.h>

#include "DestinationVerifier.h"
#include "FlightMath.h"
#include "config.h"
#include "network/AdsbdbClient.h"
#include "network/DepartureVerifier.h"
#include "network/RouteCache.h"
#include "time/TimeSync.h"

namespace FlightSelector {

namespace {

// Estimates depUtc/arrUtc from current position/speed and adsbdb's
// origin/destination coordinates when no real schedule data is
// available - ports nearby_flights.py's "calculated" fallback verbatim
// (same distance-already-covered/distance-remaining divided by current
// speed math): only meaningful airborne with a nonzero speed, since
// distance/speed is undefined at a dead stop. Leaves depUtc/arrUtc at 0
// (already the case on entry, since RouteCache's real-time path always
// `continue`s before reaching this) for whichever half has no known
// coordinate to measure from.
void estimateTimes(Flight &f, const AdsbdbClient::RouteInfo &info) {
    if (!TimeSync::isSynced() || f.onGround || f.speedKt <= 0) return;
    float speedMph = f.speedKt * 1.15078f;
    time_t now = TimeSync::nowUtc();

    if (info.haveOriginCoords) {
        float distFromOrigin =
            FlightMath::haversineMiles(info.originLat, info.originLon, f.lat, f.lon);
        float hoursSinceTakeoff = distFromOrigin / speedMph;
        f.depUtc = now - (time_t)(hoursSinceTakeoff * 3600.0f);
    }
    if (info.haveDestCoords) {
        float distRemaining = FlightMath::haversineMiles(f.lat, f.lon, info.destLat, info.destLon);
        float hoursRemaining = distRemaining / speedMph;
        f.arrUtc = now + (time_t)(hoursRemaining * 3600.0f);
    }
}

// Logs only when DestinationVerifier actually changed something (beforeCode
// differs from f.destination after the call) - most calls are no-ops
// (cruising, already matches), so this keeps [destination] lines rare and
// meaningful rather than printing every poll for every commercial flight.
void debugLogDestinationChange(const Flight &f, const char beforeCode[8]) {
    if (!DEBUG || strcmp(beforeCode, f.destination) == 0) return;
    Serial.printf("[destination] %s %s -> %s(%s)\n", f.callsign,
                  f.destinationGuessed ? "guessed" : "confirmed", f.destination, f.destinationName);
}

}  // namespace

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
            plausible = FlightMath::routeIsPlausible(f.lat, f.lon, f.hasHeading, f.heading,
                                                       f.speedKt, info.originLat, info.originLon,
                                                       info.destLat, info.destLon);
        }
        if (DEBUG) {
            float bearingToDest = info.haveDestCoords
                                        ? FlightMath::bearingDeg(f.lat, f.lon, info.destLat, info.destLon)
                                        : 0;
            Serial.printf(
                "[enrich] %s haveRoute=%d origin=%s(%.4f,%.4f) dest=%s(%.4f,%.4f) "
                "cur=(%.4f,%.4f) heading=%.0f bearingToDest=%.0f plausible=%d\n",
                f.callsign, haveRoute, info.originCode, info.originLat, info.originLon,
                info.destinationCode, info.destLat, info.destLon, f.lat, f.lon,
                f.hasHeading ? f.heading : -1, bearingToDest, plausible);
        }

        // adsbdb's route table is static and can be stale/wrong - if it
        // has nothing, or routeIsPlausible() flags what it has as
        // geometrically implausible, check the secondary provider cache
        // (AeroDataBox, via RouteCache/RouteProvider) before falling back
        // to a suspect/blank route.
        if (!haveRoute || !plausible) {
            RouteLeg leg;
            bool gotLeg = RouteCache::lookupActiveLeg(f.callsign, &leg);
            if (DEBUG) {
                Serial.printf("[enrich] %s RouteCache -> gotLeg=%d origin=%s dest=%s airline=%s "
                              "depUtc=%ld arrUtc=%ld\n",
                              f.callsign, gotLeg, leg.originCode, leg.destinationCode, leg.airline,
                              (long)leg.depUtc, (long)leg.arrUtc);
            }
            if (gotLeg) {
                // Trusted secondary source - not flagged.
                strncpy(f.origin, leg.originCode, sizeof(f.origin) - 1);
                f.origin[sizeof(f.origin) - 1] = '\0';
                strncpy(f.destination, leg.destinationCode, sizeof(f.destination) - 1);
                f.destination[sizeof(f.destination) - 1] = '\0';
                strncpy(f.originName, leg.originName, sizeof(f.originName) - 1);
                f.originName[sizeof(f.originName) - 1] = '\0';
                strncpy(f.destinationName, leg.destinationName, sizeof(f.destinationName) - 1);
                f.destinationName[sizeof(f.destinationName) - 1] = '\0';
                // If adsbdb had no route at all, this is also what makes
                // the flight render as "commercial" (Flight::isCommercial()
                // checks airline[0]) - only populated if AeroDataBox's
                // response included an airline for this leg.
                if (leg.airline[0]) {
                    strncpy(f.airline, leg.airline, sizeof(f.airline) - 1);
                    f.airline[sizeof(f.airline) - 1] = '\0';
                    strncpy(f.airlineCode, leg.airlineCode, sizeof(f.airlineCode) - 1);
                    f.airlineCode[sizeof(f.airlineCode) - 1] = '\0';
                }
                f.depUtc = leg.depUtc;
                f.arrUtc = leg.arrUtc;
                char beforeDest[8];
                strncpy(beforeDest, f.destination, sizeof(beforeDest));
                DestinationVerifier::verifyOrCorrectDestination(f);
                debugLogDestinationChange(f, beforeDest);
                continue;
            }
        }
        if (!haveRoute) continue;

        strncpy(f.airline, info.airline, sizeof(f.airline) - 1);
        strncpy(f.airlineCode, info.airlineCode, sizeof(f.airlineCode) - 1);
        strncpy(f.origin, info.originCode, sizeof(f.origin) - 1);
        strncpy(f.destination, info.destinationCode, sizeof(f.destination) - 1);
        strncpy(f.originName, info.originName, sizeof(f.originName) - 1);
        strncpy(f.destinationName, info.destinationName, sizeof(f.destinationName) - 1);
        estimateTimes(f, info);
        if (DEBUG) {
            Serial.printf("[enrich] %s calculated depUtc=%ld arrUtc=%ld onGround=%d speedKt=%.1f\n",
                          f.callsign, (long)f.depUtc, (long)f.arrUtc, f.onGround, f.speedKt);
        }

        if (!plausible) {
            anyFlagged = true;
            bool originVerified = DepartureVerifier::verifyOrCorrectOrigin(
                f, info.originLat, info.originLon, info.haveOriginCoords);
            if (DEBUG && originVerified) {
                Serial.printf("[departure] %s verified/corrected origin=%s(%s)\n", f.callsign,
                              f.origin, f.originName);
            }
            if (f.origin[0] && !originVerified) {
                strncat(f.origin, ROUTE_FLAG, sizeof(f.origin) - strlen(f.origin) - 1);
            }
            if (f.destination[0]) {
                strncat(f.destination, ROUTE_FLAG, sizeof(f.destination) - strlen(f.destination) - 1);
            }
        }
        char beforeDest[8];
        strncpy(beforeDest, f.destination, sizeof(beforeDest));
        DestinationVerifier::verifyOrCorrectDestination(f);
        debugLogDestinationChange(f, beforeDest);
    }
    return anyFlagged;
}

void sortCommercialFirst(Flight *flights, int count) {
    std::stable_sort(flights, flights + count,
                      [](const Flight &a, const Flight &b) { return a.isCommercial() && !b.isCommercial(); });
}

}  // namespace FlightSelector
