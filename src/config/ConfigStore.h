#pragma once

#include <stdint.h>

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
#include "config/RadarAirportList.h"

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

// Radar screen's own zoom (Phase 11) - independent of radiusOverrideMi() above, which still governs what's
// fetched at all. Always a concrete value (no "auto" sentinel) - see config.h's DEFAULT_RADAR_RANGE_MI.
float radarRangeMi();

// Radar screen's "always show these airports" list (Phase 11) - see RadarAirportList.h.
int radarAirportCount();
const char *radarAirport(int i);  // i in [0, radarAirportCount()); out of range returns ""

// Display density (Phase 9/10) - which FlightScreen mode to show. Persisted separately from save() (a
// narrow setter, not bundled with WiFi/location/callsigns) since it's changed far more often: every
// left/right-edge tap (see Gesture.h), not just from the web config page.
enum class DisplayMode : uint8_t { MaxDensity, TwoUp, OneUp, Radar };
DisplayMode displayMode();
void setDisplayMode(DisplayMode mode);

constexpr int kSsidMax = 32;  // 802.11 limits
constexpr int kPassMax = 63;

// Validates and saves. `pass == nullptr` keeps the stored password (empty string means an open network).
// `entryCount`/`radarAirportCount` may be 0. Returns nullptr on success, else a short error string
// ("bad_ssid", "bad_pass", "bad_lat", "bad_lon", "bad_radius", "bad_callsign", "bad_origin",
// "bad_destination", "too_many_callsigns", "duplicate_callsign", "bad_radar_range", "bad_radar_airport",
// "duplicate_radar_airport", "too_many_radar_airports", "save_failed"). A non-empty origin/destination/radar
// airport code is checked against AirportTable::findByCode() - CallsignList/RadarAirportList's own
// validate() only checks shape.
const char *save(const char *ssid, const char *pass, float lat, float lon, float radiusMi,
                  const CallsignList::Entry *entries, int entryCount, float radarRangeMi,
                  const RadarAirportList::Code *radarAirports, int radarAirportCount);

// Erases WiFi credentials (keeps location/radius/callsigns). Used to re-enter provisioning.
void forgetWifi();

}  // namespace ConfigStore
