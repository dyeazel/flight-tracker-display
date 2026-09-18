#include "OpenSkyClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#include "config.h"
#include "flight/FlightMath.h"
#include "secrets.h"

namespace OpenSkyClient {

namespace {
// OpenSky's OAuth2 tokens run ~1500 chars in practice (confirmed on real
// hardware, Milestone 3) - undersizing this silently truncates the token,
// which the server then rejects with a clean 401 that looks like an auth
// bug rather than a buffer bug. Sized with real headroom.
constexpr size_t kTokenBufSize = 2048;
char cachedToken[kTokenBufSize] = "";
unsigned long cachedTokenTimeMs = 0;
bool haveToken = false;

int rateLimitRemaining = -1;  // -1 = unknown, matches Python's None
char lastError[48] = "";

// Static (not stack-local) scratch buffer for fetchNearby()'s token copy -
// see fetchOnce()'s comment on why large buffers here must not be
// stack-local: this call chain runs underneath the same deep mbedTLS
// handshake stack.
char tokenScratch[kTokenBufSize];

// Inserts f into the maxCount-sized array arr (sorted ascending by
// distanceMi), keeping only the closest maxCount candidates seen so far -
// avoids needing a separate large buffer for all candidates before
// sorting/truncating, unlike a naive "collect everything then sort" port.
void insertSorted(Flight *arr, int &count, int maxCount, const Flight &f) {
    if (count < maxCount) {
        int i = count;
        while (i > 0 && arr[i - 1].distanceMi > f.distanceMi) {
            arr[i] = arr[i - 1];
            i--;
        }
        arr[i] = f;
        count++;
    } else if (maxCount > 0 && f.distanceMi < arr[maxCount - 1].distanceMi) {
        int i = maxCount - 1;
        while (i > 0 && arr[i - 1].distanceMi > f.distanceMi) {
            arr[i] = arr[i - 1];
            i--;
        }
        arr[i] = f;
    }
}
}  // namespace

bool getToken(char *tokenOut, size_t tokenOutSize, bool force) {
    unsigned long now = millis();
    if (!force && haveToken && (now - cachedTokenTimeMs) < OPENSKY_TOKEN_TTL_MS) {
        strncpy(tokenOut, cachedToken, tokenOutSize - 1);
        tokenOut[tokenOutSize - 1] = '\0';
        return true;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);

    if (!http.begin(client, OPENSKY_TOKEN_URL)) {
        strncpy(lastError, "token: begin failed", sizeof(lastError) - 1);
        return false;
    }
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String body = String("grant_type=client_credentials&client_id=") + OPENSKY_CLIENT_ID +
                  "&client_secret=" + OPENSKY_CLIENT_SECRET;

    int code = http.POST(body);
    bool ok = false;
    if (code == 200) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (!err && doc["access_token"].is<const char *>()) {
            strncpy(cachedToken, doc["access_token"], kTokenBufSize - 1);
            cachedToken[kTokenBufSize - 1] = '\0';
            cachedTokenTimeMs = now;
            haveToken = true;
            strncpy(tokenOut, cachedToken, tokenOutSize - 1);
            tokenOut[tokenOutSize - 1] = '\0';
            ok = true;
        } else {
            strncpy(lastError, "token: bad JSON", sizeof(lastError) - 1);
        }
    } else {
        snprintf(lastError, sizeof(lastError), "token: HTTP %d", code);
    }
    http.end();
    return ok;
}

int currentRefreshInterval() {
    if (rateLimitRemaining >= 0 && rateLimitRemaining <= RATE_LIMIT_LOW_WATER) {
        return RATE_LIMIT_BACKOFF_SECONDS;
    }
    return REFRESH_SECONDS;
}

