#pragma once

// Newline-delimited JSON commands on the USB serial port: one JSON object per line in, one per line out.
// Debug logs elsewhere always start with '[', so a host tool can ignore any line not starting with '{'.
// Line buffering, JSON parsing, and reply hygiene (one Serial.write() per reply, so a concurrent log
// line can't splice into it) are ../../cyd-common's shared SerialCore - this file only owns the actual
// command set. Same shape as ../mlb-board's SetupSerial (hello/get/diag/reboot), so shared host tools
// work on both.
//
//   {"cmd":"hello"}            -> {"ok":true,"cmd":"hello","fw":"...","board":"cyd","proto":2,
//                                  "maxCallsigns":8}
//   {"cmd":"get"}              -> {"ok":true,"cmd":"get","debug":false,"radius_mi":50.0,"refresh_s":30,
//                                  "wifi":true,"wifi_state":"connected","failure":"","ip":"..",
//                                  "time_synced":true,"ssid":"..","hasPass":true,"lat":43.18,"lon":-88.99,
//                                  "callsigns":["UAL1217"]}                  (never returns the password)
//   {"cmd":"set","ssid":"..","pass":"..","lat":43.18,"lon":-88.99,"radius_mi":50.0,
//    "callsigns":["UAL1217"]}
//        Phase 7 (web/config.html, Web Serial), mirrors ../../mlb-board's SetupSerial. "pass" optional:
//        omitted keeps the stored one, "" means an open network. "radius_mi" optional: omitted/RADIUS_MI_AUTO
//        means auto (see FlightMath::effectiveRadius) - forced wide anyway whenever "callsigns" is
//        non-empty. "callsigns": 0-8 tracked flight callsigns; empty means "show whatever's nearby".
//   {"cmd":"scan"}             -> {"ok":true,"cmd":"scan","networks":[{"ssid":"..","rssi":-50,"secure":true}]}
//   {"cmd":"forget"}           erases WiFi credentials (back to the setup screen)
//   {"cmd":"diag"}             -> uptime, boot reason, restart counts, heap, poll counters, per-source network
//                                 timing and the last failure (see Diag::fillDiag)
//   {"cmd":"debug"}            -> {"ok":true,"cmd":"debug","on":false}       (query)
//   {"cmd":"debug","on":true}  -> {"ok":true,"cmd":"debug","on":true}        (explicit, idempotent - the old
//                                 stateful 'd' toggle made scripted use error-prone)
//   {"cmd":"reboot"}           -> reply, then a clean restart recorded as reason "user"
// Failures: {"ok":false,"cmd":"..","error":"bad_json|too_long|unknown_cmd|bad_ssid|bad_pass|bad_lat|
//           bad_lon|bad_radius|bad_callsign|too_many_callsigns|duplicate_callsign|scan_failed"}
//
// Bench-test commands, compiled only into TEST_HOOKS builds (env:esp32dev-test), to force failure paths on
// real hardware: _hang (stops calling WatchdogManager::markProgress - the UI task, so this now tests the
// stall ISR in isolation from NetTask, which keeps fetching normally throughout), _blocknet {"on":bool}
// (every network call, on NetTask, fails immediately), _slownet {"ms":N} (every network call stalls N ms
// first with no progress marking - large enough and NetTask::msSinceLoop() grows past
// HEALTH_NET_STUCK_MS, exercising Health's "net stuck" restart), _wifidown {"on":bool} (WifiManager
// reports itself down without touching the real radio - the real link stays up, so turning it back off
// recovers instantly; exercises Health's "wifi down" restart, which this project has no runtime WiFi
// reconfiguration to trigger for real), _resetdiag (wipes lifetime counters - avoid on the deployed
// board), _lookup {"callsign":".."} (one adsbdb lookup with its real retry policy, on the UI task - see
// the comment at its call site for the cross-task caveat).
// Screenshot builds (SCREEN_CAPTURE, env:esp32dev-shot / -sim) add _shot (dump the panel; tools/screenshot.py);
// sim builds (SIM_DATA, env:esp32dev-sim) add _sim {"n":k} to hold scripted scene k (n < 0 resumes cycling).
namespace SerialCommands {

// Call from the main loop and from any long delay loop; non-blocking (reads only what is buffered).
void update();

}  // namespace SerialCommands
