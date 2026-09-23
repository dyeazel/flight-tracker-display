#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

// Per-network-source timing/counting - the part of this app's diagnostics that's specific to flight-
// tracker's own set of network calls (OpenSky token/states/track, adsbdb, AeroDataBox), so it stays
// local rather than moving to ../../cyd-common's shared Diag module along with the generic restart/
// poll-counter machinery. Reopens the `Diag` namespace (see diag/Diag.h) rather than introducing a
// second name, since call sites already spell this Diag::FetchTimer/Diag::Source.
namespace Diag {

// The network endpoints we time and count individually.
enum class Source : uint8_t { Token, States, Track, Adsbdb, AeroDataBox, kCount };

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

// Adds this app's own extra fields to the serial "diag" reply, on top of the shared Diag::fillDiag()
// base fields: per-source "sources" timing/counts and, if any call has ever failed, "last_failure"/
// "last_failure_ago_s". Call right after Diag::fillDiag(d).
void fillNetworkDiag(JsonDocument &d);

#ifdef TEST_HOOKS
void testSetBlockNet(bool on);
void testSetSlowNetMs(uint32_t ms);
void testResetNetworkStats();  // clears gStats/last_failure - Diag::testResetCounters() clears the rest
#endif

}  // namespace Diag
