#pragma once

#include <ArduinoJson.h>

#include "RouteProvider.h"

// The pure JSON-decoding half of AeroDataBoxClient::fetchOnce(), split out so it can be exercised by the
// native Unity test env (see [env:native] in platformio.ini) against a real captured response body, with
// no HTTPClient/WiFiClientSecure dependency - mirrors ../mlb-board's EspnParse split.
namespace AeroDataBoxParse {

// Parses one already-deserialized top-level response body (a JSON array of flight-record elements, or -
// per AeroDataBoxClient.h's comment - sometimes a single object) into outLegs (up to maxLegs) and sets
// *outLegCount. Returns true if at least one leg was written.
bool parseLegs(JsonVariant root, RouteLeg *outLegs, int maxLegs, int *outLegCount);

}  // namespace AeroDataBoxParse
