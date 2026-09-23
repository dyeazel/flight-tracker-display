#include "SerialCommands.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "diag/Diag.h"
#include "diag/NetStats.h"
#include "flight/FlightMath.h"
#include "network/AdsbdbClient.h"
#include "network/RouteCache.h"
#include "network/WifiManager.h"
#include "serial/SerialCore.h"
#include "time/TimeSync.h"
#ifndef SIM_DATA
#include "network/NetTask.h"
#endif

#ifdef SCREEN_CAPTURE
#include "display/DisplayManager.h"
extern DisplayManager display;  // defined in main.cpp
#endif
#ifdef SIM_DATA
#include "flight/SimFlights.h"
#endif

using SerialCore::reply;
using SerialCore::replyError;
using SerialCore::replyOk;

namespace SerialCommands {

namespace {

void handleCommand(JsonDocument &in, const char *cmd) {
    if (!strcmp(cmd, "hello")) {
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "hello";
        d["fw"] = "flight-tracker-arduino " __DATE__;
        d["board"] = "cyd";
        d["proto"] = 1;
        return reply(d);
    }
    if (!strcmp(cmd, "get")) {
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "get";
        d["debug"] = DEBUG;
        d["radius_mi"] = FlightMath::effectiveRadius();
        d["refresh_s"] = REFRESH_SECONDS;
        d["wifi"] = WifiManager::isConnected();  // not a raw WiFi.status() check - see Diag::fillDiag's comment
        d["ip"] = WiFi.localIP().toString();
        d["time_synced"] = TimeSync::isSynced();
        return reply(d);
    }
    if (!strcmp(cmd, "diag")) {
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "diag";
        Diag::fillDiag(d);              // shared base fields (../../cyd-common)
        Diag::fillNetworkDiag(d);       // this app's per-source network timing (sources/last_failure)
        d["debug"] = DEBUG;
        d["wifi_reconnects"] = Diag::lifetimeWifiReconnects();
        // Visibility for RouteCache's tightly-tracked AeroDataBox budget (config.h's
        // ROUTE_PROVIDER_MONTHLY_BUDGET) - added after Phase 6's "measure first" review found no real
        // usage this session to justify LittleFS-persisting the route cache itself; this at least lets
        // a future session check actual spend before deciding whether that's still true.
        d["aerodatabox_budget_used"] = RouteCache::budgetUsedThisMonth();
        d["aerodatabox_budget_month"] = RouteCache::budgetMonthNumber();
#ifndef SIM_DATA
        d["net_task_ms_since_loop"] = NetTask::msSinceLoop();
        d["net_task_stack_free_min"] = NetTask::stackFreeMin();
        d["net_task_polling"] = NetTask::isPolling();
        d["flights_shown"] = NetTask::lastPublishedCount();
#endif
        return reply(d);
    }
    if (!strcmp(cmd, "debug")) {
        if (in["on"].is<bool>()) DEBUG = in["on"].as<bool>();
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "debug";
        d["on"] = DEBUG;
        return reply(d);
    }
    if (!strcmp(cmd, "reboot")) {
        replyOk("reboot");
        Serial.flush();
        Diag::reboot("user");
    }
#ifdef TEST_HOOKS
    if (!strcmp(cmd, "_hang")) {
        replyOk("_hang");
        Serial.flush();
        for (;;) {
        }  // never calls WatchdogManager::markProgress(): the stall ISR must rescue us
    }
    if (!strcmp(cmd, "_blocknet")) {
        Diag::testSetBlockNet(in["on"] | true);
        return replyOk("_blocknet");
    }
    if (!strcmp(cmd, "_slownet")) {
        Diag::testSetSlowNetMs(in["ms"] | 0);
        return replyOk("_slownet");
    }
    if (!strcmp(cmd, "_wifidown")) {
        WifiManager::testForceDown(in["on"] | true);
        return replyOk("_wifidown");
    }
    if (!strcmp(cmd, "_resetdiag")) {
        Diag::testResetCounters();
        Diag::testResetNetworkStats();
        return replyOk("_resetdiag");
    }
#ifdef SIM_DATA
    if (!strcmp(cmd, "_sim")) {  // {"cmd":"_sim","n":3} holds scene 3; n < 0 resumes cycling. Always reports count/scene.
        SimFlights::hold(in["n"] | -1);
        const int cur = SimFlights::current(millis());
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "_sim";
        d["count"] = SimFlights::count();
        d["scene"] = cur;
        d["name"] = SimFlights::name(cur);
        return reply(d);
    }
#endif
#ifdef SCREEN_CAPTURE
    if (!strcmp(cmd, "_shot")) {  // dumps the panel as "#S x y w h <hex RGB565 LE>" strips; see tools/screenshot.py
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "_shot";
        d["w"] = display.width();
        d["h"] = display.height();
        reply(d);
        Serial.flush();
        display.dumpScreen();
        return;
    }
#endif
    // {"cmd":"_lookup","callsign":"ZZZ9999"}: one adsbdb lookup, incl. its retry policy. Blocks THIS (UI)
    // task, and races NetTask's own AdsbdbClient calls for its file-static scratch buffers (url, etc.) if
    // one happens to land mid-call - acceptable for a manual bench command (worst case: a garbled reply,
    // obviously wrong, just retry), not worth a cross-task request queue for this rare, deliberate use.
    if (!strcmp(cmd, "_lookup")) {
        AdsbdbClient::RouteInfo info;
        const bool found = AdsbdbClient::lookupRoute(in["callsign"] | "", &info);
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "_lookup";
        d["found"] = found;
        d["origin"] = info.originCode;
        d["destination"] = info.destinationCode;
        return reply(d);
    }
#endif
    replyError(cmd, "unknown_cmd");
}

}  // namespace

void update() { SerialCore::update(handleCommand); }

}  // namespace SerialCommands
