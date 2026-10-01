#include "ConfigStore.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "data/AirportTable.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define HAVE_DEV_SECRETS 1
#endif

namespace ConfigStore {

namespace {
char gSsid[kSsidMax + 1] = "";
char gPass[kPassMax + 1] = "";
float gLat = HOME_LAT;
float gLon = HOME_LON;
float gRadiusMi = RADIUS_MI_AUTO;
CallsignList::Entry gCallsigns[CallsignList::kMax];
int gCallsignCount = 0;
DisplayMode gDisplayMode = DisplayMode::MaxDensity;
float gRadarRangeMi = DEFAULT_RADAR_RANGE_MI;
RadarAirportList::Code gRadarAirports[RadarAirportList::kMax];
int gRadarAirportCount = 0;

void logConfig() {
    char enc[CallsignList::kEncodedMax];
    CallsignList::encode(gCallsigns, gCallsignCount, enc, sizeof(enc));
    char radarEnc[RadarAirportList::kEncodedMax];
    RadarAirportList::encode(gRadarAirports, gRadarAirportCount, radarEnc, sizeof(radarEnc));
    Serial.printf("[cfg] ssid='%s' pass=%s lat=%.6f lon=%.6f radius=%.1f callsigns=%s radarRange=%.1f radarAirports=%s\n",
                  gSsid, gPass[0] ? "set" : "none", gLat, gLon, gRadiusMi, enc, gRadarRangeMi, radarEnc);
}

void load(Preferences &p) {
    p.getString("ssid", gSsid, sizeof(gSsid));
    p.getString("pass", gPass, sizeof(gPass));
    gLat = p.getFloat("lat", HOME_LAT);
    gLon = p.getFloat("lon", HOME_LON);
    gRadiusMi = p.getFloat("radius", RADIUS_MI_AUTO);

    char enc[CallsignList::kEncodedMax];
    enc[p.getString("callsigns", enc, sizeof(enc))] = '\0';
    gCallsignCount = CallsignList::decode(enc, gCallsigns, CallsignList::kMax);

    const uint8_t mode = p.getUChar("dispMode", (uint8_t)DisplayMode::MaxDensity);
    gDisplayMode = mode <= (uint8_t)DisplayMode::Radar ? (DisplayMode)mode : DisplayMode::MaxDensity;

    gRadarRangeMi = p.getFloat("radarRange", DEFAULT_RADAR_RANGE_MI);
    char radarEnc[RadarAirportList::kEncodedMax];
    radarEnc[p.getString("radarAirports", radarEnc, sizeof(radarEnc))] = '\0';
    gRadarAirportCount = RadarAirportList::decode(radarEnc, gRadarAirports, RadarAirportList::kMax);
}
}  // namespace

void begin() {
    Preferences p;
    if (!p.begin("cfg", false)) {
        Serial.println("[cfg] NVS open failed");
        return;
    }
#ifdef HAVE_DEV_SECRETS
    if (!p.getBool("seeded", false)) {
        p.putString("ssid", WIFI_SSID);
        p.putString("pass", WIFI_PASSWORD);
        p.putBool("seeded", true);
        Serial.println("[cfg] seeded WiFi credentials from secrets.h (dev)");
    }
#endif
    load(p);
    p.end();
    logConfig();
}

bool hasWifi() { return gSsid[0] != '\0'; }
const char *ssid() { return gSsid; }
const char *pass() { return gPass; }
bool hasPass() { return gPass[0] != '\0'; }
float lat() { return gLat; }
float lon() { return gLon; }
float radiusOverrideMi() { return gRadiusMi; }
int callsignCount() { return gCallsignCount; }
const char *callsign(int i) { return (i >= 0 && i < gCallsignCount) ? gCallsigns[i].callsign : ""; }
const char *origin(int i) { return (i >= 0 && i < gCallsignCount) ? gCallsigns[i].origin : ""; }
const char *destination(int i) { return (i >= 0 && i < gCallsignCount) ? gCallsigns[i].destination : ""; }

int callsigns(CallsignList::Buf *out) {
    for (int i = 0; i < gCallsignCount; i++) strcpy(out[i], gCallsigns[i].callsign);
    return gCallsignCount;
}

bool pinFor(const char *callsign, char *outOrigin, char *outDestination) {
    outOrigin[0] = outDestination[0] = '\0';
    for (int i = 0; i < gCallsignCount; i++) {
        if (strcmp(gCallsigns[i].callsign, callsign) == 0) {
            strcpy(outOrigin, gCallsigns[i].origin);
            strcpy(outDestination, gCallsigns[i].destination);
            return true;
        }
    }
    return false;
}

float radarRangeMi() { return gRadarRangeMi; }
int radarAirportCount() { return gRadarAirportCount; }
const char *radarAirport(int i) { return (i >= 0 && i < gRadarAirportCount) ? gRadarAirports[i] : ""; }

DisplayMode displayMode() { return gDisplayMode; }

void setDisplayMode(DisplayMode mode) {
    if (mode == gDisplayMode) return;  // skip the NVS write on every same-mode tap/no-op set
    gDisplayMode = mode;
    Preferences p;
    if (!p.begin("cfg", false)) return;
    p.putUChar("dispMode", (uint8_t)mode);
    p.end();
    Serial.printf("[cfg] display mode -> %d\n", (int)mode);
}

const char *save(const char *ssid, const char *pass, float lat, float lon, float radiusMi,
                  const CallsignList::Entry *entries, int entryCount, float radarRangeMi,
                  const RadarAirportList::Code *radarAirports, int radarAirportCount) {
    if (!ssid || !ssid[0] || strlen(ssid) > kSsidMax) return "bad_ssid";
    // WPA2 passphrases are 8..63 characters; an empty one means an open network.
    if (pass && pass[0] && (strlen(pass) < 8 || strlen(pass) > kPassMax)) return "bad_pass";
    if (lat < -90.0f || lat > 90.0f) return "bad_lat";
    if (lon < -180.0f || lon > 180.0f) return "bad_lon";
    if (radiusMi != RADIUS_MI_AUTO && (radiusMi < 1.0f || radiusMi > 20000.0f)) return "bad_radius";
    if (radarRangeMi < RADAR_RANGE_MIN_MI || radarRangeMi > RADAR_RANGE_MAX_MI) return "bad_radar_range";
    if (const char *e = CallsignList::validate(entries, entryCount)) return e;
    // CallsignList::validate() only checked pin shape - confirm each non-empty pin is a real airport code.
    for (int i = 0; i < entryCount; i++) {
        if (entries[i].origin[0] && !AirportTable::findByCode(entries[i].origin)) return "bad_origin";
        if (entries[i].destination[0] && !AirportTable::findByCode(entries[i].destination)) return "bad_destination";
    }
    if (const char *e = RadarAirportList::validate(radarAirports, radarAirportCount)) {
        // RadarAirportList's own error names ("too_many_airports"/"duplicate_airport") are ambiguous
        // against CallsignList's ("too_many_callsigns"/etc.) once surfaced to the web page - rename here.
        if (!strcmp(e, "too_many_airports")) return "too_many_radar_airports";
        if (!strcmp(e, "duplicate_airport")) return "duplicate_radar_airport";
        return "bad_radar_airport";
    }
    // RadarAirportList::validate() only checked code shape - confirm each one is a real airport.
    for (int i = 0; i < radarAirportCount; i++) {
        if (!AirportTable::findByCode(radarAirports[i])) return "bad_radar_airport";
    }
    char enc[CallsignList::kEncodedMax];
    CallsignList::encode(entries, entryCount, enc, sizeof(enc));
    char radarEnc[RadarAirportList::kEncodedMax];
    RadarAirportList::encode(radarAirports, radarAirportCount, radarEnc, sizeof(radarEnc));

    Preferences p;
    if (!p.begin("cfg", false)) return "save_failed";
    p.putString("ssid", ssid);
    if (pass) p.putString("pass", pass);
    p.putFloat("lat", lat);
    p.putFloat("lon", lon);
    p.putFloat("radius", radiusMi);
    p.putString("callsigns", enc);
    p.putFloat("radarRange", radarRangeMi);
    p.putString("radarAirports", radarEnc);
    p.putBool("seeded", true);
    load(p);
    p.end();
    Serial.print("[cfg] saved: ");
    logConfig();
    return nullptr;
}

void forgetWifi() {
    Preferences p;
    if (!p.begin("cfg", false)) return;
    if (p.isKey("ssid")) p.remove("ssid");  // remove() on a missing key logs an error to the serial port
    if (p.isKey("pass")) p.remove("pass");
    p.putBool("seeded", true);  // do not re-seed from secrets.h
    gSsid[0] = gPass[0] = '\0';
    p.end();
    Serial.println("[cfg] WiFi credentials erased");
}

}  // namespace ConfigStore
