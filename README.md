# flight-tracker-arduino

Arduino/C++ (PlatformIO) port of `/Users/dean/Projects/Python/flight-tracker/cyd_nearby_flights.py`,
the MicroPython version of this same app. Same ESP32 "Cheap Yellow Display"
board (ESP32-2432S028R), same OpenSky/adsbdb data sources, same on-screen
layout. Ported to get deterministic (non-GC) memory management after the
MicroPython version showed apparent heap-fragmentation-driven TLS/OOM
failures degrading over hours of uptime.

The MicroPython version is untouched and still flashable - this is a
from-scratch rewrite in a separate project, not a replacement in place.

## Status

All milestones (0-7) are done and verified on real hardware: toolchain,
WiFi, display, the authenticated OpenSky call, the full data pipeline,
rendering, watchdog hardening, and a soak test.

**Milestone 7 (soak test) results**: ~9h44m total (6:36 AM-4:19 PM),
spanning one full scheduled 6h self-reboot cycle. Combined across both
segments: 766 successful fetches, 1 transient TLS connect failure
(handled correctly - error screen shown, backed off, recovered next
cycle), 0 WiFi reconnects needed. **`ESP.getMaxAllocHeap()` (the real
fragmentation signal) was identical - 110,580 bytes - at every single
reading in both segments, for the entire run.** That's the core question
this migration exists to answer, and the answer is a clean flat line, not
the degradation-over-uptime the MicroPython version showed. See "What
Milestone 7 watched for" below for what a longer/future run should keep
checking.

The soak run also surfaced a real bug - the RTC-memory-based counters
this instrumentation originally used didn't survive the scheduled
self-reboot - since fixed. See "RTC memory doesn't survive
`esp_restart_noos()`" below.

## Build / flash / monitor

```
pio run                                          # build
pio run -t upload --upload-port /dev/cu.usbserial-XX   # flash
pio device monitor --port /dev/cu.usbserial-XX --baud 115200
```

`include/secrets.h` (gitignored) must exist with `WIFI_SSID`,
`WIFI_PASSWORD`, `OPENSKY_CLIENT_ID`, `OPENSKY_CLIENT_SECRET` - copy
`include/secrets.h.example` and fill in real values (see the MicroPython
project's `secrets.py` for the current values, or OpenSky's account page
for the client credentials).

## Key findings from bring-up (real hardware, this session)

### Display: ILI9341_2_DRIVER + TFT_INVERSION_ON + rotation=1

This board's panel needed empirical rediscovery under TFT_eSPI - the
MicroPython driver's `rotation=0, bgr=False` fix for its own ILI9342-clone
finding does **not** translate directly. Found by systematic testing on
hardware (see `DisplayManager.cpp`'s comment for the full reasoning):

- `ILI9341_DRIVER` reported correct width/height for rotation=1 (320x240)
  but the physical chip didn't actually reconfigure its scan order to
  match - same underlying "mislabeled clone chip" issue as the MicroPython
  version's ILI9342 finding, just manifesting differently. Fixed by
  switching to `ILI9341_2_DRIVER` (TFT_eSPI's variant driver for exactly
  this class of clone).
- Colors were exact bitwise inverses without `TFT_INVERSION_ON` (verified:
  red -> cyan, green -> purple, blue -> yellow, black -> white - each is
  the precise complement, not a channel-order swap). This is a display
  inversion setting, not an RGB/BGR order issue - the default RGB order is
  correct here, unlike the MicroPython driver's `bgr=False` workaround.
- `rotation=1` is landscape and correctly oriented.

All three flags live in `platformio.ini`'s `build_flags`. If a different
physical panel/unit is ever used, re-verify from scratch - don't assume
these values.

### The MicroPython authenticated-OpenSky-call hang does NOT reproduce here

This was the single biggest open question going into the port. Confirmed
on hardware (Milestone 3): dozens of back-to-back authenticated OpenSky
`states/all` calls via `HTTPClient`/`WiFiClientSecure`, every one completed
in 1-3 seconds with no hangs, no timeouts. The raw-socket workaround the
MicroPython version needed (see its `raw_https_get`) was never built here
- it's unnecessary. `OpenSkyClient` uses plain `HTTPClient` throughout.

(The first "failures" during this test were a self-inflicted 401 from a
300-byte token buffer silently truncating OpenSky's ~1500-character JWT,
not a networking issue - fixed by sizing `kTokenBufSize` to 2048.)

### Large stack-local buffers crashed inside mbedTLS's own call chain

`WiFiClientSecure`'s TLS handshake (`mbedtls_ctr_drbg_seed` -> entropy
gathering -> SHA) is itself several KB deep on the stack by the time it's
reached from `OpenSkyClient::fetchOnce()`. Adding ~2KB+ of our own
stack-local buffers (a token/header buffer) on top of that overflowed the
default 8KB `loopTask` stack, confirmed via a real "Stack canary watchpoint
triggered" crash with a full backtrace into `WiFiClientSecure::connect()`.
Fixed by making those buffers `static` instead of stack-local (see
`OpenSkyClient.cpp`/`AdsbdbClient.cpp`), plus `ARDUINO_LOOP_STACK_SIZE=16384`
in `platformio.ini` as headroom (note: must override
`ARDUINO_LOOP_STACK_SIZE`, not `CONFIG_ARDUINO_LOOP_STACK_SIZE` - the
latter gets silently clobbered back to 8192 by `sdkconfig.h`, included via
`Arduino.h` before `cores/esp32/main.cpp` ever checks it).

