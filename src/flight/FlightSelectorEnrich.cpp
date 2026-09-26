#include "FlightSelector.h"

#include <string.h>

#include <Arduino.h>

#include "DestinationVerifier.h"
#include "FlightMath.h"
#include "config.h"
#include "config/ConfigStore.h"
#include "data/AirportTable.h"
#include "network/AdsbdbClient.h"
#include "network/DepartureVerifier.h"
#include "network/RouteCache.h"
#include "time/TimeSync.h"

// FlightSelector::enrich() and its network-dependent helpers - split out from FlightSelector.cpp (which
// keeps only the pure findMissing()/sortCommercialFirst()) so that file can be compiled into the native
// Unity test env. This one can't: it calls adsbdb/RouteCache/DepartureVerifier over the network and logs
// via Serial, so it's exercised only by live hardware tests (see test_watchdog.py's force-tests) and the
// pure decision functions it calls (FlightMath::routeIsPlausible, DestinationVerifier), which do have
// native coverage.
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

// Route pinning (Phase 8): ConfigStore::pinFor() lets the user assert a tracked flight's origin and/or
// destination, to work around a live provider's route being wrong, reused for a different day, or just
// stale - confirmed with the user that a pin should win outright, not merely act as a tiebreaker. Applied
// here, after the normal adsbdb/RouteCache resolution below, so it always has the last word; only touches
// the field(s) actually pinned, via AirportTable (the same on-device table DestinationVerifier already
// uses) so the airport *name* is correct immediately too, with no dependency on adsbdb ever succeeding.
// Airline name/code are unaffected - a pin only overrides route fields, never the airline lookup.
void applyPin(Flight &f, const char *pinOrigin, const char *pinDest) {
    if (pinOrigin[0]) {
        if (const AirportTable::AirportEntry *e = AirportTable::findByCode(pinOrigin)) {
            strncpy(f.origin, e->code, sizeof(f.origin) - 1);
            f.origin[sizeof(f.origin) - 1] = '\0';
            strncpy(f.originName, e->name, sizeof(f.originName) - 1);
            f.originName[sizeof(f.originName) - 1] = '\0';
        }
    }
    if (pinDest[0]) {
        if (const AirportTable::AirportEntry *e = AirportTable::findByCode(pinDest)) {
            strncpy(f.destination, e->code, sizeof(f.destination) - 1);
            f.destination[sizeof(f.destination) - 1] = '\0';
            strncpy(f.destinationName, e->name, sizeof(f.destinationName) - 1);
            f.destinationName[sizeof(f.destinationName) - 1] = '\0';
            f.destinationGuessed = false;  // an asserted pin is never a "guess"
        }
    }
}

}  // namespace

bool enrich(Flight *flights, int count) {
    if (!LOOKUP_ROUTES) return false;
    bool anyFlagged = false;

    for (int i = 0; i < count; i++) {
        Flight &f = flights[i];
        if (strcmp(f.callsign, "N/A") == 0 || !FlightMath::isScheduledCallsign(f.callsign)) {
            continue;
        }

        char pinOrigin[CallsignList::kCodeChars + 1] = "", pinDest[CallsignList::kCodeChars + 1] = "";
        ConfigStore::pinFor(f.callsign, pinOrigin, pinDest);
        // Nothing left for a route lookup to answer once both sides are pinned - skip the plausibility
        // check and the RouteCache fallback (a paid AeroDataBox call) entirely rather than spend budget on
        // a result that's about to be overridden anyway.
        const bool fullyPinned = pinOrigin[0] && pinDest[0];

        AdsbdbClient::RouteInfo info;
        bool haveRoute = AdsbdbClient::lookupRoute(f.callsign, &info);

        bool plausible = true;
        if (!fullyPinned && haveRoute && info.haveOriginCoords && info.haveDestCoords) {
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
                "cur=(%.4f,%.4f) heading=%.0f bearingToDest=%.0f plausible=%d pin=%s/%s\n",
                f.callsign, haveRoute, info.originCode, info.originLat, info.originLon,
                info.destinationCode, info.destLat, info.destLon, f.lat, f.lon,
                f.hasHeading ? f.heading : -1, bearingToDest, plausible, pinOrigin, pinDest);
        }

        // adsbdb's route table is static and can be stale/wrong - if it
        // has nothing, or routeIsPlausible() flags what it has as
        // geometrically implausible, check the secondary provider cache
        // (AeroDataBox, via RouteCache/RouteProvider) before falling back
        // to a suspect/blank route.
        if (!fullyPinned && (!haveRoute || !plausible)) {
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
                applyPin(f, pinOrigin, pinDest);
                if (!pinDest[0]) {
                    char beforeDest[8];
                    strncpy(beforeDest, f.destination, sizeof(beforeDest));
                    DestinationVerifier::verifyOrCorrectDestination(f);
                    debugLogDestinationChange(f, beforeDest);
                }
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
            bool originVerified = pinOrigin[0] || DepartureVerifier::verifyOrCorrectOrigin(
                f, info.originLat, info.originLon, info.haveOriginCoords);
            if (DEBUG && originVerified) {
                Serial.printf("[departure] %s verified/corrected origin=%s(%s)\n", f.callsign,
                              f.origin, f.originName);
            }
            if (f.origin[0] && !originVerified) {
                strncat(f.origin, ROUTE_FLAG, sizeof(f.origin) - strlen(f.origin) - 1);
            }
            if (f.destination[0] && !pinDest[0]) {
                strncat(f.destination, ROUTE_FLAG, sizeof(f.destination) - strlen(f.destination) - 1);
            }
        }
        applyPin(f, pinOrigin, pinDest);
        if (!pinDest[0]) {
            char beforeDest[8];
            strncpy(beforeDest, f.destination, sizeof(beforeDest));
            DestinationVerifier::verifyOrCorrectDestination(f);
            debugLogDestinationChange(f, beforeDest);
        }
    }
    return anyFlagged;
}

void classifyMissing(const char missing[][12], int missingCount, MissingStatus *outStatus, time_t *outWhenUtc) {
    const time_t now = TimeSync::isSynced() ? TimeSync::nowUtc() : 0;
    for (int i = 0; i < missingCount; i++) {
        RouteLeg leg;
        const bool gotLeg = now != 0 && RouteCache::lookupActiveLeg(missing[i], &leg);
        if (!gotLeg) {
            outStatus[i] = MissingStatus::Unknown;
            outWhenUtc[i] = 0;
            continue;
        }
        outStatus[i] = classifyMissingStatus(leg.depUtc, leg.arrUtc, now);
        outWhenUtc[i] = outStatus[i] == MissingStatus::NotYetDeparted   ? leg.depUtc
                        : outStatus[i] == MissingStatus::Landed         ? leg.arrUtc
                                                                         : 0;
    }
}

}  // namespace FlightSelector
