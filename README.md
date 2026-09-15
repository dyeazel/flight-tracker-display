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

Milestones 0-6 (of the implementation plan) are done and verified on real
hardware: toolchain, WiFi, display, the authenticated OpenSky call, the
full data pipeline, rendering, and watchdog hardening. **Milestone 7 (a
multi-hour soak test) has not been run yet** - that's the actual proof the
migration solved the original fragmentation problem, not just that it
compiles and runs briefly. See "What Milestone 7 should watch for" below.

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

## What Milestone 7 (not yet run) should watch for

A multi-hour (8-12h minimum, ideally 24h+) unattended run, logging over
time:

- `ESP.getFreeHeap()` - target: flat or bounded-sawtooth, not a slow
  decline (a decline would mean an actual leak, e.g. an `HTTPClient`/
  `WiFiClientSecure` object not being cleanly destructed each call).
- `ESP.getMaxAllocHeap()` (largest contiguous block) - target: stays close
  to total free heap. This is the real fragmentation signal, distinct from
  a leak - the whole reason for this migration.
- TLS/HTTP error rate over time - target: flat, since the original bug's
  signature was degradation *over uptime*, not a fixed rate.
- WiFi disconnect/reconnect events.
- Self-reboot event count (every `SELF_REBOOT_INTERVAL_MS`, 6h) - only
  after a clean soak should removing this scheduled reboot even be
  considered, and only with real data in hand.

## Known simplification: TARGET_CALLSIGNS

`OpenSkyClient::fetchNearby()` folds the "keep only the closest N flights"
vs. "keep only these specific tracked callsigns" decision into its single
JSON-parsing pass (to avoid a large intermediate candidate buffer), rather
than keeping fetch and selection as separate steps the way the Python
version does. Functionally equivalent as long as the number of tracked
callsigns stays small (the normal case) - see the comment in
`OpenSkyClient.cpp`'s per-state insertion logic.
