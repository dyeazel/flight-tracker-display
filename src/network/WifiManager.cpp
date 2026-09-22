#include "WifiManager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "secrets.h"

namespace WifiManager {

namespace {
#ifdef TEST_HOOKS
bool gForceDown = false;
#endif

// The one place "are we really connected" is decided - both connect()'s loop condition and isConnected()
// go through this, so gForceDown can't make them disagree (that would busy-spin connect(): a real
// WL_CONNECTED status would end its loop immediately while isConnected() kept reporting down).
bool reallyConnected() {
#ifdef TEST_HOOKS
    if (gForceDown) return false;
#endif
    return WiFi.status() == WL_CONNECTED;
}
}  // namespace

void connect(StatusCallback onAttempt, ProgressCallback onProgress) {
    WiFi.mode(WIFI_STA);
    int attempt = 0;
    while (!reallyConnected()) {
#ifdef TEST_HOOKS
        if (gForceDown) {
            if (onProgress) onProgress();
            delay(1000);
            continue;
        }
#endif
        attempt++;
        if (onAttempt) onAttempt(attempt);

        // Explicit disconnect before each attempt - the MicroPython version
        // found the ESP32 WiFi driver can get wedged into a stuck
        // "connecting" state if connect() is called again on top of a
        // previous attempt that didn't cleanly resolve. Cheap, and
        // disconnect() is a safe no-op if already idle. Kept here as a
        // carried-forward precaution; worth revisiting once Milestone 1 has
        // run long enough to show whether Arduino's WiFi stack even needs it.
        WiFi.disconnect();
        delay(300);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        int timeout = 20;
        while (WiFi.status() != WL_CONNECTED && timeout > 0) {
            delay(1000);
            if (onProgress) onProgress();
            timeout--;
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("[wifi] attempt %d timed out, retrying\n", attempt);
        }
    }
}

bool isConnected() { return reallyConnected(); }

#ifdef TEST_HOOKS
void testForceDown(bool on) { gForceDown = on; }
#endif

}  // namespace WifiManager
