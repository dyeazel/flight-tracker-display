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
const char *callsign(int i);     // i in [0, callsignCount()); out of range returns ""
const char *origin(int i);       // "" if unpinned or i out of range
const char *destination(int i);  // "" if unpinned or i out of range
// Fills out[0..kMax) and returns callsignCount() - convenient for passing straight into
// FlightSelector::findMissing/OpenSkyParse::parseStates (neither cares about pins, only the callsign).
int callsigns(CallsignList::Buf *out);

// True if `callsign` is one of the tracked entries (regardless of whether it has a pin); fills
// outOrigin/outDestination (each CallsignList::kCodeChars+1 bytes) with that entry's pin fields, "" for
// whichever field isn't pinned - see FlightSelector::enrich()'s "Route pinning" comment (Phase 8).
bool pinFor(const char *callsign, char *outOrigin, char *outDestination);

constexpr int kSsidMax = 32;  // 802.11 limits
constexpr int kPassMax = 63;

// Validates and saves. `pass == nullptr` keeps the stored password (empty string means an open network).
// `entryCount` may be 0 (nearby mode). Returns nullptr on success, else a short error string ("bad_ssid",
// "bad_pass", "bad_lat", "bad_lon", "bad_radius", "bad_callsign", "bad_origin", "bad_destination",
// "too_many_callsigns", "duplicate_callsign", "save_failed"). A non-empty origin/destination is checked
// against AirportTable::findByCode() - CallsignList::validate() only checks their shape.
const char *save(const char *ssid, const char *pass, float lat, float lon, float radiusMi,
                  const CallsignList::Entry *entries, int entryCount);

// Erases WiFi credentials (keeps location/radius/callsigns). Used to re-enter provisioning.
void forgetWifi();

}  // namespace ConfigStore
