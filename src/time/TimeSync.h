#pragma once

#include <time.h>
#include <stdint.h>

// NTP + timezone wall-clock sync. Needed only for DepartureVerifier's per-local-day cache,
// RouteCache's per-local-day cache freshness and multi-leg time-window matching, and
// FlightSelector::enrich()'s estimated dep/arr times - nothing else in this app needs wall-clock time
// (only millis() uptime), and all three silently no-op until isSynced() is true. Call begin() once,
// before WiFi is even up - see update() below for why that's fine (SNTP just needs to be configured
// before it can be kicked, not connected yet).
namespace TimeSync {

void begin();

// True once the clock has actually been set from NTP (time() reads back
// a plausible value, not the 1970 epoch default).
bool isSynced();

// Call frequently (e.g. every NetTask loop tick) with the current WiFi state. begin() configures SNTP
// before WiFi exists, so lwIP's own request only goes out once the link comes up and otherwise retries
// on its own ~15s cadence - this instead restarts the request the moment wifiUp becomes true, then
// keeps re-kicking every NTP_KICK_MS until synced, so a fresh boot or reconnect doesn't sit through a
// stale in-flight request or wait out lwIP's slower default retry before DepartureVerifier/RouteCache/
// estimated times start working. No-ops once synced or while wifiUp is false.
void update(bool wifiUp);

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
