#pragma once

#include "flight/Flight.h"

// OpenSky Network client: OAuth2 client-credentials token cache + the
// authenticated states/all fetch, ported from cyd_nearby_flights.py's
// get_opensky_token()/fetch_nearby()/current_refresh_interval().
//
// Milestone 3 (see main.cpp git history) confirmed plain HTTPClient +
// WiFiClientSecure does NOT reproduce the MicroPython urequests hang on
// this exact authenticated call - so no raw-socket workaround is needed
// here, unlike the Python version.
namespace OpenSkyClient {

// Returns true and fills tokenOut with a cached (or freshly fetched)
// bearer token; false on failure. force=true bypasses the cache (e.g.
// after a 401).
bool getToken(char *tokenOut, size_t tokenOutSize, bool force = false);

// Fetches nearby aircraft within radiusMi of HOME_LAT/HOME_LON, filters by
// haversine distance, sorts by distance ascending, and fills outFlights
// (up to maxOut entries - excess candidates beyond maxOut are simply not
// stored, matching the fixed-size/no-dynamic-allocation design). Returns
// the number of flights written, or -1 on failure (network/auth error -
// caller should treat this like the Python version's raised exception:
// show an error state and back off).
int fetchNearby(float radiusMi, Flight *outFlights, int maxOut);

// Fetches the live/current-day track for icao24 (OpenSky's "purely
// experimental" - their own words - /tracks/all endpoint) and returns just
// its first waypoint's lat/lon, which OpenSky's docs say is set
// "immediately after the aircraft's expected departure, or after the
// network received the first position when the aircraft entered its
// reception range" - i.e. a real, independent read on where this flight
// actually started, for DepartureVerifier to check a claimed route's
// origin against. Returns false on any failure (no track yet, network
// error, budget/rate-limited) - treat like every other provider here:
// skip verification for this flight, not fatal. Billed against OpenSky's
// /tracks/* credit bucket, entirely separate from fetchNearby()'s
// /states/* bucket - see config.h's TRACK_PROVIDER_MIN_INTERVAL_MS.
bool fetchLiveTrackFirstPoint(const char *icao24, float *outLat, float *outLon);

// Normally REFRESH_SECONDS, but backs off hard once the last successful
// response's X-Rate-Limit-Remaining header says the day's quota is running
// low - see RATE_LIMIT_LOW_WATER/RATE_LIMIT_BACKOFF_SECONDS in config.h.
int currentRefreshInterval();

// A short human-readable reason for the most recent fetchNearby()/getToken()
// failure, for display on the error screen - mirrors the Python version's
// str(e)[:34] in its except block.
const char *lastErrorMessage();

}  // namespace OpenSkyClient
