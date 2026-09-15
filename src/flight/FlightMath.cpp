#include "FlightMath.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "config.h"

namespace FlightMath {

static const float kEarthRadiusMi = 3958.8f;

float haversineMiles(float lat1, float lon1, float lat2, float lon2) {
    float p1 = radians(lat1), p2 = radians(lat2);
    float dphi = radians(lat2 - lat1);
    float dlambda = radians(lon2 - lon1);
    float a = sinf(dphi / 2) * sinf(dphi / 2) +
              cosf(p1) * cosf(p2) * sinf(dlambda / 2) * sinf(dlambda / 2);
    return 2 * kEarthRadiusMi * asinf(sqrtf(a));
}

void deadReckon(float lat, float lon, float headingDeg, float speedKt,
                 float elapsedS, float *outLat, float *outLon) {
    float distMi = speedKt * (elapsedS / 3600.0f) * 1.15078f;  // knots -> mph
    float delta = distMi / kEarthRadiusMi;
    float theta = radians(headingDeg);
    float phi1 = radians(lat);
    float lambda1 = radians(lon);
    float phi2 = asinf(sinf(phi1) * cosf(delta) +
                        cosf(phi1) * sinf(delta) * cosf(theta));
    float lambda2 = lambda1 + atan2f(sinf(theta) * sinf(delta) * cosf(phi1),
                                       cosf(delta) - sinf(phi1) * sinf(phi2));
    *outLat = degrees(phi2);
    *outLon = degrees(lambda2);
}

float bearingDeg(float lat1, float lon1, float lat2, float lon2) {
    float phi1 = radians(lat1), phi2 = radians(lat2);
    float dlambda = radians(lon2 - lon1);
    float y = sinf(dlambda) * cosf(phi2);
    float x = cosf(phi1) * sinf(phi2) - sinf(phi1) * cosf(phi2) * cosf(dlambda);
    float brg = degrees(atan2f(y, x));
    brg = fmodf(brg, 360.0f);
    if (brg < 0) brg += 360.0f;
    return brg;
}

void boundingBox(float lat, float lon, float radiusMi, float *outLamin,
                  float *outLamax, float *outLomin, float *outLomax) {
    float latDelta = radiusMi / 69.0f;
    // Guard against div-by-~0 near the poles (cos(lat) -> 0).
    float cosLat = cosf(radians(lat));
    if (cosLat < 0.01f) cosLat = 0.01f;
    float lonDelta = radiusMi / (69.0f * cosLat);
    *outLamin = max(lat - latDelta, -90.0f);
    *outLamax = min(lat + latDelta, 90.0f);
    *outLomin = max(lon - lonDelta, -180.0f);
    *outLomax = min(lon + lonDelta, 180.0f);
}

float effectiveRadius() {
    if (RADIUS_MI != RADIUS_MI_AUTO) return RADIUS_MI;
    return (TARGET_CALLSIGNS_COUNT > 0) ? CALLSIGN_SEARCH_RADIUS_MI
                                         : DEFAULT_RADIUS_MI;
}

bool isScheduledCallsign(const char *callsign) {
    size_t len = strlen(callsign);
    if (len < 4) return false;
    for (int i = 0; i < 3; i++) {
        if (!isalpha((unsigned char)callsign[i])) return false;
    }
    if (!isdigit((unsigned char)callsign[3])) return false;
    for (int i = 0; i < CHARTER_OPERATOR_PREFIXES_COUNT; i++) {
        if (strncmp(callsign, CHARTER_OPERATOR_PREFIXES[i], 3) == 0) return false;
    }
    return true;
}

bool routeIsPlausible(float curLat, float curLon, float originLat,
                       float originLon, float destLat, float destLon) {
    float total = haversineMiles(originLat, originLon, destLat, destLon);
    if (total <= 0) return true;  // nothing to check against
    float viaCurrent = haversineMiles(originLat, originLon, curLat, curLon) +
                        haversineMiles(curLat, curLon, destLat, destLon);
    // Proportional slack (50% over great-circle) plus a flat floor (150mi)
    // so short regional hops aren't flagged over ordinary vectoring.
    return viaCurrent <= total * 1.5f + 150.0f;
}

static const char *const kCardinalPoints[16] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};

const char *cardinalDirection(float brg) {
    float wrapped = fmodf(brg, 360.0f);
    if (wrapped < 0) wrapped += 360.0f;
    int idx = ((int)(wrapped / 22.5f + 0.5f)) % 16;
    return kCardinalPoints[idx];
}

void applyDeadReckoning(Flight *flights, int count, float elapsedS) {
    for (int i = 0; i < count; i++) {
        Flight &f = flights[i];
        if (f.onGround || f.speedKt < MIN_DEAD_RECKON_KT || !f.hasHeading) {
            continue;
        }
        float newLat, newLon;
        deadReckon(f.fixLat, f.fixLon, f.heading, f.speedKt, elapsedS, &newLat, &newLon);
        f.lat = newLat;
        f.lon = newLon;
        f.distanceMi = roundf(haversineMiles(HOME_LAT, HOME_LON, newLat, newLon) * 10) / 10;
        float rawAlt = f.fixAltitudeFt + f.vrateFpm * (elapsedS / 60.0f);
        int alt = (int)(roundf(rawAlt / 10.0f) * 10);
        f.altitudeFt = max(0, alt);
    }
}

}  // namespace FlightMath