// One GET attempt against states/all with the given bearer token. Returns
// the HTTP status code (or a negative HTTPClient error code), and on 200
// parses the body into outFlights/outCount and updates rateLimitRemaining.
static int fetchOnce(const char *path, const char *token, float radiusMi, Flight *outFlights,
                      int maxOut, int *outCount) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20);
    HTTPClient http;
    http.setConnectTimeout(20000);
    http.setTimeout(20000);

    // url/authHeader are static (not stack-local): WiFiClientSecure's TLS
    // handshake (mbedtls_ctr_drbg_seed -> entropy gathering -> SHA) is
    // itself several KB deep on the stack by the time it's called from
    // here - adding a couple more KB of our own stack-local buffers on
    // top of that overflowed the default 8KB loopTask stack in testing
    // (crashed with "Stack canary watchpoint triggered" inside the
    // mbedtls call chain). Static storage avoids stacking our buffers on
    // top of that budget at all.
    static char url[256];
    snprintf(url, sizeof(url), "https://%s%s", OPENSKY_HOST, path);
    if (!http.begin(client, url)) {
        strncpy(lastError, "states: begin failed", sizeof(lastError) - 1);
        return -1;
    }

    static const char *kHeaders[] = {"X-Rate-Limit-Remaining"};
    http.collectHeaders(kHeaders, 1);

    static char authHeader[kTokenBufSize + 16];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", token);
    http.addHeader("Authorization", authHeader);

    int code = http.GET();
    if (code == 200) {
        if (http.hasHeader("X-Rate-Limit-Remaining")) {
            rateLimitRemaining = http.header("X-Rate-Limit-Remaining").toInt();
        }

        // Parsed from a fully-buffered String (http.getString()), not
        // http.getStream() directly - streaming straight into ArduinoJson
        // produced "InvalidInput" against this endpoint's chunked response
        // in testing, while the buffered String parsed cleanly. Costs one
        // extra full-body copy (the plan's original preference was to
        // stream directly to avoid that), but a confirmed-working path
        // beats a theoretically-more-efficient one that doesn't parse.
        String raw = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, raw);
        *outCount = 0;
        if (err) {
            Serial.printf("[opensky] JSON parse error: %s (body length=%d)\n", err.c_str(),
                          raw.length());
            snprintf(lastError, sizeof(lastError), "states: bad JSON (%s)", err.c_str());
        } else {
            JsonArray states = doc["states"].as<JsonArray>();
            for (JsonArray s : states) {
                if (s.size() < 12) continue;
                const char *icao24 = s[0].isNull() ? "" : s[0].as<const char *>();
                const char *callsign = s[1].isNull() ? "" : s[1].as<const char *>();
                if (s[5].isNull() || s[6].isNull()) continue;  // no lon/lat
                float lon = s[5].as<float>();
                float lat = s[6].as<float>();
                float baroAlt = s[7].isNull() ? 0 : s[7].as<float>();
                bool onGround = s[8].as<bool>();
                float velocity = s[9].isNull() ? 0 : s[9].as<float>();
                bool hasHeading = !s[10].isNull();
                float heading = hasHeading ? s[10].as<float>() : 0;
                float vrate = s[11].isNull() ? 0 : s[11].as<float>();

                float dist = FlightMath::haversineMiles(HOME_LAT, HOME_LON, lat, lon);
                if (dist > radiusMi) continue;

                Flight f;
                // Trim leading/trailing whitespace OpenSky pads callsigns with.
                char trimmed[16] = "";
                strncpy(trimmed, callsign, sizeof(trimmed) - 1);
                char *start = trimmed;
                while (*start == ' ') start++;
                char *end = start + strlen(start);
                while (end > start && *(end - 1) == ' ') *(--end) = '\0';
                strncpy(f.callsign, (*start ? start : "N/A"), sizeof(f.callsign) - 1);
                strncpy(f.icao24, icao24, sizeof(f.icao24) - 1);

                int altFt = baroAlt ? (int)(baroAlt * 3.28084f) : 0;
                f.lat = lat;
                f.lon = lon;
                f.fixLat = lat;
                f.fixLon = lon;
                f.fixAltitudeFt = altFt;
                f.altitudeFt = altFt;
                f.distanceMi = roundf(dist * 10) / 10;
                f.onGround = onGround;
                f.speedKt = velocity ? roundf(velocity * 1.94384f * 10) / 10 : 0;
                f.hasHeading = hasHeading;
                f.heading = heading;
                f.vrateFpm = vrate ? roundf(vrate * 196.85f) : 0;

                if (TARGET_CALLSIGNS_COUNT > 0) {
                    // Tracking specific callsigns: keep only matches, in
                    // arrival order (not distance-sorted - a tracked
                    // flight could be anywhere), up to maxOut.
                    bool isTarget = false;
                    for (int t = 0; t < TARGET_CALLSIGNS_COUNT; t++) {
                        if (strcmp(f.callsign, TARGET_CALLSIGNS[t]) == 0) {
                            isTarget = true;
                            break;
                        }
                    }
                    if (isTarget && *outCount < maxOut) {
                        outFlights[*outCount] = f;
                        (*outCount)++;
                    }
                } else {
                    insertSorted(outFlights, *outCount, maxOut, f);
                }
            }
        }
    } else {
        Serial.printf("[opensky] GET -> code=%d\n", code);
        snprintf(lastError, sizeof(lastError), "states: HTTP %d", code);
        if (code == 429) rateLimitRemaining = 0;
    }
    http.end();
    return code;
}

