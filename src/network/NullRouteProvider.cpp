#include "RouteProvider.h"

// No secondary route provider is wired in yet - deciding which paid/free
// API to use (AeroDataBox, FlightAware AeroAPI, etc.) is a deliberately
// deferred follow-up decision (see README's "Secondary route provider"
// section). This stub keeps RouteCache and FlightSelector::enrich()'s
// fallback path fully exercised and compiling, while guaranteeing it
// never actually overrides adsbdb's answer until a real client replaces
// it.
bool fetchRouteLegs(const char * /*callsign*/, RouteLeg * /*outLegs*/, int /*maxLegs*/,
                     int *outLegCount) {
    *outLegCount = 0;
    return false;
}
