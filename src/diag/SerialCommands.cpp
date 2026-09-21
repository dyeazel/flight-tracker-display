#include "SerialCommands.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "diag/Diag.h"
#include "flight/FlightMath.h"
#include "network/AdsbdbClient.h"
#include "time/TimeSync.h"

namespace SerialCommands {

namespace {

constexpr size_t kLineMax = 160;
char gLine[kLineMax];  // static: no per-command allocation of the line buffer
size_t gLen = 0;
bool gOverflow = false;

// serializeJson(doc, Serial) issues many small writes, and a log line printed in between would land in the middle
// of the reply and make it unparseable. Build the whole reply first and send it in ONE write; the leading newline
// guarantees the reply starts on a fresh line even if a log line was cut between its text and its newline.
void reply(JsonDocument &doc) {
    char buf[1400];  // on the loop task's 16 KB stack; the largest reply (diag) is under ~900 bytes
    const size_t need = measureJson(doc);
    if (need + 2 <= sizeof(buf)) {
        buf[0] = '\n';
        const size_t n = serializeJson(doc, buf + 1, sizeof(buf) - 2);
        buf[1 + n] = '\n';
        Serial.write(reinterpret_cast<const uint8_t *>(buf), n + 2);
        return;
    }
    Serial.print('\n');  // oversized (not expected): best effort, may interleave
    serializeJson(doc, Serial);
    Serial.print('\n');
}

void replyError(const char *cmd, const char *error) {
    JsonDocument d;
    d["ok"] = false;
    d["cmd"] = cmd ? cmd : "";
    d["error"] = error;
    reply(d);
}

void replyOk(const char *cmd) {
    JsonDocument d;
    d["ok"] = true;
    d["cmd"] = cmd;
    reply(d);
}

void handleLine(const char *line) {
    if (line[0] != '{') return;  // not a command
    JsonDocument in;
    if (deserializeJson(in, line)) return replyError("", "bad_json");
    const char *cmd = in["cmd"] | "";

    if (!strcmp(cmd, "hello")) {
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "hello";
        d["fw"] = "flight-tracker-arduino " __DATE__;
        d["board"] = "cyd";
        d["proto"] = 1;
        return reply(d);
    }
    if (!strcmp(cmd, "get")) {
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "get";
        d["debug"] = DEBUG;
        d["radius_mi"] = FlightMath::effectiveRadius();
        d["refresh_s"] = REFRESH_SECONDS;
        d["wifi"] = WiFi.status() == WL_CONNECTED;
        d["ip"] = WiFi.localIP().toString();
        d["time_synced"] = TimeSync::isSynced();
        return reply(d);
    }
    if (!strcmp(cmd, "diag")) {
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "diag";
        Diag::fillDiag(d);
        return reply(d);
    }
    if (!strcmp(cmd, "debug")) {
        if (in["on"].is<bool>()) DEBUG = in["on"].as<bool>();
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "debug";
        d["on"] = DEBUG;
        return reply(d);
    }
    if (!strcmp(cmd, "reboot")) {
        replyOk("reboot");
        Serial.flush();
        Diag::reboot("user");
    }
#ifdef TEST_HOOKS
    if (!strcmp(cmd, "_hang")) {
        replyOk("_hang");
        Serial.flush();
        for (;;) {
        }  // never calls WatchdogManager::markProgress(): the stall ISR must rescue us
    }
    if (!strcmp(cmd, "_blocknet")) {
        Diag::testSetBlockNet(in["on"] | true);
        return replyOk("_blocknet");
    }
    if (!strcmp(cmd, "_slownet")) {
        Diag::testSetSlowNetMs(in["ms"] | 0);
        return replyOk("_slownet");
    }
    if (!strcmp(cmd, "_resetdiag")) {
        Diag::testResetCounters();
        return replyOk("_resetdiag");
    }
    if (!strcmp(cmd, "_lookup")) {  // {"cmd":"_lookup","callsign":"ZZZ9999"}: one adsbdb lookup, incl. its retry policy
        AdsbdbClient::RouteInfo info;
        const bool found = AdsbdbClient::lookupRoute(in["callsign"] | "", &info);
        JsonDocument d;
        d["ok"] = true;
        d["cmd"] = "_lookup";
        d["found"] = found;
        d["origin"] = info.originCode;
        d["destination"] = info.destinationCode;
        return reply(d);
    }
#endif
    replyError(cmd, "unknown_cmd");
}

}  // namespace

void update() {
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            if (gOverflow) {
                replyError("", "too_long");
            } else if (gLen > 0) {
                gLine[gLen] = '\0';
                handleLine(gLine);
            }
            gLen = 0;
            gOverflow = false;
            continue;
        }
        if (gLen < kLineMax - 1) gLine[gLen++] = c;
        else gOverflow = true;
    }
}

}  // namespace SerialCommands
