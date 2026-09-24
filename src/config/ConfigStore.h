#pragma once

// Runtime user configuration in NVS (Preferences namespace "cfg"): WiFi credentials, home location,
// search radius override, and tracked callsigns. Set by the web setup page over serial (see
// SerialCommands's set/scan/forget, mirroring ../../../mlb-board's ConfigStore/SetupSerial). Values are
// cached in static buffers/vars, so getters never allocate.
//
// Dev convenience: if include/secrets.h exists it seeds WiFi credentials ONCE on first boot (a "seeded"
// flag stops it re-seeding after a `forget`). End-user builds have no secrets.h. Location/radius/callsigns
// have no such gate - they always default from config.h's HOME_LAT/HOME_LON/RADIUS_MI_AUTO/empty list when
// their NVS key is absent, since those aren't secrets.
#include "config/CallsignList.h"

namespace ConfigStore {

void begin();

bool hasWifi();
const char *ssid();
const char *pass();
bool hasPass();

float lat();
float lon();
// RADIUS_MI_AUTO means "no override" - see FlightMath::effectiveRadius(). Ignored (forced wide) whenever
// callsignCount() > 0, same as the old compile-time RADIUS_MI/TARGET_CALLSIGNS_COUNT interaction.
float radiusOverrideMi();

int callsignCount();
const char *callsign(int i);  // i in [0, callsignCount()); out of range returns ""
// Fills out[0..kMax) and returns callsignCount() - convenient for passing straight into
// FlightSelector::findMissing/OpenSkyParse::parseStates.
int callsigns(CallsignList::Buf *out);

constexpr int kSsidMax = 32;  // 802.11 limits
constexpr int kPassMax = 63;

// Validates and saves. `pass == nullptr` keeps the stored password (empty string means an open network).
// `callsignCount` may be 0 (nearby mode). Returns nullptr on success, else a short error string
// ("bad_ssid", "bad_pass", "bad_lat", "bad_lon", "bad_radius", "bad_callsign", "too_many_callsigns",
// "duplicate_callsign", "save_failed").
const char *save(const char *ssid, const char *pass, float lat, float lon, float radiusMi,
                  const CallsignList::Buf *callsigns, int callsignCount);

// Erases WiFi credentials (keeps location/radius/callsigns). Used to re-enter provisioning.
void forgetWifi();

}  // namespace ConfigStore
