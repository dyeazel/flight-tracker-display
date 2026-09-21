#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "diag/Diag.h"
#include "diag/SerialCommands.h"
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

static void logSoakLine() {
    Serial.printf(
        "[soak] t=%lu heap=%u maxAlloc=%u bootCount=%u fetchOk=%u fetchFail=%u wifiReconnects=%u\n",
        millis(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(), Diag::bootCount(), Diag::lifetimeOk(),
        Diag::lifetimeFail(), Diag::lifetimeWifiReconnects());
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

// The scheduled self-reboot runs here, in task context, rather than in the
// watchdog's timer ISR: from the ISR only esp_restart_noos() is safe, which
// can neither record why we restarted nor drop the WiFi association cleanly
// (see Diag::reboot). The ISR keeps only the stall detector, so a wedged loop
// still gets rescued.
static void checkScheduledReboot() {
    if (millis() > SELF_REBOOT_INTERVAL_MS) Diag::reboot("scheduled");
}

// A wait that keeps the watchdog fed, keeps serial commands responsive and
// still honors the scheduled reboot.
static void waitServicing(unsigned long totalMs) {
    unsigned long elapsed = 0;
    while (elapsed < totalMs) {
        unsigned long step = min((unsigned long)REDRAW_SECONDS * 1000UL, totalMs - elapsed);
        delay(step);
        WatchdogManager::markProgress();
        SerialCommands::update();
        checkScheduledReboot();
        elapsed += step;
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Diag::begin();
    Serial.printf("=== flight-tracker-arduino booting (boot #%u, reason=%s) ===\n", Diag::bootCount(),
                  Diag::bootReason());

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
    SerialCommands::update();
    checkScheduledReboot();

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
        Diag::noteWifiReconnect();
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
        Diag::noteCycle(false);
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
        waitServicing(OpenSkyClient::currentRefreshInterval() * 1000UL);
        return;
    }
    Diag::noteCycle(true);

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
        SerialCommands::update();
        checkScheduledReboot();
        elapsedS += REDRAW_SECONDS;
        FlightMath::applyDeadReckoning(flights, count, (float)elapsedS);
        Renderer::renderValues(display, flights, count);
    }
}
