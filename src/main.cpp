#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "diag/Diag.h"
#include "diag/SerialCommands.h"
#include "display/DisplayManager.h"
#include "flight/Flight.h"
#include "flight/FlightMath.h"
#include "flight/SimFlights.h"
#include "network/NetTask.h"
#include "network/OpenSkyClient.h"
#include "network/WifiManager.h"
#include "render/Renderer.h"
#include "time/TimeSync.h"
#include "watchdog/Health.h"
#include "watchdog/WatchdogManager.h"

// Full app: Arduino/C++ port of cyd_nearby_flights.py's main() loop, restructured (2026-09-22, "Phase 2")
// so networking can never block this task. All fetching/enriching now happens on NetTask (its own FreeRTOS
// task - see network/NetTask.h); this loop() only reads what it last published and draws. See Health.h for
// the recovery logic this split enables (a stuck network call used to be able to freeze the whole board -
// measured live in a 2026-09-21 soak run - and now cannot).

DisplayManager display;

static void logSoakLine() {
    Serial.printf(
        "[soak] t=%lu heap=%u maxAlloc=%u bootCount=%u fetchOk=%u fetchFail=%u wifiReconnects=%u\n",
        millis(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(), Diag::bootCount(), Diag::lifetimeOk(),
        Diag::lifetimeFail(), Diag::lifetimeWifiReconnects());
}

// Shown only until the very first successful publish ever - after that, main() always has real flights to
// draw (see loop()) and never falls back to this again, even through a long outage (the stale heartbeat
// marker takes over instead). Gated on a "which state did we last draw" static so it doesn't repaint every
// tick while nothing has changed.
static void showPreDataStatus() {
    static int lastState = -1;
    int state;
    if (!WifiManager::isConnected()) {
        state = 0;
    } else if (!TimeSync::isSynced()) {
        state = 1;
    } else if (Diag::lifetimeFail() > 0 && Diag::consecutiveFails() > 0) {
        state = 2;  // NetTask's most recent attempt failed and none has ever succeeded
    } else {
        state = 3;
    }
    if (state == lastState) return;
    lastState = state;
    display.clear(display.black);
    switch (state) {
        case 0:
            display.drawText8x8(4, 4, "Connecting to WiFi...", display.white, display.black);
            break;
        case 1:
            display.drawText8x8(4, 4, "Syncing time...", display.white, display.black);
            break;
        case 2:
            display.drawText8x8(4, 4, "Error fetching data:", display.red, display.black);
            // OpenSkyClient::lastErrorMessage() is written by NetTask without a lock - fine for a status
            // line that only needs to be eventually-consistent and repaints again in well under a second
            // if it happens to be caught mid-update.
            display.drawText8x8(4, 14, OpenSkyClient::lastErrorMessage(), display.white, display.black);
            break;
        default:
            display.drawText8x8(4, 4, "Loading...", display.white, display.black);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Diag::begin();
    Serial.printf("=== flight-tracker-arduino booting (boot #%u, reason=%s) ===\n", Diag::bootCount(),
                  Diag::bootReason());

    display.begin();
    TimeSync::begin();
    Serial.println("[time] NTP sync requested");

#ifdef SIM_DATA
    // Scripted scenes need no network task - just wifi (for NTP, so relative dep/arr times work) and time
    // sync, both via the old simple blocking call; see network/WifiManager.h.
    WifiManager::connect();
    Serial.print("[wifi] connected! IP: ");
    Serial.println(WiFi.localIP());
#else
    Health::begin();
    NetTask::begin();  // owns WiFi connect/reconnect and all fetching from here on
    Serial.println("[net] task started");
#endif

    WatchdogManager::begin();  // last: everything above may legitimately take a while
    Serial.println("[watchdog] armed");
}

void loop() {
    WatchdogManager::markProgress();
    SerialCommands::update();

    static bool timeSyncLogged = false;
    if (!timeSyncLogged && TimeSync::isSynced()) {
        time_t now = TimeSync::nowUtc();
        struct tm local;
        localtime_r(&now, &local);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &local);
        Serial.printf("[time] synced: %s (local)\n", buf);
        timeSyncLogged = true;
    }

#ifdef SIM_DATA
    static Flight simFlights[MAX_ROWS];
    static char simMissing[MAX_ROWS][12];
    static int shown = -2;
    const int scene = SimFlights::current(millis());
    if (scene != shown) {
        shown = scene;
        const SimFlights::Scene sc =
            SimFlights::fill(scene, simFlights, MAX_ROWS, simMissing, (long)TimeSync::nowUtc());
        if (sc.error) {
            display.clear(display.black);
            display.drawText8x8(4, 4, "Error fetching data:", display.red, display.black);
            display.drawText8x8(4, 14, "Sim: connection failed", display.white, display.black);
        } else {
            Renderer::render(display, simFlights, sc.count, simMissing, sc.missingCount, sc.anyFlagged);
            if (sc.refreshing) Renderer::drawRefreshing(display);
        }
        Serial.printf("[sim] scene %d/%d %s\n", scene, SimFlights::count(), SimFlights::name(scene));
    }
    delay(100);
    return;
#else
    Health::update();

    static uint32_t lastTickMs = 0;
    const uint32_t now = millis();
    if (now - lastTickMs < REDRAW_SECONDS * 1000UL) {
        delay(20);
        return;
    }
    lastTickMs = now;

    static Flight uiFlights[MAX_ROWS];
    static char uiMissing[MAX_ROWS][12];
    static int uiCount = 0, uiMissingCount = 0;
    static bool uiAnyFlagged = false;
    static uint32_t uiFixMs = 0;
    static bool uiHaveData = false;
    static uint32_t lastSoakLogMs = 0;

    Flight tmpFlights[MAX_ROWS];
    char tmpMissing[MAX_ROWS][12];
    int tmpCount = 0, tmpMissingCount = 0;
    bool tmpAnyFlagged = false;
    uint32_t fixMs = 0;
    const bool got = NetTask::getFlights(tmpFlights, MAX_ROWS, &tmpCount, tmpMissing, MAX_ROWS,
                                         &tmpMissingCount, &tmpAnyFlagged, &fixMs);

    if (!got) {
        showPreDataStatus();
    } else if (!uiHaveData || fixMs != uiFixMs) {
        // A fresh publish since our last tick (or our very first one): adopt it and do a full redraw.
        for (int i = 0; i < tmpCount; i++) uiFlights[i] = tmpFlights[i];
        for (int i = 0; i < tmpMissingCount; i++) memcpy(uiMissing[i], tmpMissing[i], sizeof(uiMissing[i]));
        uiCount = tmpCount;
        uiMissingCount = tmpMissingCount;
        uiAnyFlagged = tmpAnyFlagged;
        uiFixMs = fixMs;
        uiHaveData = true;
        Renderer::render(display, uiFlights, uiCount, uiMissing, uiMissingCount, uiAnyFlagged);
        logSoakLine();
        lastSoakLogMs = now;
    } else {
        // Same data as last tick: just advance dead reckoning from the real fix, capped so a long outage
        // settles rather than extrapolating positions into nonsense.
        float elapsedS = (now - uiFixMs) / 1000.0f;
        if (elapsedS > DEAD_RECKON_MAX_S) elapsedS = (float)DEAD_RECKON_MAX_S;
        FlightMath::applyDeadReckoning(uiFlights, uiCount, elapsedS);
        Renderer::renderValues(display, uiFlights, uiCount);
        if (now - lastSoakLogMs >= 60000UL) {
            logSoakLine();
            lastSoakLogMs = now;
        }
    }

    if (uiHaveData) {
        if (NetTask::isPolling()) {
            Renderer::drawRefreshing(display);
        } else if (Diag::msSinceLastOk() >
                   3UL * (uint32_t)OpenSkyClient::currentRefreshInterval() * 1000UL + 30000UL) {
            Renderer::drawStale(display);
        }
    }
#endif
}
