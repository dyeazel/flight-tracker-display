#include "AeroDataBoxClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#include "config.h"
#include "secrets.h"
#include "time/Iso8601.h"

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
        const char *iata = airline["iata"] | "";
        const char *icao = airline["icao"] | "";
        strncpy(leg->airlineCode, (*iata ? iata : icao), sizeof(leg->airlineCode) - 1);
    }

    return leg->originCode[0] != '\0' || leg->destinationCode[0] != '\0';
}

// Returns the HTTP status code (or a negative HTTPClient transport
// error). On 200, parses the (array-or-single-object) body into
// outLegs/outLegCount.
int fetchOnce(const char *callsign, RouteLeg *outLegs, int maxLegs, int *outLegCount) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);

    // static, not stack-local - see OpenSkyClient.cpp's comment on why
    // buffers here must not stack on top of mbedTLS's own deep handshake
    // call chain.
    static char url[160];
    snprintf(url, sizeof(url), AERODATABOX_URL_FMT, callsign);
    *outLegCount = 0;
    if (!http.begin(client, url)) return -1;
    http.addHeader("X-RapidAPI-Key", AERODATABOX_API_KEY);
    http.addHeader("X-RapidAPI-Host", AERODATABOX_HOST);

    int code = http.GET();
    if (code == 200) {
        // Buffered String, not http.getStream() directly - streaming
        // produced "InvalidInput" against OpenSky's chunked response in
        // testing (see OpenSkyClient.cpp); applying the same precaution
        // here since it's the same HTTPClient/chunked-response combo.
        String raw = http.getString();
        if (DEBUG) Serial.println(raw);
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, raw);
        if (err) {
            Serial.printf("[aerodatabox] JSON parse error: %s\n", err.c_str());
        } else {
            JsonVariant root = doc.as<JsonVariant>();
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
        }
    }
    http.end();
    return code;
}

}  // namespace

bool fetchRouteLegs(const char *callsign, RouteLeg *outLegs, int maxLegs, int *outLegCount) {
    *outLegCount = 0;
    for (int attempt = 0; attempt < HTTPS_RETRIES; attempt++) {
        int code = fetchOnce(callsign, outLegs, maxLegs, outLegCount);
        if (code == 200) return *outLegCount > 0;
        if (attempt < HTTPS_RETRIES - 1) delay(HTTPS_RETRY_DELAY_MS);
    }
    return false;
}
