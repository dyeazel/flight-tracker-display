#pragma once

// WiFi connect/reconnect, ported from cyd_nearby_flights.py's connect_wifi().
// Runs unattended on a wall/desk display - connect() blocks and retries
// indefinitely rather than giving up after one failed attempt, since a
// transient AP hiccup shouldn't leave the display frozen until someone
// notices and power-cycles it.
namespace WifiManager {

// Blocks until connected. onAttempt (if given) is called once per
// connection attempt so a caller can draw "Connecting to WiFi..." status -
// this module doesn't know about the display, kept decoupled per the
// plan's risk-first milestone ordering (Milestone 1 tests this with no
// display wired up at all). onProgress (if given) is called roughly once
// per second while waiting, so a caller can feed a watchdog once
// WatchdogManager exists (Milestone 7) without WifiManager depending on it.
using StatusCallback = void (*)(int attempt);
using ProgressCallback = void (*)();
void connect(StatusCallback onAttempt = nullptr, ProgressCallback onProgress = nullptr);

bool isConnected();

#ifdef TEST_HOOKS
// Bench-test hook: makes isConnected() report false and connect() spin without actually attempting,
// WITHOUT touching the real radio/association - the real WiFi link stays up underneath, so turning this
// back off recovers instantly instead of needing a real reconnect. Exercises Health's "wifi down" check,
// which this project (unlike ../mlb-board) has no runtime-reconfigurable credentials to trigger for real.
void testForceDown(bool on);
#endif

}  // namespace WifiManager