### The ESP-IDF task watchdog doesn't work fed from a raw hardware-timer ISR

`WatchdogManager` originally layered the ESP-IDF task watchdog
(`esp_task_wdt`) on top of the hardware-timer stall/self-reboot design,
mirroring the MicroPython version's `machine.WDT` + `Timer` combo. This
caused a real crash: `esp_task_wdt_reset()` called from the timer ISR
does not reliably feed the subscribed `loopTask`'s watchdog (ISR context
doesn't map back to it the way a cooperative in-task call does) - the
ESP-IDF watchdog fired and aborted the board even though the ISR was
running exactly on schedule. Removed entirely; unnecessary anyway since
Milestone 3 already showed the one operation MicroPython needed a hardware
watchdog to insure against (a stuck TLS call) doesn't hang here.

### `ESP.restart()` is not safe to call from an ISR

The stall-detection/self-reboot logic that's left calls a restart function
directly from the same hardware-timer ISR. `ESP.restart()` tries to
gracefully tear down WiFi and other subsystems, which needs locks that
can't be acquired from interrupt context - confirmed via a real "Interrupt
wdt timeout on CPU1" panic when force-testing the self-reboot path (a
different, more severe crash than the task-watchdog one above). Fixed by
using `esp_restart_noos()` instead - the same low-level, no-graceful-
teardown restart ESP-IDF's own panic handler and brownout detector
interrupt call directly from their own interrupt contexts.

Both the stall-reset and self-reboot paths were force-tested afterward
(temporarily shortened intervals + a deliberate infinite loop in `setup()`
that never calls `markProgress()`) and recovered cleanly and repeatably -
`rst:0xc (SW_CPU_RESET)`, no panic, clean reboot, WiFi reconnects. Not
just trusted to compile.

### RTC memory doesn't survive `esp_restart_noos()` here

The soak-test instrumentation (`bootCount`/`fetchOkCount`/etc. in
`main.cpp`) originally used `RTC_DATA_ATTR` variables, on the assumption
(standard for ESP32) that RTC "slow memory" survives any reset except a
true power-on/brownout - so these counters would accumulate across the
scheduled 6h self-reboot, not just since the last boot. A real 9h44m soak
run showed otherwise: the counters silently reset to zero exactly at the
self-reboot boundary, even though the reboot itself was clean (`SW_CPU_RESET`,
no panic - `esp_restart_noos()` worked correctly as a restart mechanism,
just not as RTC-memory-preserving as expected). No source was available
for this framework distribution to confirm exactly why. Rather than chase
another undocumented ESP-IDF/RTC-domain subtlety (this is the third
ISR/reset-related surprise this session - see the two above), switched to
NVS-backed `Preferences` instead: genuinely non-volatile flash storage,
immune to whatever RTC-domain behavior `esp_restart_noos()` has, verified
by force-testing across several real `esp_restart_noos()` cycles (shortened
`SELF_REBOOT_INTERVAL_MS` temporarily, confirmed `bootCount`/`fetchOkCount`
correctly continued incrementing across each reboot rather than resetting).
Costs one small flash write per fetch cycle (roughly once per 30s+),
well within flash wear limits at that cadence.

## What Milestone 7 watched for (and what a longer run should keep watching)

- `ESP.getFreeHeap()` - target: flat or bounded-sawtooth, not a slow
  decline (a decline would mean an actual leak, e.g. an `HTTPClient`/
  `WiFiClientSecure` object not being cleanly destructed each call).
  **Result: flat, no decline, across both segments.**
- `ESP.getMaxAllocHeap()` (largest contiguous block) - target: stays close
  to total free heap. This is the real fragmentation signal, distinct from
  a leak - the whole reason for this migration. **Result: exactly
  110,580 bytes at every reading, zero movement, for the whole 9h44m run.**
- TLS/HTTP error rate over time - target: flat, since the original bug's
  signature was degradation *over uptime*, not a fixed rate. **Result:
  1 transient failure in 767 attempts (~0.13%), self-recovered, no
  clustering or worsening trend.**
