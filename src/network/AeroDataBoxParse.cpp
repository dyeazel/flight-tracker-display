#include "AeroDataBoxParse.h"

#include <string.h>

#include "time/Iso8601.h"
#include "util/Ascii.h"

namespace AeroDataBoxParse {

namespace {

// Confirmed against a real live response (2026-09-16): the airport object
// does carry a "location":{"lat":...,"lon":...} sub-object, contrary to
// the pre-hardware assumption in AeroDataBoxClient.h - populate it.
//
// nameDst/nameSize: the airport's short human name, if present - "name"
// confirmed against a live payload 2026-09-17 (see AeroDataBoxClient.h).
void copyAirport(char *codeDst, size_t codeSize, char *nameDst, size_t nameSize, bool *haveCoords,
                  float *lat, float *lon, JsonVariant airport) {
    if (airport.isNull()) return;
    const char *iata = airport["iata"] | "";
    const char *icao = airport["icao"] | "";
    strncpy(codeDst, (*iata ? iata : icao), codeSize - 1);
    codeDst[codeSize - 1] = '\0';
    strncpy(nameDst, airport["name"] | "", nameSize - 1);
    nameDst[nameSize - 1] = '\0';
    Ascii::sanitize(nameDst);

    JsonVariant loc = airport["location"];
    if (!loc["lat"].isNull() && !loc["lon"].isNull()) {
        *lat = loc["lat"].as<float>();
        *lon = loc["lon"].as<float>();
        *haveCoords = true;
    }
}

// Tries runwayTime (actual) -> revisedTime (airline-revised estimate) ->
// predictedTime (AeroDataBox's own live prediction) -> scheduledTime
// (original filed schedule, least fresh), parsing whichever's ".utc"
// string is present first via Iso8601::parseUtc(). Confirmed against a
// real live response (2026-09-15) that predictedTime is a real, distinct
// field some movements carry with NONE of the other three present (a
// smaller/less-tracked operator's arrival had only predictedTime) - an
// earlier version of this list omitted it, based on other live examples
// that happened to also have revisedTime, and silently left arrUtc at 0
// for this callsign until caught here.
time_t pickTime(JsonVariant movement) {
    if (movement.isNull()) return 0;
    static const char *kKeys[] = {"runwayTime", "revisedTime", "predictedTime", "scheduledTime"};
    for (const char *key : kKeys) {
        JsonVariant block = movement[key];
        if (block.isNull()) continue;
        const char *utc = block["utc"] | "";
        time_t t;
        if (*utc && Iso8601::parseUtc(utc, &t)) return t;
    }
    return 0;
}

// Fills leg from one AeroDataBox flight-record JSON element. Returns false
// only if neither origin nor destination resolved to anything usable - a
// leg missing just the airline (or times) is still counted, matching
// AdsbdbClient's "partial data is still data" handling.
bool parseElement(JsonVariant el, RouteLeg *leg) {
    *leg = RouteLeg();
    JsonVariant departure = el["departure"];
    JsonVariant arrival = el["arrival"];
    copyAirport(leg->originCode, sizeof(leg->originCode), leg->originName, sizeof(leg->originName),
                &leg->haveOriginCoords, &leg->originLat, &leg->originLon, departure["airport"]);
    copyAirport(leg->destinationCode, sizeof(leg->destinationCode), leg->destinationName,
                sizeof(leg->destinationName), &leg->haveDestCoords, &leg->destLat, &leg->destLon,
                arrival["airport"]);
    leg->depUtc = pickTime(departure);
    leg->arrUtc = pickTime(arrival);

    JsonVariant airline = el["airline"];
    if (!airline.isNull()) {
        strncpy(leg->airline, airline["name"] | "", sizeof(leg->airline) - 1);
        Ascii::sanitize(leg->airline);
        const char *iata = airline["iata"] | "";
        const char *icao = airline["icao"] | "";
        strncpy(leg->airlineCode, (*iata ? iata : icao), sizeof(leg->airlineCode) - 1);
    }

    return leg->originCode[0] != '\0' || leg->destinationCode[0] != '\0';
}

}  // namespace

bool parseLegs(JsonVariant root, RouteLeg *outLegs, int maxLegs, int *outLegCount) {
    *outLegCount = 0;
    if (root.is<JsonArray>()) {
        for (JsonVariant el : root.as<JsonArray>()) {
            if (*outLegCount >= maxLegs) break;
            RouteLeg leg;
            if (parseElement(el, &leg)) outLegs[(*outLegCount)++] = leg;
        }
    } else if (root.is<JsonObject>() && maxLegs > 0) {
        RouteLeg leg;
        if (parseElement(root, &leg)) outLegs[(*outLegCount)++] = leg;
    }
    return *outLegCount > 0;
}

}  // namespace AeroDataBoxParse
