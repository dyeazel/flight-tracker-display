#include "OpenSkyParse.h"

#include <math.h>
#include <string.h>

#include "flight/FlightMath.h"

namespace OpenSkyParse {

namespace {

// Ranks a as "should sort before b" - likely-commercial flights (by callsign shape; real airline enrichment
// hasn't happened yet at parse time, so this callsign heuristic - the same one FlightSelectorEnrich.cpp
// trusts to decide whether a flight is worth a route lookup - is the only commercial signal available
// here) always sort ahead of GA, and within the same tier, nearer sorts ahead of farther. Keeps a busy
// area's local GA traffic from filling every slot before a farther-out airliner is ever kept - see
// insertSorted()'s comment.
bool ranksBefore(const Flight &a, const Flight &b) {
    const bool aCommercial = FlightMath::isScheduledCallsign(a.callsign);
    const bool bCommercial = FlightMath::isScheduledCallsign(b.callsign);
    if (aCommercial != bCommercial) return aCommercial;
    return a.distanceMi < b.distanceMi;
}

// Inserts f into the maxCount-sized array arr (sorted by ranksBefore()), keeping only the best maxCount
// candidates seen so far - avoids needing a separate large buffer for all candidates before
// sorting/truncating, unlike a naive "collect everything then sort" port.
void insertSorted(Flight *arr, int &count, int maxCount, const Flight &f) {
    if (count < maxCount) {
        int i = count;
        while (i > 0 && ranksBefore(f, arr[i - 1])) {
            arr[i] = arr[i - 1];
            i--;
        }
        arr[i] = f;
        count++;
    } else if (maxCount > 0 && ranksBefore(f, arr[maxCount - 1])) {
        int i = maxCount - 1;
        while (i > 0 && ranksBefore(f, arr[i - 1])) {
            arr[i] = arr[i - 1];
            i--;
        }
        arr[i] = f;
    }
}

}  // namespace

void parseStates(JsonVariant root, float radiusMi, float homeLat, float homeLon, Flight *outFlights,
                  int maxOut, int *outCount, const CallsignList::Buf *targetCallsigns,
                  int targetCallsignCount) {
    *outCount = 0;
    // Tracking specific callsigns: scattered into slots by each target's own configured index (not
    // appended in OpenSky's arbitrary state order) so the display always reads in the order the user
    // entered them - e.g. a friend's itinerary shown leg by leg - and stays stable poll to poll even if
    // OpenSky's own ordering isn't. Compacted into outFlights below once the scan is done.
    Flight targetSlots[CallsignList::kMax];
    bool targetFound[CallsignList::kMax] = {false};

    JsonArray states = root["states"].as<JsonArray>();
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

        float dist = FlightMath::haversineMiles(homeLat, homeLon, lat, lon);
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

        if (targetCallsignCount > 0) {
            // Tracking specific callsigns: not distance-sorted (a tracked flight could be anywhere) -
            // record into this target's own slot; see the scatter/compact comment above.
            for (int ti = 0; ti < targetCallsignCount; ti++) {
                if (strcmp(f.callsign, targetCallsigns[ti]) == 0) {
                    targetSlots[ti] = f;
                    targetFound[ti] = true;
                    break;
                }
            }
        } else {
            insertSorted(outFlights, *outCount, maxOut, f);
        }
    }

    if (targetCallsignCount > 0) {
        for (int ti = 0; ti < targetCallsignCount && *outCount < maxOut; ti++) {
            if (targetFound[ti]) outFlights[(*outCount)++] = targetSlots[ti];
        }
    }
}

bool parseTrackFirstPoint(JsonVariant root, float *outLat, float *outLon) {
    JsonArray path = root["path"].as<JsonArray>();
    if (path.size() == 0) return false;
    JsonArray first = path[0].as<JsonArray>();
    // Index 1 = latitude, 2 = longitude - see OpenSky's /tracks waypoint
    // schema (time, lat, lon, baro_altitude, true_track, on_ground).
    if (first.size() < 3 || first[1].isNull() || first[2].isNull()) return false;
    *outLat = first[1].as<float>();
    *outLon = first[2].as<float>();
    return true;
}

}  // namespace OpenSkyParse
