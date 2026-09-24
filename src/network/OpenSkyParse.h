#pragma once

#include <ArduinoJson.h>

#include "config/CallsignList.h"
#include "flight/Flight.h"

// The pure JSON-decoding half of OpenSkyClient's fetchOnce()/fetchLiveTrackFirstPoint(), split out so it
// can be exercised by the native Unity test env (see [env:native] in platformio.ini) against a real
// captured response body, with no HTTPClient/WiFiClientSecure dependency - mirrors ../mlb-board's
// EspnParse split.
namespace OpenSkyParse {

// Parses an already-deserialized /api/states/all response body (root is the document's top level) into
// outFlights (up to maxOut, closest-first by distance from homeLat/homeLon), keeping only states within
// radiusMi and (if targetCallsignCount > 0) matching one of targetCallsigns. Sets *outCount. States
// missing lon/lat, or with fewer than 12 array elements (a malformed row), are skipped. homeLat/homeLon/
// targetCallsigns are ConfigStore's values, passed in (rather than read from ConfigStore directly) so
// this stays a pure, native-testable function.
void parseStates(JsonVariant root, float radiusMi, float homeLat, float homeLon, Flight *outFlights,
                  int maxOut, int *outCount, const CallsignList::Buf *targetCallsigns,
                  int targetCallsignCount);

// Parses an already-deserialized /api/tracks/all response body's first waypoint (path[0]: [time, lat,
// lon, baro_altitude, true_track, on_ground] - see OpenSky's /tracks schema) into *outLat/*outLon.
// Returns false if there's no path, or the first waypoint is missing lat/lon.
bool parseTrackFirstPoint(JsonVariant root, float *outLat, float *outLon);

}  // namespace OpenSkyParse
