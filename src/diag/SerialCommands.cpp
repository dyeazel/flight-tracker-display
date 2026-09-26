#include "SerialCommands.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "config/CallsignList.h"
#include "config/ConfigStore.h"
#include "diag/Diag.h"
#include "diag/NetStats.h"
#include "flight/FlightMath.h"
#include "flight/FlightSelector.h"
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

// Blocking scan (a few seconds; the UI redraw pauses briefly, which is fine while someone is running the
// setup page) - mirrors ../../mlb-board's SetupSerial::cmdScan (its own comment: the async scan API proved
// unreliable on this core).
constexpr int kMaxNetworks = 12;
void cmdScan() {
    WifiManager::pause();  // a scan fails while a connection attempt is in flight
    WiFi.scanDelete();
    const int n = WiFi.scanNetworks(false /*async*/, false /*hidden*/, false /*passive*/, 150 /*ms per channel*/);
    WifiManager::resume();
    if (n < 0) {
        WiFi.scanDelete();
        return replyError("scan", "scan_failed");
    }
    JsonDocument d;
    d["ok"] = true;
    d["cmd"] = "scan";
    JsonArray arr = d["networks"].to<JsonArray>();
    bool used[64] = {false};
    const int cap = n < 64 ? n : 64;
    for (int count = 0; count < kMaxNetworks; count++) {
        int best = -1;
        for (int i = 0; i < cap; i++)
            if (!used[i] && WiFi.SSID(i).length() > 0 && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
        if (best < 0) break;
        used[best] = true;
        const String ssid = WiFi.SSID(best);
        bool dup = false;
        for (JsonObject o : arr)
            if (ssid == o["ssid"].as<const char *>()) dup = true;
        if (dup) {
            count--;
            continue;
        }
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = ssid;
        o["rssi"] = WiFi.RSSI(best);
        o["secure"] = WiFi.encryptionType(best) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    reply(d);
}

// Reads "callsigns" ([[callsign, origin, destination], ...], 0-CallsignList::kMax entries; origin/
// destination are each "" or omitted for an unpinned field, mirroring ../../mlb-board's [league, id]
// positional-array precedent) into out. Returns nullptr on success, else the error to report.
const char *parseCallsigns(JsonDocument &in, CallsignList::Entry *out, int *count) {
    *count = 0;
    if (!in["callsigns"].is<JsonArray>()) return nullptr;  // omitted = no tracked callsigns
    JsonArray arr = in["callsigns"].as<JsonArray>();
    if (arr.size() > (size_t)CallsignList::kMax) return "too_many_callsigns";
    for (JsonVariant v : arr) {
        if (!v.is<JsonArray>()) return "bad_json";
        JsonArray e = v.as<JsonArray>();
        if (e.size() < 1 || e.size() > 3 || !e[0].is<const char *>()) return "bad_json";
        const char *cs = e[0].as<const char *>();
        // Reject outright rather than silently truncating - a truncated callsign would validate fine
        // (still uppercase alnum, still <= kCallsignChars) and get saved as something the caller never
        // asked for.
        if (strlen(cs) > (size_t)CallsignList::kCallsignChars) return "bad_callsign";
        const char *origin = (e.size() > 1 && e[1].is<const char *>()) ? e[1].as<const char *>() : "";
        const char *dest = (e.size() > 2 && e[2].is<const char *>()) ? e[2].as<const char *>() : "";
        if (strlen(origin) > (size_t)CallsignList::kCodeChars) return "bad_origin";
        if (strlen(dest) > (size_t)CallsignList::kCodeChars) return "bad_destination";
        strcpy(out[*count].callsign, cs);
        strcpy(out[*count].origin, origin);
        strcpy(out[*count].destination, dest);
        (*count)++;
    }
    return nullptr;
}

void cmdSet(JsonDocument &in) {
    if (!in["ssid"].is<const char *>() || in["lat"].isNull() || in["lon"].isNull()) {
        return replyError("set", "bad_json");
    }
    CallsignList::Entry targets[CallsignList::kMax];
    int targetCount;
    if (const char *e = parseCallsigns(in, targets, &targetCount)) return replyError("set", e);
    const char *pass = in["pass"].is<const char *>() ? in["pass"].as<const char *>() : nullptr;
    const float radiusMi = in["radius_mi"].isNull() ? RADIUS_MI_AUTO : in["radius_mi"].as<float>();
    const char *err = ConfigStore::save(in["ssid"].as<const char *>(), pass, in["lat"].as<float>(),
                                         in["lon"].as<float>(), radiusMi, targets, targetCount);
    if (err) return replyError("set", err);
    WifiManager::reconfigure();  // reconnect with the new credentials, no reboot needed
    return replyOk("set");
}

void handleCommand(JsonDocument &in, const char *cmd) {
    if (!strcmp(cmd, "hello")) {
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "hello";
        d["fw"] = "flight-tracker-arduino " __DATE__;
        d["board"] = "cyd";
        d["proto"] = 2;  // 2 = runtime WiFi/location/callsign config (Phase 7); 1 = compile-time only
        d["maxCallsigns"] = CallsignList::kMax;
        return reply(d);
    }
    if (!strcmp(cmd, "get")) {
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "get";
        d["debug"] = DEBUG;
        d["radius_mi"] = FlightMath::effectiveRadius(ConfigStore::radiusOverrideMi(), ConfigStore::callsignCount());
        d["refresh_s"] = REFRESH_SECONDS;
        d["wifi"] = WifiManager::isConnected();  // not a raw WiFi.status() check - see Diag::fillDiag's comment
        d["wifi_state"] = WifiManager::stateName();
        d["failure"] = WifiManager::lastFailure();
        d["ip"] = WiFi.localIP().toString();
        d["time_synced"] = TimeSync::isSynced();
        d["ssid"] = ConfigStore::ssid();
        d["hasPass"] = ConfigStore::hasPass();
        d["lat"] = ConfigStore::lat();
        d["lon"] = ConfigStore::lon();
        JsonArray cs = d["callsigns"].to<JsonArray>();
        for (int i = 0; i < ConfigStore::callsignCount(); i++) {
            JsonArray e = cs.add<JsonArray>();
            e.add(ConfigStore::callsign(i));
            e.add(ConfigStore::origin(i));
            e.add(ConfigStore::destination(i));
        }
        return reply(d);
    }
    if (!strcmp(cmd, "set")) return cmdSet(in);
    if (!strcmp(cmd, "scan")) return cmdScan();
    if (!strcmp(cmd, "forget")) {
        ConfigStore::forgetWifi();
        WifiManager::reconfigure();
        return replyOk("forget");
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
        // Phase 8: why each tracked-but-absent flight isn't showing - lets a bench session verify
        // classifyMissing() without needing to watch the physical panel.
        Flight diagFlights[MAX_ROWS];
        char diagMissing[MAX_ROWS][12];
        FlightSelector::MissingStatus diagMissingStatus[MAX_ROWS];
        time_t diagMissingWhen[MAX_ROWS];
        int diagFlightCount = 0, diagMissingCount = 0;
        bool diagAnyFlagged = false;
        uint32_t diagFixMs = 0;
        if (NetTask::getFlights(diagFlights, MAX_ROWS, &diagFlightCount, diagMissing, diagMissingStatus,
                                 diagMissingWhen, MAX_ROWS, &diagMissingCount, &diagAnyFlagged, &diagFixMs)) {
            JsonArray missing = d["missing"].to<JsonArray>();
            for (int i = 0; i < diagMissingCount; i++) {
                JsonArray e = missing.add<JsonArray>();
                e.add(diagMissing[i]);
                e.add(FlightSelector::missingStatusName(diagMissingStatus[i]));
                e.add((uint32_t)diagMissingWhen[i]);
            }
        }
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
