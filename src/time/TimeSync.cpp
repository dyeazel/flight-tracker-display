#include "TimeSync.h"

#include <Arduino.h>

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
    // Sets both the NTP server and the POSIX TZ rule in one call, so
    // localtime() reflects HOME_TZ_POSIX (including DST) as soon as sync
    // completes - not just a raw UTC offset that would drift wrong twice
    // a year.
    configTzTime(HOME_TZ_POSIX, NTP_SERVER);
}

bool isSynced() { return time(nullptr) >= kPlausibleEpoch; }

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
