#include "TimeSync.h"

#include <Arduino.h>
#include <esp_sntp.h>

#include "config.h"

namespace TimeSync {

namespace {
// 2020-01-01 00:00:00 UTC. Before NTP sync completes, time() reads back
// near the 1970 epoch - anything at or past this is a reliable "sync has
// happened" signal without needing a separate callback/flag from
// configTzTime()'s underlying SNTP client.
const time_t kPlausibleEpoch = 1577836800;
}  // namespace

void begin() {
    // Sets the NTP servers (falling back through NTP_SERVER2/3 if the first is slow/unreachable - a
    // single-server config had no fallback if pool.ntp.org itself was having a bad day) and the POSIX
    // TZ rule in one call, so localtime() reflects HOME_TZ_POSIX (including DST) as soon as sync
    // completes - not just a raw UTC offset that would drift wrong twice a year.
    configTzTime(HOME_TZ_POSIX, NTP_SERVER, NTP_SERVER2, NTP_SERVER3);
}

bool isSynced() { return time(nullptr) >= kPlausibleEpoch; }

void update(bool wifiUp) {
    static uint32_t lastKickMs = 0;
    static bool wasUp = false;
    if (isSynced()) return;
    if (!wifiUp) {
        wasUp = false;
        return;
    }
    // Kick at the moment the link comes up (SNTP was configured before WiFi existed, in begin()), then
    // keep re-kicking every NTP_KICK_MS until synced - faster than waiting out lwIP's own ~15s default
    // retry cadence.
    if (!wasUp || millis() - lastKickMs >= NTP_KICK_MS) {
        wasUp = true;
        lastKickMs = millis();
        sntp_restart();
    }
}

time_t nowUtc() { return isSynced() ? time(nullptr) : 0; }

uint32_t currentLocalDay() {
    if (!isSynced()) return 0;
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    return (uint32_t)(local.tm_year + 1900) * 10000 + (uint32_t)(local.tm_mon + 1) * 100 +
           (uint32_t)local.tm_mday;
}

}  // namespace TimeSync
