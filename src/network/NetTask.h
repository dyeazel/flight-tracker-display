#pragma once

#include <stdint.h>

#include "flight/Flight.h"

// All networking (WiFi connect/reconnect, the OpenSky fetch, FlightSelector::enrich()'s adsbdb/AeroDataBox/
// track lookups) runs here, in its own FreeRTOS task, so a slow or stuck call can never freeze the display
// or serial commands - a real case of this was measured live: the 2026-09-21 soak caught a single OpenSky
// token fetch taking 120223ms despite a 15000ms configured timeout. Pattern from ../mlb-board's NetTask.
//
// Results are published under a mutex for the UI task to copy out each redraw (see main.cpp) - small,
// fixed-size data (MAX_ROWS Flights), copied under a lock held only for the copy itself, so contention
// with the rare (every REFRESH_SECONDS-ish) publish is not a concern. NVS/Preferences calls (Diag,
// RouteCache's budget counter) are NOT similarly guarded - the underlying nvs_flash component is
// documented thread-safe, and both ../mlb-board's NetTask and this one rely on that rather than adding a
// second lock.
//
// The UI task must never touch TFT_eSPI/the display from here - only main.cpp's loop() may.
namespace NetTask {

// Call once from setup(), after Diag::begin(). Creates the mutex and starts the task pinned to core 0
// (the UI loop task defaults to core 1), so the two never contend for CPU time either.
void begin();

// Milliseconds since the top of this task's own outer loop iteration - NOT since its last successful
// fetch (see msSinceLastOk() in Diag, which already tracks that). Only advances between iterations, so a
// call stuck inside HTTPClient/WiFiClientSecure/WiFi.begin() correctly makes this grow without bound
// until it returns - the basis of Health's "net stuck" check.
uint32_t msSinceLoop();

// True only while a real fetch+enrich cycle is in progress right now (not during the idle wait between
// polls) - drives the UI's yellow "refreshing" heartbeat from outside the task doing the work.
bool isPolling();

// The flight count from the most recent publish (0 if none yet) - used by Health to defer the scheduled
// reboot while flights are actually on screen.
int lastPublishedCount();

// Copies the latest published snapshot: up to maxOut Flights into outFlights (*outCount set to however
// many), up to maxMissing callsigns into outMissing (*outMissingCount set likewise), *outAnyFlagged, and
// *outFixMs (millis() at the real fetch this snapshot reflects - compare against a previous call's value
// to tell whether the data actually changed). Returns false, touching nothing, if nothing has ever been
// published (still connecting/loading).
bool getFlights(Flight *outFlights, int maxOut, int *outCount, char outMissing[][12], int maxMissing,
                int *outMissingCount, bool *outAnyFlagged, uint32_t *outFixMs);

// Minimum free stack (bytes) seen so far on this task, for diagnostics (see NET_TASK_STACK_BYTES).
uint32_t stackFreeMin();

}  // namespace NetTask
