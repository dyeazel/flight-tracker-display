#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "config/ConfigStore.h"
#include "diag/Diag.h"
#include "diag/SerialCommands.h"
#include "display/DisplayManager.h"
#include "flight/Flight.h"
#include "flight/FlightMath.h"
#include "flight/FlightSelector.h"
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

// Health::Hooks::isBusy: a scheduled reboot is deferred while flights are on screen.
// Health::Hooks::isWifiConfigured: ConfigStore::hasWifi() (Phase 7) - so "wifi down" isn't reported while
// a freshly-flashed/forgotten board is legitimately sitting unconfigured, awaiting the setup page.
static bool flightsVisible() { return NetTask::lastPublishedCount() > 0; }

// WifiManager::Credentials: NVS-backed via ConfigStore (Phase 7), same shape as ../../mlb-board's own
// consumer of this hook. reconfigure() is called after a serial "set"/"forget" (see SerialCommands.cpp).
static bool wifiHasCreds() { return ConfigStore::hasWifi(); }
static const char *wifiSsid() { return ConfigStore::ssid(); }
static bool wifiHasPass() { return ConfigStore::hasPass(); }
static const char *wifiPass() { return ConfigStore::pass(); }

// Shown only until the very first successful publish ever - after that, main() always has real flights to
// draw (see loop()) and never falls back to this again, even through a long outage (the stale heartbeat
// marker takes over instead). Gated on a "which state did we last draw" static so it doesn't repaint every
// tick while nothing has changed.
static void showPreDataStatus() {
    static int lastState = -2;
    static char lastFailure[24] = "";
    int state;
    if (WifiManager::state() == WifiManager::State::Unconfigured) {
        state = -1;  // no WiFi credentials saved yet - see ConfigStore
    } else if (!WifiManager::isConnected()) {
        state = 0;
    } else if (!TimeSync::isSynced()) {
        state = 1;
    } else if (Diag::lifetimeFail() > 0 && Diag::consecutiveFails() > 0) {
        state = 2;  // NetTask's most recent attempt failed and none has ever succeeded
    } else {
        state = 3;
    }
    // In state 0, WifiManager::lastFailure() can change (e.g. "wrong_password" -> "timeout" across
    // retries with different failure modes) without the bucket itself changing - redraw on that too, so
    // someone on-site debugging a bad AP config sees the current reason, not a stale first one.
    bool failureChanged = false;
    if (state == 0 && strcmp(WifiManager::lastFailure(), lastFailure) != 0) {
        strncpy(lastFailure, WifiManager::lastFailure(), sizeof(lastFailure) - 1);
        lastFailure[sizeof(lastFailure) - 1] = '\0';
        failureChanged = true;
    }
    if (state == lastState && !failureChanged) return;
    lastState = state;
    display.clear(display.black);
    switch (state) {
        case -1:
            display.drawText8x8(4, 4, "Setup needed", display.white, display.black);
            display.drawText8x8(4, 14, "Open web/config.html and", display.white, display.black);
            display.drawText8x8(4, 24, "connect this board by USB", display.white, display.black);
            break;
        case 0:
            display.drawText8x8(4, 4, "Connecting to WiFi...", display.white, display.black);
            if (lastFailure[0]) {
                display.drawText8x8(4, 14, lastFailure, display.red, display.black);
            }
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
    ConfigStore::begin();

    display.begin();
    TimeSync::begin();
    Serial.println("[time] NTP sync requested");

    WifiManager::begin({wifiHasCreds, wifiSsid, wifiHasPass, wifiPass});

#ifdef SIM_DATA
    // Scripted scenes need no network task - just wifi (for NTP, so relative dep/arr times work) and
    // time sync. No NetTask to keep responsive here, so a simple blocking wait is fine.
    while (!WifiManager::isConnected()) {
        WifiManager::update();
        TimeSync::update(false);
        delay(50);
    }
    Serial.print("[wifi] connected! IP: ");
    Serial.println(WiFi.localIP());
    // Bounded, not indefinite: this is a demo/test build (SIM_DATA), and NTP being unreachable on some
    // bench network shouldn't hang scene iteration forever - only the timeline row's "current time"
    // marker is affected if it never lands.
    for (uint32_t start = millis(); !TimeSync::isSynced() && millis() - start < 10000; delay(50)) {
        TimeSync::update(true);
    }
#else
    Health::begin({ConfigStore::hasWifi, flightsVisible});
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
    static FlightSelector::MissingStatus simMissingStatus[MAX_ROWS];
    static time_t simMissingWhen[MAX_ROWS];
    static int shown = -2;
    const int scene = SimFlights::current(millis());
    if (scene != shown) {
        shown = scene;
        const SimFlights::Scene sc = SimFlights::fill(scene, simFlights, MAX_ROWS, simMissing, simMissingStatus,
                                                       simMissingWhen, (long)TimeSync::nowUtc());
        if (sc.error) {
            display.clear(display.black);
            display.drawText8x8(4, 4, "Error fetching data:", display.red, display.black);
            display.drawText8x8(4, 14, "Sim: connection failed", display.white, display.black);
        } else {
            Renderer::render(display, simFlights, sc.count, simMissing, simMissingStatus, simMissingWhen,
                              sc.missingCount, sc.anyFlagged);
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
    static FlightSelector::MissingStatus uiMissingStatus[MAX_ROWS];
    static time_t uiMissingWhen[MAX_ROWS];
    static int uiCount = 0, uiMissingCount = 0;
    static bool uiAnyFlagged = false;
    static uint32_t uiFixMs = 0;
    static bool uiHaveData = false;
    static uint32_t lastSoakLogMs = 0;

    Flight tmpFlights[MAX_ROWS];
    char tmpMissing[MAX_ROWS][12];
    FlightSelector::MissingStatus tmpMissingStatus[MAX_ROWS];
    time_t tmpMissingWhen[MAX_ROWS];
    int tmpCount = 0, tmpMissingCount = 0;
    bool tmpAnyFlagged = false;
    uint32_t fixMs = 0;
    const bool got = NetTask::getFlights(tmpFlights, MAX_ROWS, &tmpCount, tmpMissing, tmpMissingStatus,
                                         tmpMissingWhen, MAX_ROWS, &tmpMissingCount, &tmpAnyFlagged, &fixMs);

    if (!got) {
        showPreDataStatus();
    } else if (!uiHaveData || fixMs != uiFixMs) {
        // A fresh publish since our last tick (or our very first one): adopt it and do a full redraw.
        for (int i = 0; i < tmpCount; i++) uiFlights[i] = tmpFlights[i];
        for (int i = 0; i < tmpMissingCount; i++) {
            memcpy(uiMissing[i], tmpMissing[i], sizeof(uiMissing[i]));
            uiMissingStatus[i] = tmpMissingStatus[i];
            uiMissingWhen[i] = tmpMissingWhen[i];
        }
        uiCount = tmpCount;
        uiMissingCount = tmpMissingCount;
        uiAnyFlagged = tmpAnyFlagged;
        uiFixMs = fixMs;
        uiHaveData = true;
        Renderer::render(display, uiFlights, uiCount, uiMissing, uiMissingStatus, uiMissingWhen, uiMissingCount,
                          uiAnyFlagged);
        logSoakLine();
        lastSoakLogMs = now;
    } else {
        // Same data as last tick: just advance dead reckoning from the real fix, capped so a long outage
        // settles rather than extrapolating positions into nonsense.
        float elapsedS = (now - uiFixMs) / 1000.0f;
        if (elapsedS > DEAD_RECKON_MAX_S) elapsedS = (float)DEAD_RECKON_MAX_S;
        FlightMath::applyDeadReckoning(uiFlights, uiCount, elapsedS, ConfigStore::lat(), ConfigStore::lon());
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
