#include "DepartureVerifier.h"

#include <string.h>

#include "config.h"
#include "data/AirportTable.h"
#include "flight/FlightMath.h"
#include "network/OpenSkyClient.h"
#include "time/TimeSync.h"

namespace DepartureVerifier {

namespace {

struct CacheEntry {
    char callsign[12] = "";
    uint32_t day = 0;
    bool valid = false;
    bool verified = false;   // true = caller should not flag origin as suspect
    bool corrected = false;  // true = correctedCode/correctedName below apply
    char correctedCode[8] = "";
    char correctedName[40] = "";
    uint32_t lastUsedSeq = 0;
};

CacheEntry cache[DEPARTURE_VERIFY_CACHE_SIZE];
uint32_t useSeq = 0;

CacheEntry *findEntry(const char *callsign, uint32_t today) {
    for (int i = 0; i < DEPARTURE_VERIFY_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].day == today && strcmp(cache[i].callsign, callsign) == 0) {
            return &cache[i];
        }
    }
    return nullptr;
}

// Picks the first empty slot, or (cache full) the least-recently-used one -
// same eviction policy as RouteCache's allocEntry().
CacheEntry *allocEntry(const char *callsign, uint32_t today) {
    CacheEntry *victim = &cache[0];
    for (int i = 0; i < DEPARTURE_VERIFY_CACHE_SIZE; i++) {
        if (!cache[i].valid) {
            victim = &cache[i];
            break;
        }
        if (cache[i].lastUsedSeq < victim->lastUsedSeq) victim = &cache[i];
    }
    *victim = CacheEntry();
    strncpy(victim->callsign, callsign, sizeof(victim->callsign) - 1);
    victim->day = today;
    victim->valid = true;
    return victim;
}

void applyCorrection(Flight &f, const CacheEntry &entry) {
    strncpy(f.origin, entry.correctedCode, sizeof(f.origin) - 1);
    f.origin[sizeof(f.origin) - 1] = '\0';
    strncpy(f.originName, entry.correctedName, sizeof(f.originName) - 1);
    f.originName[sizeof(f.originName) - 1] = '\0';
}

}  // namespace

bool verifyOrCorrectOrigin(Flight &f, float claimedOriginLat, float claimedOriginLon,
                            bool haveClaimedOriginCoords) {
    if (!VERIFY_DEPARTURE_VIA_TRACK) return false;
    if (!haveClaimedOriginCoords) return false;
    if (!TimeSync::isSynced()) return false;  // per-day cache needs a real day bucket

    uint32_t today = TimeSync::currentLocalDay();
    CacheEntry *entry = findEntry(f.callsign, today);
    if (entry != nullptr) {
        entry->lastUsedSeq = ++useSeq;
        if (entry->corrected) applyCorrection(f, *entry);
        return entry->verified;
    }

    float trackLat, trackLon;
    if (!OpenSkyClient::fetchLiveTrackFirstPoint(f.icao24, &trackLat, &trackLon)) {
        // No track yet (common right after takeoff), rate-limited, or a
        // network error - don't cache a negative result, so a later poll
        // can retry once track data may actually exist.
        return false;
    }

    entry = allocEntry(f.callsign, today);
    entry->lastUsedSeq = ++useSeq;

    float distFromClaimed =
        FlightMath::haversineMiles(trackLat, trackLon, claimedOriginLat, claimedOriginLon);
    if (distFromClaimed <= DEPARTURE_VERIFY_MATCH_RADIUS_MI) {
        entry->verified = true;
        return true;
    }

    const AirportTable::AirportEntry *match =
        AirportTable::findNearest(trackLat, trackLon, DEPARTURE_VERIFY_MATCH_RADIUS_MI);
    if (match == nullptr) {
        entry->verified = false;  // no confident replacement - leave as-is
        return false;
    }

    const char *code = match->code;
    strncpy(entry->correctedCode, code, sizeof(entry->correctedCode) - 1);
    strncpy(entry->correctedName, match->name, sizeof(entry->correctedName) - 1);
    entry->corrected = true;
    entry->verified = true;
    applyCorrection(f, *entry);
    return true;
}

}  // namespace DepartureVerifier
