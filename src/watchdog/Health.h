#pragma once

// Loop-level health monitoring: catches conditions the stall watchdog can't see by itself - NetTask is
// technically still looping but never producing new flights, WiFi never reconnects, heap fragmentation -
// and restarts cleanly from task context when one persists too long. Also owns the scheduled maintenance
// reboot (deferred while flights are on screen; see config.h's SELF_REBOOT_INTERVAL_MS /
// SELF_REBOOT_FORCE_EXTRA_MS). Restarts triggered here are capped (Diag::consecutiveHealthReboots(),
// HEALTH_MAX_CONSECUTIVE) so a persistent outage causes a few reboots, not a loop - past the cap, the
// outage just shows as stale data on screen (main.cpp) until it clears on its own.
//
// Runs entirely on the UI task - only reads state published by NetTask/WifiManager/Diag, never blocks.
namespace Health {

// Call once, after Diag::begin() and NetTask::begin().
void begin();

// Call every loop() tick.
void update();

}  // namespace Health
