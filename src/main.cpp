#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

#include "config.h"
#include "display/DisplayManager.h"
#include "flight/Flight.h"
#include "flight/FlightMath.h"
#include "flight/FlightSelector.h"
#include "network/OpenSkyClient.h"
#include "network/WifiManager.h"
#include "render/Renderer.h"
#include "time/TimeSync.h"
#include "watchdog/WatchdogManager.h"

// Full app: Arduino/C++ port of cyd_nearby_flights.py's main() loop.
// Milestones 0-4 (toolchain, WiFi, display orientation, auth OpenSky call,
// data pipeline) were validated independently on real hardware before this
// was assembled - see git history for each milestone's standalone test.

DisplayManager display;
static Flight flights[MAX_ROWS];
static char missing[MAX_ROWS][12];

// Milestone 8 soak-test instrumentation. Originally used RTC_DATA_ATTR
// (RTC "slow memory," which is supposed to survive any reset except a
// true power-on/brownout) so these counters would accumulate across the
// whole soak run, not just since the last boot. That didn't hold in
// practice: a real 9h44m soak run showed these counters reset to zero at
// the scheduled 6h self-reboot (esp_restart_noos()) even though the
// reboot itself was clean (SW_CPU_RESET, no panic) - i.e.
// esp_restart_noos() does not reliably preserve RTC memory here, for
// reasons not confirmed (no source available for this framework
// distribution to inspect further). Switched to NVS-backed Preferences
// instead - genuinely non-volatile flash storage, immune to whatever
// RTC-domain behavior esp_restart_noos() has, at the cost of a small
// flash write once per fetch cycle (well within flash wear limits at
// this cadence).
Preferences soakPrefs;
static uint32_t bootCount = 0;
static uint32_t fetchOkCount = 0;
static uint32_t fetchFailCount = 0;
static uint32_t wifiReconnectCount = 0;

static void loadSoakCounters() {
    soakPrefs.begin("soak", false);
    bootCount = soakPrefs.getUInt("bootCount", 0) + 1;
    fetchOkCount = soakPrefs.getUInt("fetchOk", 0);
    fetchFailCount = soakPrefs.getUInt("fetchFail", 0);
    wifiReconnectCount = soakPrefs.getUInt("wifiRecon", 0);
    soakPrefs.putUInt("bootCount", bootCount);
}

static void saveSoakCounters() {
    soakPrefs.putUInt("fetchOk", fetchOkCount);
    soakPrefs.putUInt("fetchFail", fetchFailCount);
    soakPrefs.putUInt("wifiRecon", wifiReconnectCount);
}

static const char *resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        default: return "OTHER";
    }
}

static void logSoakLine() {
    Serial.printf(
        "[soak] t=%lu heap=%u maxAlloc=%u bootCount=%u fetchOk=%u fetchFail=%u wifiReconnects=%u\n",
        millis(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(), bootCount, fetchOkCount, fetchFailCount,
        wifiReconnectCount);
}

static void onWifiAttempt(int attempt) {
    display.clear(display.black);
    display.drawText8x8(4, 4, "Connecting to WiFi...", display.white, display.black);
    if (attempt > 1) {
        char buf[24];
        snprintf(buf, sizeof(buf), "attempt %d", attempt);
        display.drawText8x8(4, 14, buf, display.gray, display.black);
    }
}

static void onWifiProgress() { WatchdogManager::markProgress(); }

// Non-blocking serial 'd'/'D' toggle for DEBUG - mirrors
// check_debug_toggle()/dprint() in the Python version.
static void checkDebugToggle() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 'd' || c == 'D') {
            DEBUG = !DEBUG;
            Serial.println(DEBUG ? "[debug] ON" : "[debug] OFF");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    loadSoakCounters();
    Serial.printf("=== flight-tracker-arduino booting (boot #%u, reset reason=%s) ===\n", bootCount,
                  resetReasonName(esp_reset_reason()));

    display.begin();

    WifiManager::connect(onWifiAttempt, onWifiProgress);
    Serial.print("[wifi] connected! IP: ");
    Serial.println(WiFi.localIP());

    // Needed for RouteCache's per-local-day freshness and multi-leg
    // time-window matching - see TimeSync.h. Sync completes in the
    // background; loop() logs the first successful sync for verification.
    TimeSync::begin();
    Serial.println("[time] NTP sync requested");

    WatchdogManager::begin();
    Serial.println("[watchdog] armed");
}

void loop() {
    WatchdogManager::markProgress();
    checkDebugToggle();

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

    if (!WifiManager::isConnected()) {
        wifiReconnectCount++;
        saveSoakCounters();
        WifiManager::connect(onWifiAttempt, onWifiProgress);  // dropped mid-run - reconnect
        Serial.print("[wifi] reconnected! IP: ");
        Serial.println(WiFi.localIP());
    }

    // Marks the heartbeat yellow for the whole blocking fetch+enrich
    // window below (OpenSky, then per-flight adsbdb/AeroDataBox lookups) -
    // without this the square just sits frozen at its last state for
    // however long that takes, which reads as "stuck" rather than "busy".
    Renderer::drawRefreshing(display);

    float radius = FlightMath::effectiveRadius();
    int count = OpenSkyClient::fetchNearby(radius, flights, MAX_ROWS);

    if (count < 0) {
        fetchFailCount++;
        saveSoakCounters();
        Serial.printf("[main] fetch failed: %s\n", OpenSkyClient::lastErrorMessage());
        display.clear(display.black);
        display.drawText8x8(4, 4, "Error fetching data:", display.red, display.black);
        display.drawText8x8(4, 14, OpenSkyClient::lastErrorMessage(), display.white, display.black);
        // Blink on every loop pass, not just successful ones - this exact
        // omission was a real bug in the Python version this session (the
        // error path forgot to call the heartbeat, so it silently stopped
        // blinking during runs of frequent failures).
        Renderer::renderValues(display, flights, 0);  // draws just the heartbeat (count=0)
        logSoakLine();
        WatchdogManager::delayWithProgress(OpenSkyClient::currentRefreshInterval() * 1000UL,
                                            REDRAW_SECONDS * 1000UL);
        return;
    }
    fetchOkCount++;
    saveSoakCounters();

    int missingCount = FlightSelector::findMissing(flights, count, missing, MAX_ROWS);
    WatchdogManager::markProgress();
    bool anyFlagged = FlightSelector::enrich(flights, count);
    FlightSelector::sortCommercialFirst(flights, count);
    Renderer::render(display, flights, count, missing, missingCount, anyFlagged);
    logSoakLine();

    // Between real polls, dead-reckon from each flight's last known
    // heading/speed/vertical-rate and redraw more often than we actually
    // re-fetch. current_refresh_interval() (not the REFRESH_SECONDS
    // constant directly) so a low rate-limit backs the whole cycle off.
    unsigned long elapsedS = 0;
    unsigned long targetS = OpenSkyClient::currentRefreshInterval();
    while (elapsedS < targetS) {
        delay(REDRAW_SECONDS * 1000UL);
        WatchdogManager::markProgress();
        checkDebugToggle();
        elapsedS += REDRAW_SECONDS;
        FlightMath::applyDeadReckoning(flights, count, (float)elapsedS);
        Renderer::renderValues(display, flights, count);
    }
}
