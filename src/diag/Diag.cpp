#include "Diag.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>

#include "config.h"

namespace Diag {

namespace {

// Restart reasons we count. "scheduled"/"user" are recorded before we restart; "stall watchdog" is what a
// software reset with no recorded reason means (only the stall ISR restarts without writing one).
const char *const kClasses[] = {"power on", "stall watchdog", "scheduled", "user",
                                "panic",    "hw watchdog",    "brownout",  "other"};
constexpr size_t kNumClasses = sizeof(kClasses) / sizeof(kClasses[0]);

char gBootReason[20] = "other";
uint32_t gRestartCounts[kNumClasses] = {0};

Preferences gSoak;  // namespace "soak", kept open: written every cycle
bool gSoakOpen = false;
uint32_t gBoots = 0, gLifeOk = 0, gLifeFail = 0, gLifeWifi = 0;
uint32_t gBootOk = 0, gBootFail = 0, gConsecFails = 0;
uint32_t gLastOkMs = 0;

const char *const kSourceNames[] = {"token", "states", "track", "adsbdb", "aerodatabox"};
struct SourceStats {
    uint32_t ok = 0, fail = 0, lastMs = 0, maxMs = 0;
};
SourceStats gStats[(size_t)Source::kCount];
char gLastFailure[112] = "";
uint32_t gLastFailureMs = 0;
bool gHaveFailure = false;

#ifdef TEST_HOOKS
bool gBlockNet = false;
uint32_t gSlowNetMs = 0;
#endif

size_t classOf(const char *why) {
    for (size_t i = 0; i < kNumClasses; i++)
        if (!strcmp(why, kClasses[i])) return i;
    return kNumClasses - 1;  // "other"
}

void loadRestartReason() {
    Preferences p;
    if (!p.begin("diag", false)) {
        Serial.println("[diag] NVS open failed");
        return;
    }
    for (size_t i = 0; i < kNumClasses; i++) {
        char k[6];
        snprintf(k, sizeof(k), "c%u", (unsigned)i);
        gRestartCounts[i] = p.getUInt(k, 0);
    }
    // A restart we requested left its reason behind; otherwise infer it from the chip's reset cause.
    // (getString/remove on a missing key log NVS errors, hence the isKey guard.)
    String why;
    if (p.isKey("why")) {
        why = p.getString("why", "");
        p.remove("why");
    }
    if (why.length() == 0) {
        switch (esp_reset_reason()) {
            case ESP_RST_POWERON: why = "power on"; break;
            case ESP_RST_SW: why = "stall watchdog"; break;
            case ESP_RST_PANIC: why = "panic"; break;
            case ESP_RST_INT_WDT:
            case ESP_RST_TASK_WDT:
            case ESP_RST_WDT: why = "hw watchdog"; break;
            case ESP_RST_BROWNOUT: why = "brownout"; break;
            default: why = "other"; break;
        }
    }
    strncpy(gBootReason, why.c_str(), sizeof(gBootReason) - 1);
    const size_t cls = classOf(gBootReason);
    gRestartCounts[cls]++;
    char k[6];
    snprintf(k, sizeof(k), "c%u", (unsigned)cls);
    p.putUInt(k, gRestartCounts[cls]);
    p.end();
}

}  // namespace

void begin() {
    gSoakOpen = gSoak.begin("soak", false);
    if (gSoakOpen) {
        gBoots = gSoak.getUInt("bootCount", 0) + 1;
        gLifeOk = gSoak.getUInt("fetchOk", 0);
        gLifeFail = gSoak.getUInt("fetchFail", 0);
        gLifeWifi = gSoak.getUInt("wifiRecon", 0);
        gSoak.putUInt("bootCount", gBoots);
    }
    loadRestartReason();
    Serial.printf("[diag] boot #%u reason='%s'\n", (unsigned)gBoots, gBootReason);
}

const char *bootReason() { return gBootReason; }

[[noreturn]] void reboot(const char *why) {
    Serial.printf("[diag] restarting: %s\n", why);
    {
        Preferences p;
        if (p.begin("diag", false)) {
            p.putString("why", why);
            p.end();
        }
    }
    Serial.flush();
    WiFi.disconnect(true);  // deauth so the router drops the association cleanly
    delay(200);
    ESP.restart();
    for (;;) delay(1000);  // not reached
}

void noteCycle(bool ok) {
    if (ok) {
        gLifeOk++;
        gBootOk++;
        gConsecFails = 0;
        gLastOkMs = millis();
        if (gSoakOpen) gSoak.putUInt("fetchOk", gLifeOk);
    } else {
        gLifeFail++;
        gBootFail++;
        gConsecFails++;
        if (gSoakOpen) gSoak.putUInt("fetchFail", gLifeFail);
    }
}

void noteWifiReconnect() {
    gLifeWifi++;
    if (gSoakOpen) gSoak.putUInt("wifiRecon", gLifeWifi);
}

unsigned lifetimeOk() { return gLifeOk; }
unsigned lifetimeFail() { return gLifeFail; }
unsigned lifetimeWifiReconnects() { return gLifeWifi; }
unsigned bootCount() { return gBoots; }

void fillDiag(JsonDocument &d) {
    d["uptime_s"] = millis() / 1000;
    d["boot_reason"] = gBootReason;
    d["boots"] = gBoots;
    JsonObject r = d["restarts"].to<JsonObject>();
    for (size_t i = 0; i < kNumClasses; i++) r[kClasses[i]] = gRestartCounts[i];
    d["heap_free"] = ESP.getFreeHeap();
    d["heap_max_alloc"] = ESP.getMaxAllocHeap();
    d["heap_min_free"] = ESP.getMinFreeHeap();
    d["poll_ok"] = gLifeOk;
    d["poll_fail"] = gLifeFail;
    d["poll_ok_this_boot"] = gBootOk;
    d["poll_fail_this_boot"] = gBootFail;
    d["poll_consecutive_fails"] = gConsecFails;
    d["last_ok_ago_s"] = gBootOk ? (millis() - gLastOkMs) / 1000 : -1;
    d["wifi_reconnects"] = gLifeWifi;
    d["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    d["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    d["debug"] = DEBUG;
    JsonObject s = d["sources"].to<JsonObject>();
    for (size_t i = 0; i < (size_t)Source::kCount; i++) {
        JsonObject o = s[kSourceNames[i]].to<JsonObject>();
        o["ok"] = gStats[i].ok;
        o["fail"] = gStats[i].fail;
        o["last_ms"] = gStats[i].lastMs;
        o["max_ms"] = gStats[i].maxMs;
    }
    if (gHaveFailure) {
        d["last_failure"] = gLastFailure;
        d["last_failure_ago_s"] = (millis() - gLastFailureMs) / 1000;
    }
}

FetchTimer::FetchTimer(Source s, const char *host) : src_(s), host_(host) {
    t0_ = tDns_ = tGet_ = millis();
}

FetchTimer::~FetchTimer() {
    if (!done_) finish(false, -2, "unfinished");
}

bool FetchTimer::gate() {
#ifdef TEST_HOOKS
    if (gBlockNet) {
        finish(false, -1, "blocked");
        return false;
    }
    if (gSlowNetMs) delay(gSlowNetMs);  // deliberately no progress marking: mimics a call stuck in the TLS stack
#endif
    return true;
}

void FetchTimer::dns() {
    IPAddress ip;
    dnsOk_ = WiFi.hostByName(host_, ip);
    haveDns_ = true;
    tDns_ = millis();
}

void FetchTimer::gotResponse() {
    tGet_ = millis();
    haveGet_ = true;
}

void FetchTimer::finish(bool ok, int httpCode, const char *reason) {
    if (done_) return;
    done_ = true;
    const uint32_t tEnd = millis();
    const uint32_t total = tEnd - t0_;
    const uint32_t dnsMs = haveDns_ ? tDns_ - t0_ : 0;
    const uint32_t getMs = haveGet_ ? tGet_ - (haveDns_ ? tDns_ : t0_) : 0;
    const uint32_t bodyMs = haveGet_ ? tEnd - tGet_ : 0;

    SourceStats &st = gStats[(size_t)src_];
    if (ok) st.ok++;
    else st.fail++;
    st.lastMs = total;
    if (total > st.maxMs) st.maxMs = total;

    if (!ok || total >= SLOW_FETCH_LOG_MS) {
        Serial.printf("[net-t] %s %s http=%d reason=%s dns=%s/%ums get=%ums body=%ums total=%ums rssi=%d up=%lus\n",
                      kSourceNames[(size_t)src_], ok ? "slow" : "FAIL", httpCode, ok ? "ok" : (reason ? reason : "-"),
                      dnsOk_ ? "ok" : "FAIL", (unsigned)dnsMs, (unsigned)getMs, (unsigned)bodyMs, (unsigned)total,
                      WiFi.RSSI(), (unsigned long)(tEnd / 1000));
    }
    if (!ok) {
        snprintf(gLastFailure, sizeof(gLastFailure), "%s %s http=%d dns=%s total=%ums", kSourceNames[(size_t)src_],
                 reason ? reason : "-", httpCode, dnsOk_ ? "ok" : "FAIL", (unsigned)total);
        gLastFailureMs = tEnd;
        gHaveFailure = true;
    }
}

#ifdef TEST_HOOKS
void testSetBlockNet(bool on) { gBlockNet = on; }
void testSetSlowNetMs(uint32_t ms) { gSlowNetMs = ms; }
void testResetCounters() {
    {
        Preferences p;
        if (p.begin("diag", false)) {
            p.clear();
            p.end();
        }
    }
    for (auto &c : gRestartCounts) c = 0;
    gBoots = 1;
    gLifeOk = gLifeFail = gLifeWifi = 0;
    gBootOk = gBootFail = gConsecFails = 0;
    if (gSoakOpen) {
        gSoak.putUInt("bootCount", gBoots);
        gSoak.putUInt("fetchOk", 0);
        gSoak.putUInt("fetchFail", 0);
        gSoak.putUInt("wifiRecon", 0);
    }
    Serial.println("[diag] counters reset (test hook)");
}
#endif

}  // namespace Diag
