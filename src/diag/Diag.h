#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

// Diagnostics: why the board last restarted, lifetime/this-boot counters, and per-source network timing -
// the raw material for judging long unattended runs (see tools/soak_logger.py). Field names in
// fillDiag() deliberately match ../mlb-board's Health::fillDiag so the same tools work on both apps.
//
// Persistence (NVS): namespace "soak" keeps the original lifetime counters (bootCount/fetchOk/fetchFail/
// wifiRecon - unchanged keys, so history carries over); namespace "diag" keeps the reason for a restart we
// requested plus a per-reason restart count. RTC memory was tried earlier and does not survive
// esp_restart_noos() on this framework build.
namespace Diag {

// The network endpoints we time and count individually.
enum class Source : uint8_t { Token, States, Track, Adsbdb, AeroDataBox, kCount };

// Call once after Serial.begin(): loads counters, classifies why we booted (a reason we recorded before
// restarting, else inferred from the chip's reset cause), bumps bootCount.
void begin();

const char *bootReason();

// Restarts cleanly from task context: records `why` in NVS, drops the WiFi association so the router
// forgets us, then ESP.restart(). Never returns. The stall watchdog ISR cannot do this (it must use
// esp_restart_noos, which cannot write NVS) - a boot with no recorded reason after a software reset is
// therefore classified "stall watchdog".
[[noreturn]] void reboot(const char *why);

// One main-loop cycle's outcome (the OpenSky fetch that drives the screen). Feeds the lifetime counters.
void noteCycle(bool ok);
void noteWifiReconnect();

unsigned lifetimeOk();
unsigned lifetimeFail();
unsigned lifetimeWifiReconnects();
unsigned bootCount();

// Fills the reply for the serial "diag" command.
void fillDiag(JsonDocument &d);

// Times and counts one network call (a single attempt, not a retry loop). Typical use:
//   Diag::FetchTimer t(Diag::Source::Adsbdb, "api.adsbdb.com");
//   if (!t.gate()) return -1;
//   t.dns();
//   ... http.begin(); int code = http.GET(); t.gotResponse();
//   ... read/parse the body ...
//   t.finish(ok, code, "http");
// A [net-t] line is logged when the call fails or is slow (SLOW_FETCH_LOG_MS), never for a fast success.
// If finish() is never reached (forgotten early return) the destructor records a failure ("unfinished").
class FetchTimer {
public:
    FetchTimer(Source s, const char *host);
    ~FetchTimer();
    FetchTimer(const FetchTimer &) = delete;
    FetchTimer &operator=(const FetchTimer &) = delete;

    // Bench-test hook (TEST_HOOKS builds only; always true otherwise): may refuse the call ("network
    // unreachable") or stall it, to exercise the watchdog and failure handling on real hardware.
    bool gate();

    void dns();           // resolves the host once, timing it (result is cached by lwIP for the real call)
    void gotResponse();   // marks the end of connect + TLS + request + headers (i.e. http.GET() returned)
    void finish(bool ok, int httpCode, const char *reason);

private:
    Source src_;
    const char *host_;
    uint32_t t0_, tDns_, tGet_;
    bool dnsOk_ = true;
    bool haveDns_ = false;
    bool haveGet_ = false;
    bool done_ = false;
};

#ifdef TEST_HOOKS
void testSetBlockNet(bool on);
void testSetSlowNetMs(uint32_t ms);
void testResetCounters();
#endif

}  // namespace Diag
