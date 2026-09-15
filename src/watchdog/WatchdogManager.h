#pragma once

// Self-contained hardware-timer-driven stall detector and scheduled
// self-reboot, ported in spirit from cyd_nearby_flights.py's start_wdt() /
// _wdt_timer_callback() - but WITHOUT the ESP-IDF task watchdog layer that
// design also used. That layer was tried and removed: esp_task_wdt_reset()
// called from this same hardware-timer ISR does not reliably feed a task's
// subscription (confirmed by a real crash on hardware - see
// WatchdogManager.cpp's comment), and it's unnecessary here regardless,
// since Milestone 3 already proved the OpenSky authenticated call - the
// one operation MicroPython needed a true hardware watchdog to insure
// against - never hangs under Arduino's HTTPClient/WiFiClientSecure.
//
// What's left is markProgress() + STALL_RESET_MS: an ISR, decoupled from
// the main loop/task, forces a real ESP.restart() if no progress (loop
// iteration or retry attempt) has been marked in that long - the same
// generous, independent fallback the MicroPython version had as its
// second layer. A scheduled reboot every SELF_REBOOT_INTERVAL_MS is also
// checked in the same ISR, as a mitigation against possible degradation
// over long uptime (open question - see HANDOFF/plan: keep this through
// the Milestone 8 soak test regardless of how things look early on).
namespace WatchdogManager {

// Starts the hardware-timer stall/self-reboot ISR. Call once, early in
// setup().
void begin();

// Call frequently (loop top, inside retry loops, etc.) to record that
// forward progress is happening - resets the STALL_RESET_MS clock.
void markProgress();

// delay() broken into chunks, marking progress each time - a long
// uninterrupted delay (e.g. the rate-limit backoff, which can exceed
// STALL_RESET_MS) would otherwise look like a stall and trigger a
// false-positive restart mid-backoff.
void delayWithProgress(unsigned long totalMs, unsigned long chunkMs);

}  // namespace WatchdogManager
