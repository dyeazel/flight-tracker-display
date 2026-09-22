#include "AeroDataBoxClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#include "AeroDataBoxParse.h"
#include "config.h"
#include "diag/Diag.h"
#include "secrets.h"

namespace {

// Returns the HTTP status code (or a negative HTTPClient transport
// error). On 200, parses the (array-or-single-object) body into
// outLegs/outLegCount.
int fetchOnce(const char *callsign, RouteLeg *outLegs, int maxLegs, int *outLegCount) {
    *outLegCount = 0;
    Diag::FetchTimer t(Diag::Source::AeroDataBox, AERODATABOX_HOST);
    if (!t.gate()) return -1;
    t.dns();

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
    if (!http.begin(client, url)) {
        t.finish(false, -1, "begin");
        return -1;
    }
    http.addHeader("X-RapidAPI-Key", AERODATABOX_API_KEY);
    http.addHeader("X-RapidAPI-Host", AERODATABOX_HOST);

    int code = http.GET();
    t.gotResponse();
    bool parsedOk = true;
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
            parsedOk = false;
            Serial.printf("[aerodatabox] JSON parse error: %s\n", err.c_str());
        } else {
            AeroDataBoxParse::parseLegs(doc.as<JsonVariant>(), outLegs, maxLegs, outLegCount);
        }
    }
    http.end();
    t.finish(code == 200 && parsedOk, code, code == 200 ? "parse" : "http");
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
