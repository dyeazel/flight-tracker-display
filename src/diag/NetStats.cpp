#include "NetStats.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"

namespace Diag {

namespace {

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

}  // namespace

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

void fillNetworkDiag(JsonDocument &d) {
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

#ifdef TEST_HOOKS
void testSetBlockNet(bool on) { gBlockNet = on; }
void testSetSlowNetMs(uint32_t ms) { gSlowNetMs = ms; }
void testResetNetworkStats() {
    for (auto &s : gStats) s = SourceStats();
    gLastFailure[0] = '\0';
    gHaveFailure = false;
}
#endif

}  // namespace Diag
