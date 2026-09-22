#include "AdsbdbClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "AdsbdbParse.h"
#include "config.h"
#include "diag/Diag.h"

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
        if (!err) *foundRoute = AdsbdbParse::parseRoute(doc.as<JsonVariant>(), info);
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
