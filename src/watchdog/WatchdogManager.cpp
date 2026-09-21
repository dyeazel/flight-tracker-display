#include "WatchdogManager.h"

#include <Arduino.h>
#include <esp_private/system_internal.h>

#include "config.h"

// NOTE: originally also subscribed to the ESP-IDF task watchdog
// (esp_task_wdt) and fed it from this same hardware-timer ISR, mirroring
// the MicroPython version's two-layer design (machine.WDT + Timer feed).
// Dropped after it caused a real, reproducible crash on real hardware:
// esp_task_wdt_reset() called from a raw hw_timer_t ISR does not reliably
// feed the subscribed loopTask (ISR context doesn't map back to it the way
// a cooperative in-task call does), so the ESP-IDF watchdog fired and
// aborted the board even though this ISR was running on schedule. Since
// Milestone 3 already proved the OpenSky authenticated call itself never
// hangs under Arduino's HTTPClient/WiFiClientSecure (the whole reason
// MicroPython needed a hardware watchdog as insurance against a single
// stuck TLS call), the stall/self-reboot logic below - already
// independent of the main loop, verified by force-testing the stall path
// directly - is sufficient on its own without layering the ESP-IDF
// watchdog on top.
//
// Also found by force-testing (see plan): plain ESP.restart() is NOT safe
// to call from this ISR either - it tries to gracefully tear down WiFi
// and other subsystems, which involves locks that can't be acquired from
// interrupt context, and that caused a real "Interrupt wdt timeout"
// panic on hardware instead of a clean reboot. esp_restart_noos() is
// used instead - the same low-level, no-graceful-teardown restart
// ESP-IDF's own panic handler and brownout detector interrupt call
// directly from their own interrupt/abnormal contexts, which is
// architecturally the same situation this ISR is in.
namespace WatchdogManager {

namespace {
hw_timer_t *timer = nullptr;
volatile unsigned long lastProgressMs = 0;

// Stall detector only. The scheduled self-reboot used to live here too, but
// esp_restart_noos() cannot record why it restarted or drop WiFi cleanly, so
// it moved to main.cpp's loop (Diag::reboot). A software reset with no
// recorded reason is therefore, by construction, this ISR.
void IRAM_ATTR onTimer() {
    unsigned long now = millis();
    if (lastProgressMs != 0 && (now - lastProgressMs) > STALL_RESET_MS) {
        esp_restart_noos();
    }
}
}  // namespace

void begin() {
    lastProgressMs = millis();

    // Timer 0, /80 prescaler on the 80MHz APB clock -> 1MHz tick (1us per
    // count), counting up.
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    timerAlarmWrite(timer, (uint64_t)WDT_FEED_INTERVAL_MS * 1000, true);
    timerAlarmEnable(timer);
}

void markProgress() { lastProgressMs = millis(); }

}  // namespace WatchdogManager
