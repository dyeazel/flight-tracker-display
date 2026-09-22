#include "Health.h"

#include <Arduino.h>

#include "config.h"
#include "diag/Diag.h"
#include "network/NetTask.h"
#include "network/WifiManager.h"
#include "time/TimeSync.h"

namespace Health {

namespace {

uint32_t gLastCheckMs = 0;
uint32_t gWifiUpSinceMs = 0;    // millis() when the link last came up (0 = down)
uint32_t gWifiDownSinceMs = 0;  // millis() when it last went down / boot
uint32_t gLowHeapSinceMs = 0;   // millis() when maxAlloc first dropped below the threshold (0 = fine)
bool gStableCleared = false;

}  // namespace

void begin() { gWifiDownSinceMs = millis(); }

void update() {
    const uint32_t now = millis();
    if (now - gLastCheckMs < 1000) return;
    gLastCheckMs = now;

    // WiFi up/down bookkeeping - independent of whether NetTask's own reconnect is blocking or not, since
    // this only observes WifiManager::isConnected(), a fast non-blocking query.
    if (WifiManager::isConnected()) {
        if (!gWifiUpSinceMs) gWifiUpSinceMs = now ? now : 1;
        gWifiDownSinceMs = 0;
    } else {
        if (!gWifiDownSinceMs) gWifiDownSinceMs = now ? now : 1;
        gWifiUpSinceMs = 0;
    }

    // A stable run (some good data, long enough) clears the consecutive-health-reboot count, so a later,
    // unrelated outage is judged fresh rather than against old attempts.
    if (!gStableCleared && now > HEALTH_STABLE_MS && Diag::lifetimeOk() > 0) {
        gStableCleared = true;
        Diag::clearConsecutiveHealthReboots();
    }

    // Heap fragmentation tracking.
    if (ESP.getMaxAllocHeap() < HEALTH_MIN_MAXALLOC) {
        if (!gLowHeapSinceMs) gLowHeapSinceMs = now ? now : 1;
    } else {
        gLowHeapSinceMs = 0;
    }

    const bool allowed = Diag::consecutiveHealthReboots() < HEALTH_MAX_CONSECUTIVE;
    static bool warned = false;
    if (!allowed && !warned) {
        warned = true;
        Serial.printf("[health] %u health reboots in a row without a stable run: further health reboots suppressed\n",
                      Diag::consecutiveHealthReboots());
    }
    if (allowed) {
        // "no data" requires WiFi to have been continuously up for the same window - otherwise a WiFi
        // outage would trip both this and "wifi down" for the same underlying cause; "wifi down" is the
        // more specific diagnosis in that case.
        const uint32_t sinceOk = Diag::msSinceLastOk();
        const uint32_t sinceUp = gWifiUpSinceMs ? now - gWifiUpSinceMs : 0;
        if (gWifiUpSinceMs && TimeSync::isSynced() && sinceOk > HEALTH_NO_DATA_MS && sinceUp > HEALTH_NO_DATA_MS) {
            Diag::reboot("no data", true);
        }
        if (NetTask::msSinceLoop() > HEALTH_NET_STUCK_MS) Diag::reboot("net stuck", true);
        if (gWifiDownSinceMs && now - gWifiDownSinceMs > HEALTH_WIFI_DOWN_MS) Diag::reboot("wifi down", true);
        if (gLowHeapSinceMs && now - gLowHeapSinceMs > HEALTH_LOW_HEAP_MS) Diag::reboot("low heap", true);
    }

    // Scheduled restart: deferred while flights are currently on screen, forced anyway once very overdue.
    const bool flightsVisible = NetTask::lastPublishedCount() > 0;
    if (now > SELF_REBOOT_INTERVAL_MS &&
        (!flightsVisible || now > SELF_REBOOT_INTERVAL_MS + SELF_REBOOT_FORCE_EXTRA_MS)) {
        Diag::reboot("scheduled");
    }
}

}  // namespace Health