int fetchNearby(float radiusMi, Flight *outFlights, int maxOut) {
    float lamin, lamax, lomin, lomax;
    FlightMath::boundingBox(HOME_LAT, HOME_LON, radiusMi, &lamin, &lamax, &lomin, &lomax);
    char path[192];
    snprintf(path, sizeof(path), "%s?lamin=%f&lamax=%f&lomin=%f&lomax=%f", OPENSKY_PATH, lamin,
             lamax, lomin, lomax);

    if (!getToken(tokenScratch, kTokenBufSize)) return -1;

    int count = 0;
    int code = fetchOnce(path, tokenScratch, radiusMi, outFlights, maxOut, &count);
    if (code == 401) {
        if (!getToken(tokenScratch, kTokenBufSize, /*force=*/true)) return -1;
        code = fetchOnce(path, tokenScratch, radiusMi, outFlights, maxOut, &count);
    }
    if (code != 200) return -1;
    return count;
}

bool fetchLiveTrackFirstPoint(const char *icao24, float *outLat, float *outLon) {
    if (!icao24 || !icao24[0]) return false;

    static unsigned long lastTrackCallMs = 0;
    unsigned long nowMs = millis();
    if (lastTrackCallMs != 0 && nowMs - lastTrackCallMs < TRACK_PROVIDER_MIN_INTERVAL_MS) {
        return false;
    }
    lastTrackCallMs = nowMs;

    if (!getToken(tokenScratch, kTokenBufSize)) return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);

    // Static - see fetchOnce()'s comment on why buffers here must not be
    // stack-local (mbedTLS's own handshake call chain).
    static char url[160];
    snprintf(url, sizeof(url), "https://%s%s?icao24=%s&time=0", OPENSKY_HOST, OPENSKY_TRACKS_PATH,
              icao24);
    if (!http.begin(client, url)) return false;

    static char authHeader[kTokenBufSize + 16];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", tokenScratch);
    http.addHeader("Authorization", authHeader);

    int code = http.GET();
    bool ok = false;
    if (code == 200) {
        // Buffered String, not http.getStream() directly - same chunked-
        // response precaution as fetchOnce()/AeroDataBoxClient.
        String raw = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, raw);
        if (!err) {
            JsonArray path = doc["path"].as<JsonArray>();
            if (path.size() > 0) {
                JsonArray first = path[0].as<JsonArray>();
                // Index 1 = latitude, 2 = longitude - see OpenSky's /tracks
                // waypoint schema (time, lat, lon, baro_altitude,
                // true_track, on_ground).
                if (first.size() >= 3 && !first[1].isNull() && !first[2].isNull()) {
                    *outLat = first[1].as<float>();
                    *outLon = first[2].as<float>();
                    ok = true;
                }
            }
        }
    }
    http.end();
    return ok;
}

const char *lastErrorMessage() { return lastError; }

}  // namespace OpenSkyClient
