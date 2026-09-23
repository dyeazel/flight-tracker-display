#include "NetTask.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

#include "config.h"
#include "diag/Diag.h"
#include "flight/FlightMath.h"
#include "flight/FlightSelector.h"
#include "network/OpenSkyClient.h"
#include "network/WifiManager.h"
#include "time/TimeSync.h"

namespace NetTask {

namespace {

TaskHandle_t gTask = nullptr;
volatile uint32_t gLoopMs = 0;
volatile bool gPolling = false;

// Guards every gPublished* variable below - the only state this task and the UI task both touch.
SemaphoreHandle_t gMux = nullptr;
struct Lock {
    Lock() { xSemaphoreTake(gMux, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(gMux); }
};

Flight gPublished[MAX_ROWS];
int gPublishedCount = 0;
char gPublishedMissing[MAX_ROWS][12];
int gPublishedMissingCount = 0;
bool gPublishedAnyFlagged = false;
uint32_t gPublishedFixMs = 0;
bool gEverPublished = false;

// This task's own working copies - never touched by the UI task, so no lock needed.
Flight gWorking[MAX_ROWS];
char gWorkingMissing[MAX_ROWS][12];
uint32_t gNextPollMs = 0;

void publish(Flight *flights, int count, char missing[][12], int missingCount, bool anyFlagged) {
    Lock lock;
    const int n = count < MAX_ROWS ? count : MAX_ROWS;
    for (int i = 0; i < n; i++) gPublished[i] = flights[i];
    gPublishedCount = n;
    const int nm = missingCount < MAX_ROWS ? missingCount : MAX_ROWS;
    for (int i = 0; i < nm; i++) memcpy(gPublishedMissing[i], missing[i], sizeof(gPublishedMissing[i]));
    gPublishedMissingCount = nm;
    gPublishedAnyFlagged = anyFlagged;
    gPublishedFixMs = millis();
    gEverPublished = true;
}

// Fetch + enrich + publish, if REFRESH_SECONDS (or the rate-limit-backed-off interval) has elapsed since
// the last attempt. Returns quickly otherwise - this is what taskMain() calls every ~100ms.
void pollIfDue() {
    if ((int32_t)(millis() - gNextPollMs) < 0) return;
    gPolling = true;

    const float radius = FlightMath::effectiveRadius();
    const int count = OpenSkyClient::fetchNearby(radius, gWorking, MAX_ROWS);
    if (count < 0) {
        Diag::noteCycle(false);
        Serial.printf("[main] fetch failed: %s\n", OpenSkyClient::lastErrorMessage());
        gNextPollMs = millis() + OpenSkyClient::currentRefreshInterval() * 1000UL;
        gPolling = false;
        return;
    }
    Diag::noteCycle(true);

    const int missingCount = FlightSelector::findMissing(gWorking, count, gWorkingMissing, MAX_ROWS);
    const bool anyFlagged = FlightSelector::enrich(gWorking, count);
    FlightSelector::sortCommercialFirst(gWorking, count);
    publish(gWorking, count, gWorkingMissing, missingCount, anyFlagged);

    gNextPollMs = millis() + OpenSkyClient::currentRefreshInterval() * 1000UL;
    gPolling = false;
}

void taskMain(void *) {
    bool everConnected = false;
    for (;;) {
        // gLoopMs refreshes every tick regardless of WiFi state now that WifiManager::update() is
        // non-blocking (see network/WifiManager.h) - no separate progress-callback hack needed the way
        // a blocking connect() used to require.
        gLoopMs = millis();
        WifiManager::update();
        TimeSync::update(WifiManager::isConnected());
        if (WifiManager::takeJustConnected()) {
            if (everConnected) {
                Diag::noteWifiReconnect();
                Serial.print("[wifi] reconnected! IP: ");
            } else {
                Serial.print("[wifi] connected! IP: ");
            }
            Serial.println(WiFi.localIP());
            everConnected = true;
        }
        if (WifiManager::isConnected()) pollIfDue();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}  // namespace

void begin() {
    gMux = xSemaphoreCreateMutex();
    gLoopMs = millis();
    // Core 0 (with the WiFi/lwIP tasks), leaving core 1 to the UI loop task.
    xTaskCreatePinnedToCore(taskMain, "net", NET_TASK_STACK_BYTES, nullptr, 1, &gTask, 0);
}

uint32_t msSinceLoop() { return millis() - gLoopMs; }
bool isPolling() { return gPolling; }

int lastPublishedCount() {
    Lock lock;
    return gEverPublished ? gPublishedCount : 0;
}

bool getFlights(Flight *outFlights, int maxOut, int *outCount, char outMissing[][12], int maxMissing,
                int *outMissingCount, bool *outAnyFlagged, uint32_t *outFixMs) {
    Lock lock;
    if (!gEverPublished) return false;
    const int n = gPublishedCount < maxOut ? gPublishedCount : maxOut;
    for (int i = 0; i < n; i++) outFlights[i] = gPublished[i];
    *outCount = n;
    const int nm = gPublishedMissingCount < maxMissing ? gPublishedMissingCount : maxMissing;
    for (int i = 0; i < nm; i++) memcpy(outMissing[i], gPublishedMissing[i], 12);
    *outMissingCount = nm;
    *outAnyFlagged = gPublishedAnyFlagged;
    *outFixMs = gPublishedFixMs;
    return true;
}

uint32_t stackFreeMin() { return gTask ? uxTaskGetStackHighWaterMark(gTask) * sizeof(StackType_t) : 0; }

}  // namespace NetTask
