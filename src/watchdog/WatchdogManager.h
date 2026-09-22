#pragma once

// Self-contained hardware-timer-driven stall detector, ported in spirit from
// cyd_nearby_flights.py's start_wdt() / _wdt_timer_callback() - but WITHOUT the
// ESP-IDF task watchdog layer that design also used. That layer was tried and
// removed: esp_task_wdt_reset() called from this same hardware-timer ISR does
// not reliably feed a task's subscription (confirmed by a real crash on
// hardware - see WatchdogManager.cpp's comment), and it's unnecessary here
// regardless, since Milestone 3 already proved the OpenSky authenticated call
// - the one operation MicroPython needed a true hardware watchdog to insure
// against - never hangs under Arduino's HTTPClient/WiFiClientSecure.
//
// What's left is markProgress() + STALL_RESET_MS: an ISR, decoupled from the
// main loop/task, forces a restart (esp_restart_noos - the only restart that is
// safe from interrupt context) if no progress (loop iteration or retry attempt)
// has been marked in that long - the same generous, independent fallback the
// MicroPython version had as its second layer.
//
// The scheduled self-reboot (SELF_REBOOT_INTERVAL_MS) is NOT here: it runs via Health/Diag::reboot() in
// task context, because a restart from this ISR can neither record its reason nor deauthenticate from the
// router cleanly.
//
// Fed ONLY from the UI task (main.cpp's loop()) since NetTask was split out onto its own FreeRTOS task -
// markProgress() must not be called from NetTask or anything it calls (adsbdb/AeroDataBox/OpenSky
// clients, FlightSelector::enrich()): this is a single shared timestamp, so calls from a second task would
// mean a wedged UI loop could still look "alive" as long as NetTask kept running. NetTask's own liveness
// is watched separately, by Health, via NetTask::msSinceLoop().
namespace WatchdogManager {

// Starts the hardware-timer stall ISR. Call once, early in setup().
void begin();

// Call frequently (loop top, inside retry loops, etc.) to record that
// forward progress is happening - resets the STALL_RESET_MS clock.
void markProgress();

}  // namespace WatchdogManager
