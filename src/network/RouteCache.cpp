#include "RouteCache.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "time/TimeSync.h"

namespace RouteCache {

namespace {

struct CacheEntry {
    char callsign[12] = "";
    uint32_t cachedDay = 0;
    uint8_t legCount = 0;
    RouteLeg legs[ROUTE_CACHE_MAX_LEGS];
    bool valid = false;
    uint32_t lastUsedMs = 0;
    // Real-time (not simulated) millis() of the last actual provider
    // fetch for this callsign - distinct from lastUsedMs (bumped on every
    // cache hit, used, used for LRU eviction). Gates the per-callsign
    // ROUTE_CACHE_MIN_REFETCH_INTERVAL_MS floor on the staleness-refetch
    // path below.
    unsigned long lastFetchMs = 0;
};

CacheEntry cache[ROUTE_CACHE_SIZE];
unsigned long lastCallMs = 0;

// Persisted (NVS) monthly request budget, mirroring the soak-counter
// pattern already used in main.cpp - a plain RAM counter would reset on
// every reboot/self-reboot and silently blow through the real monthly cap.
Preferences budgetPrefs;
bool budgetLoaded = false;
uint32_t budgetMonth = 0;
uint32_t budgetCount = 0;

void loadBudget() {
    if (budgetLoaded) return;
    budgetPrefs.begin("route", false);
    budgetMonth = budgetPrefs.getUInt("month", 0);
    budgetCount = budgetPrefs.getUInt("count", 0);
    budgetLoaded = true;
}

// Resets the counter when the local month has rolled over, and reports
// whether a fresh RouteProvider call is still within budget this month.
bool budgetAllows(uint32_t localDay) {
    loadBudget();
    uint32_t month = localDay / 100;  // yyyymmdd -> yyyymm
    if (month != budgetMonth) {
        budgetMonth = month;
        budgetCount = 0;
        budgetPrefs.putUInt("month", budgetMonth);
        budgetPrefs.putUInt("count", budgetCount);
    }
    return budgetCount < ROUTE_PROVIDER_MONTHLY_BUDGET;
}

void recordCall() {
    budgetCount++;
    budgetPrefs.putUInt("count", budgetCount);
}

CacheEntry *findEntry(const char *callsign) {
    for (int i = 0; i < ROUTE_CACHE_SIZE; i++) {
        if (cache[i].valid && strcmp(cache[i].callsign, callsign) == 0) return &cache[i];
    }
    return nullptr;
}

// Picks a slot for a new callsign: the first empty one, or (cache full)
// the least-recently-used entry. 32 slots should comfortably exceed the
// number of distinct commercial callsigns seen near one fixed location in
// a day, so eviction pressure is expected to be rare in practice.
CacheEntry *allocEntry(const char *callsign) {
    CacheEntry *victim = &cache[0];
    for (int i = 0; i < ROUTE_CACHE_SIZE; i++) {
        if (!cache[i].valid) {
            victim = &cache[i];
            break;
        }
        if (cache[i].lastUsedMs < victim->lastUsedMs) victim = &cache[i];
    }
    *victim = CacheEntry();
    strncpy(victim->callsign, callsign, sizeof(victim->callsign) - 1);
    return victim;
}

// Picks whichever leg's [depUtc, arrUtc] window contains `now`; failing
// that, the leg closest in time. This is what resolves a multi-city
// callsign (e.g. one flight number flying ORD->DFW->LAX in a day) to the
// correct currently-active city pair using data already fetched in one
// call - no extra network round-trip needed to disambiguate.
bool pickActiveLeg(const CacheEntry &entry, time_t now, RouteLeg *out) {
    if (entry.legCount == 0) return false;
    const RouteLeg *best = nullptr;
    long bestDist = 0;
    for (int i = 0; i < entry.legCount; i++) {
        const RouteLeg &leg = entry.legs[i];
        if (leg.depUtc != 0 && leg.arrUtc != 0 && now >= leg.depUtc && now <= leg.arrUtc) {
            *out = leg;
            return true;
        }
        long dist = 0;
        if (leg.depUtc != 0 && now < leg.depUtc) {
            dist = leg.depUtc - now;
        } else if (leg.arrUtc != 0 && now > leg.arrUtc) {
            dist = now - leg.arrUtc;
        }
        if (best == nullptr || dist < bestDist) {
            best = &leg;
            bestDist = dist;
        }
    }
    if (best == nullptr) return false;
    *out = *best;
    return true;
}

// True only if EVERY cached leg's arrival is both known and more than
// ROUTE_CACHE_STALE_GRACE_SEC in the past - i.e. nothing is currently
// active or still upcoming (a layover between two already-known legs
// correctly returns false here, since the next leg hasn't arrived yet).
// An entry with no legs at all (a remembered "nothing today" result) is
// never considered expired this way - that's intentionally retried only
// once per day, unchanged from the original design.
bool allLegsExpired(const CacheEntry &entry, time_t now) {
    if (entry.legCount == 0) return false;
    for (int i = 0; i < entry.legCount; i++) {
        const RouteLeg &leg = entry.legs[i];
        if (leg.arrUtc == 0) return false;
        if (now <= leg.arrUtc + (time_t)ROUTE_CACHE_STALE_GRACE_SEC) return false;
    }
    return true;
}

}  // namespace

bool lookupActiveLeg(const char *callsign, RouteLeg *out) {
    if (!TimeSync::isSynced()) return false;
    uint32_t today = TimeSync::currentLocalDay();
    time_t now = TimeSync::nowUtc();

    CacheEntry *entry = findEntry(callsign);

    // Cached for today, but every known leg's arrival (+ grace) has
    // already passed - re-check with the provider (a new leg may since
    // have appeared) rather than keep serving arrived/aged-out data
    // forever, but no more than once per callsign per
    // ROUTE_CACHE_MIN_REFETCH_INTERVAL_MS - otherwise a callsign with no
    // new leg yet would get hammered on every ~30s poll waiting for one.
    if (entry != nullptr && entry->cachedDay == today && allLegsExpired(*entry, now) &&
        (millis() - entry->lastFetchMs) >= ROUTE_CACHE_MIN_REFETCH_INTERVAL_MS) {
        unsigned long nowMs = millis();
        if ((lastCallMs == 0 || nowMs - lastCallMs >= ROUTE_PROVIDER_MIN_INTERVAL_MS) &&
            budgetAllows(today)) {
            RouteLeg legs[ROUTE_CACHE_MAX_LEGS];
            int legCount = 0;
            lastCallMs = nowMs;
            bool ok = fetchRouteLegs(callsign, legs, ROUTE_CACHE_MAX_LEGS, &legCount);
            recordCall();
            entry->lastFetchMs = nowMs;
            if (ok) {
                entry->legCount = (uint8_t)legCount;
                for (int i = 0; i < legCount; i++) entry->legs[i] = legs[i];
            }
            // else: refetch failed - keep serving the previous (now
            // aged-out) legs rather than blank them; pickActiveLeg below
            // still returns the best (if stale) match.
        }
        // else: globally rate-limited or over budget - fall through and
        // keep serving the aged-out entry as-is.
    }

    if (entry == nullptr || entry->cachedDay != today) {
        unsigned long nowMs = millis();
        bool rateOk = (lastCallMs == 0) || (nowMs - lastCallMs >= ROUTE_PROVIDER_MIN_INTERVAL_MS);
        if (rateOk && budgetAllows(today)) {
            RouteLeg legs[ROUTE_CACHE_MAX_LEGS];
            int legCount = 0;
            lastCallMs = nowMs;
            bool ok = fetchRouteLegs(callsign, legs, ROUTE_CACHE_MAX_LEGS, &legCount);
            recordCall();
            entry = allocEntry(callsign);
            entry->cachedDay = today;
            entry->valid = true;
            entry->lastFetchMs = nowMs;
            if (ok) {
                entry->legCount = (uint8_t)legCount;
                for (int i = 0; i < legCount; i++) entry->legs[i] = legs[i];
            } else {
                entry->legCount = 0;  // remember "nothing today" - don't retry all day
            }
        } else if (entry == nullptr) {
            // Rate-limited or over budget, and nothing cached at all -
            // nothing to fall back to this cycle.
            return false;
        }
        // else: rate-limited/over budget but a stale entry exists - keep
        // using it rather than blocking enrich() entirely; it'll refresh
        // once the limiter allows it.
    }

    entry->lastUsedMs = millis();
    return pickActiveLeg(*entry, now, out);
}

}  // namespace RouteCache
