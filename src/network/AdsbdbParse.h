#pragma once

#include <ArduinoJson.h>

#include "AdsbdbClient.h"

// The pure JSON-decoding half of AdsbdbClient::fetchOnce(), split out so it can be exercised by the
// native Unity test env (see [env:native] in platformio.ini) against a real captured response body,
// with no HTTPClient/WiFiClientSecure dependency - mirrors ../mlb-board's EspnParse split.
namespace AdsbdbParse {

// Parses one already-deserialized adsbdb /v0/callsign/<callsign> response body (root is the document's
// top level, i.e. what deserializeJson() filled) into *info. Returns true if a flightroute was present
// (info's fields are set to whatever the route happened to provide - a nonexistent airline/origin/
// destination sub-object just leaves that field's initializers unchanged); false for a clean "no route
// on file" response (e.g. {"response":"unknown callsign"}), same as a nonexistent JSON key.
bool parseRoute(JsonVariant root, AdsbdbClient::RouteInfo *info);

}  // namespace AdsbdbParse