- WiFi disconnect/reconnect events. **Result: 0.**
- Self-reboot event count (every `SELF_REBOOT_INTERVAL_MS`, 6h). **Result:
  fired once, right on schedule (~5.99h), recovered cleanly.** Given this
  one run's results, removing the scheduled self-reboot could reasonably
  be considered after a longer (24h+) run confirms the same flat
  `maxAllocHeap` trend holds over multiple reboot cycles - not yet done,
  and not a given just because this one run looked clean.

## Secondary route provider (fallback cache) - deliberately not wired up yet

adsbdb's route table is static and occasionally stale/wrong -
`FlightMath::routeIsPlausible()` already sanity-checks a resolved route
against the aircraft's live position and flags a suspect one with
`ROUTE_FLAG` (`*`), but nothing double-checks a flagged (or missing)
route against a second source. `RouteProvider`/`RouteCache`/`TimeSync`
add that as a generic, provider-agnostic fallback that
`FlightSelector::enrich()` consults only when adsbdb has nothing or gets
flagged - but **no concrete provider is wired in**. `RouteProvider.h`
declares the interface (`fetchRouteLegs()`); `NullRouteProvider.cpp` is
the only implementation right now, and it always returns false, so this
whole path is currently inert - confirmed on hardware to leave existing
enrich()/display behavior unchanged.

Why not just wire up AeroDataBox? Investigated it as the obvious
candidate and found conflicting information about its RapidAPI "Basic"
plan - `aerodatabox.com`'s own pricing page describes it as a time-limited
7-day trial, but the actual RapidAPI marketplace listing (where sign-up
happens) shows it as a genuine `$0.00/mo` ongoing plan: 400 API
units/month, 1,600 requests/month (hard caps), 1,000 requests/hour rate
limit, no trial language. Given that ambiguity, chose to build the
caching/fallback architecture generically now and pick the actual
paid/free provider as a separate follow-up decision, rather than build
around a specific number that turned out to be uncertain. If AeroDataBox
is the one eventually chosen: its exact per-request unit cost for a
callsign lookup wasn't confirmed (endpoints are billed at 1/2/6 units
depending on tier), so `ROUTE_PROVIDER_MONTHLY_BUDGET` in `config.h`
should be set conservatively (worst case ~65 requests/month at 6
units/request) until that's verified against a real account.

Design notes for whoever wires in a real provider:
- **Cache key is the callsign, not the tail number (ICAO24)** - a tail
  number is reused by the same aircraft across many different city-pair
  legs in a day, so keying by it would be actively wrong. A callsign
  (flight number) corresponds to a specific published route for a given
  day.
- **Cache freshness is by local calendar day, not UTC** - UTC midnight
  falls at 6-8pm Central (`HOME_TZ_POSIX` in `config.h`), squarely in
  peak evening flight-watching hours; keying by UTC day would force a
  spurious refetch mid-evening daily. `TimeSync::currentLocalDay()`
  handles this (including DST, via a POSIX TZ rule) - confirmed on
  hardware reporting correct local time (`CDT` in September) after NTP
  sync.
- **Multi-city callsigns** (one flight number flying e.g. ORD->DFW->LAX
  in a day) are handled by caching ALL legs `fetchRouteLegs()` returns
  for that callsign+day, then picking whichever leg's `[depUtc, arrUtc]`
  window contains "now" (falling back to the closest leg by time) - no
  extra network round-trip needed to disambiguate. Verified via a
  temporary synthetic test on hardware (single-leg, multi-leg-in-window,
  and between-legs-nearest-fallback cases, all passed) before removing
  the test scaffolding.
- `RouteLeg` (see `RouteProvider.h`) carries no airline name - only a
  real provider's response will determine whether one is available. Until
  then, if adsbdb has no route at all for a callsign, a successful
  `RouteCache` fallback still won't make that flight render as
  "commercial" (`Flight::isCommercial()` checks `airline[0]`) - only the
  flagged-but-present-route case gets the full benefit today.
- The request budget is tracked in NVS (`Preferences`, namespace
  `"route"`), mirroring the soak-counter pattern in `main.cpp`, so it
  survives the scheduled self-reboot instead of resetting mid-month.

## Known simplification: TARGET_CALLSIGNS

`OpenSkyClient::fetchNearby()` folds the "keep only the closest N flights"
vs. "keep only these specific tracked callsigns" decision into its single
JSON-parsing pass (to avoid a large intermediate candidate buffer), rather
than keeping fetch and selection as separate steps the way the Python
version does. Functionally equivalent as long as the number of tracked
callsigns stays small (the normal case) - see the comment in
`OpenSkyClient.cpp`'s per-state insertion logic.
