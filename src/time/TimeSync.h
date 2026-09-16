#pragma once

#include <time.h>
#include <stdint.h>

// NTP + timezone wall-clock sync. Needed only for RouteCache's per-local-
// day cache freshness and multi-leg time-window matching - nothing else
// in this app needs wall-clock time (only millis() uptime). Call begin()
// once, after WiFi is up.
namespace TimeSync {

void begin();

// True once the clock has actually been set from NTP (time() reads back
// a plausible value, not the 1970 epoch default).
bool isSynced();

// Raw UTC epoch, or 0 if not yet synced. Use directly for RouteLeg
// depUtc/arrUtc comparisons - those are plain epoch-vs-epoch, so this is
// timezone-independent.
time_t nowUtc();

// Calendar day at the tracker's fixed home location (HOME_TZ_POSIX),
// encoded as year*10000 + month*100 + day (e.g. 20260915). 0 if not yet
// synced. Used only for RouteCache's cache-freshness bucketing - see
// config.h's HOME_TZ_POSIX comment for why this must be local, not UTC.
uint32_t currentLocalDay();

}  // namespace TimeSync
