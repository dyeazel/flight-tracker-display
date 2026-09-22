#include "AdsbdbClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "diag/Diag.h"
#include "util/Ascii.h"

namespace AdsbdbClient {

// Returns the HTTP status code (or a negative HTTPClient transport error),
// and fills *foundRoute to say whether a route was actually present in an
// otherwise-successful (200) response - a clean "no route for this
// callsign" is not a transport failure and must not be retried, unlike an
// actual connection/TLS error.
static int fetchOnce(const char *callsign, RouteInfo *info, bool *foundRoute) {
    *foundRoute = false;
    Diag::FetchTimer t(Diag::Source::Adsbdb, "api.adsbdb.com");
    if (!t.gate()) return -1;
    t.dns();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);

    static char url[128];  // static, not stack-local - see OpenSkyClient.cpp's comment
    snprintf(url, sizeof(url), ADSBDB_URL_FMT, callsign);
    if (!http.begin(client, url)) {
        t.finish(false, -1, "begin");
        return -1;
    }
    http.addHeader("User-Agent", USER_AGENT);

    int code = http.GET();
    t.gotResponse();
    bool parsedOk = true;
    if (code == 200) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        parsedOk = !err;
        if (!err) {
            JsonVariant route = doc["response"]["flightroute"];
            if (!route.isNull()) {
                JsonVariant airline = route["airline"];
                JsonVariant origin = route["origin"];
                JsonVariant dest = route["destination"];

                if (!airline.isNull()) {
                    strncpy(info->airline, airline["name"] | "", sizeof(info->airline) - 1);
                    Ascii::sanitize(info->airline);
                    const char *iata = airline["iata"] | "";
                    const char *icao = airline["icao"] | "";
                    strncpy(info->airlineCode, (*iata ? iata : icao), sizeof(info->airlineCode) - 1);
                }
                if (!origin.isNull()) {
                    const char *iata = origin["iata_code"] | "";
                    const char *icao = origin["icao_code"] | "";
                    strncpy(info->originCode, (*iata ? iata : icao), sizeof(info->originCode) - 1);
                    strncpy(info->originName, origin["name"] | "", sizeof(info->originName) - 1);
                    Ascii::sanitize(info->originName);
                    if (!origin["latitude"].isNull() && !origin["longitude"].isNull()) {
                        info->originLat = origin["latitude"].as<float>();
                        info->originLon = origin["longitude"].as<float>();
                        info->haveOriginCoords = true;
                    }
                }
                if (!dest.isNull()) {
                    const char *iata = dest["iata_code"] | "";
                    const char *icao = dest["icao_code"] | "";
                    strncpy(info->destinationCode, (*iata ? iata : icao),
                            sizeof(info->destinationCode) - 1);
                    strncpy(info->destinationName, dest["name"] | "", sizeof(info->destinationName) - 1);
                    Ascii::sanitize(info->destinationName);
                    if (!dest["latitude"].isNull() && !dest["longitude"].isNull()) {
                        info->destLat = dest["latitude"].as<float>();
                        info->destLon = dest["longitude"].as<float>();
                        info->haveDestCoords = true;
                    }
                }
                *foundRoute = true;
            }
        }
    }
    http.end();
    // 404 {"response":"unknown callsign"} is adsbdb's definitive "no route on
    // file" answer: a valid response, not a network failure.
    t.finish((code == 200 && parsedOk) || code == 404, code,
             code == 404 ? "no-route" : code == 200 ? "parse" : "http");
    return code;
}

bool lookupRoute(const char *callsign, RouteInfo *info) {
    *info = RouteInfo();
    bool foundRoute = false;
    for (int attempt = 0; attempt < HTTPS_RETRIES; attempt++) {
        int code = fetchOnce(callsign, info, &foundRoute);
        // A clean answer either way - don't retry. 404 ("unknown callsign") is
        // definitive; retrying it cost 4 TLS handshakes (~10 s of blocking, measured)
        // per unknown callsign on every poll.
        if (code == 200 || code == 404) return foundRoute;
        if (attempt < HTTPS_RETRIES - 1) delay(HTTPS_RETRY_DELAY_MS);
    }
    return false;
}

}  // namespace AdsbdbClient
