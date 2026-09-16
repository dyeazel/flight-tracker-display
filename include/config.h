#pragma once

// Hardware/build-time constants for the CYD nearby-flights display.
// Arduino/C++ port of /Users/dean/Projects/Python/flight-tracker/cyd_nearby_flights.py -
// see that file's comments for the "why" behind most of these; kept terse
// here since the rationale is already documented there.

// ---- Debug ----
// Toggle at runtime by typing 'd'/'D' + Enter on the serial monitor.
extern bool DEBUG;

// ---- Watchdog / self-reboot ----
// No ESP-IDF task watchdog / WDT_TIMEOUT_MS here - see WatchdogManager.h
// for why. WDT_FEED_INTERVAL_MS is this module's own stall-check interval.
#define WDT_FEED_INTERVAL_MS 10000
#define STALL_RESET_MS (5UL * 60UL * 1000UL)
#define SELF_REBOOT_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)

// A raw-socket HTTPS fallback (mirroring the MicroPython version's
// raw_https_get workaround) was planned in case Milestone 3 found
// HTTPClient/WiFiClientSecure reproduced the auth-endpoint hang that
// forced that workaround under MicroPython - it didn't (confirmed on
// hardware, dozens of clean back-to-back authenticated calls), so no raw
// socket client exists here at all.

// ---- Location / behavior ----
#define HOME_LAT 43.184601
#define HOME_LON -88.990422

// Leave empty to show whatever's nearby; see TARGET_CALLSIGNS usage in
// FlightSelector for tracking specific callsigns instead.
#define TARGET_CALLSIGNS_COUNT 0
extern const char *const TARGET_CALLSIGNS[];

#define RADIUS_MI_AUTO -1.0f  // sentinel: "not overridden", see effectiveRadius()
#define RADIUS_MI RADIUS_MI_AUTO
#define DEFAULT_RADIUS_MI 50.0f
#define CALLSIGN_SEARCH_RADIUS_MI 10000.0f

#define REFRESH_SECONDS 30
#define REDRAW_SECONDS 2
#define MAX_ROWS 6
#define LOOKUP_ROUTES true

#define MIN_DEAD_RECKON_KT 30.0f

// ---- Network endpoints ----
#define OPENSKY_HOST "opensky-network.org"
#define OPENSKY_PATH "/api/states/all"
#define OPENSKY_TOKEN_URL "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
#define ADSBDB_URL_FMT "https://api.adsbdb.com/v0/callsign/%s"
#define USER_AGENT "Mozilla/5.0 (compatible; cyd_nearby_flights_arduino/1.0)"

// ---- OAuth2 token cache ----
#define OPENSKY_TOKEN_TTL_MS (25UL * 60UL * 1000UL)

// ---- Rate limiting ----
#define RATE_LIMIT_LOW_WATER 20
#define RATE_LIMIT_BACKOFF_SECONDS 600

// ---- Plain HTTPS retries (adsbdb, OAuth token endpoint) ----
#define HTTPS_RETRIES 4
#define HTTPS_RETRY_DELAY_MS 400

// ---- Route logic ----
#define CHARTER_OPERATOR_PREFIXES_COUNT 5
extern const char *const CHARTER_OPERATOR_PREFIXES[CHARTER_OPERATOR_PREFIXES_COUNT];
#define ROUTE_FLAG "*"

// ---- Rendering / layout ----
#define ALT_TREND_THRESHOLD_FPM 150.0f
#define TREND_ICON_SIZE 8
#define TREND_ICON_RADIUS 3
#define TREND_ICON_GAP 2
#define CALLSIGN_COL_CHARS 10
#define VALUE_X (4 + CALLSIGN_COL_CHARS * 8 + 4)
#define HEARTBEAT_SIZE 6

// ---- Wall clock (NTP) ----
// Needed only for the secondary route-provider cache below (per-local-day
// freshness, multi-leg time-window matching) - nothing else in this app
// needs wall-clock time. HOME_TZ_POSIX is a POSIX TZ rule (handles DST
// automatically) for the tracker's fixed physical location, same
// assumption already baked into HOME_LAT/HOME_LON - update this if the
// board is ever relocated to a different timezone.
#define NTP_SERVER "pool.ntp.org"
#define HOME_TZ_POSIX "CST6CDT,M3.2.0,M11.1.0"  // America/Chicago

// ---- Secondary route provider (fallback cache) ----
// FlightSelector::enrich() consults this only when adsbdb has no route or
// route_is_plausible() flags one as suspect - see RouteCache/RouteProvider
// and the README's "Secondary route provider" section. Currently backed
// by NullRouteProvider (always returns false, no-op) - which actual paid
// API to wire in (AeroDataBox, FlightAware AeroAPI, etc.) is a deliberately
// deferred decision, so these numbers are placeholders to be revisited
// once that's chosen. (Reference point: AeroDataBox's RapidAPI "Basic"
// plan - confirmed a genuine ongoing free tier, not a trial - caps out at
// 400 API units/1,600 requests per month and 1,000 requests/hour; a
// callsign lookup's exact per-request unit cost wasn't confirmed, so a
// real budget for that provider should assume the worst-case tier.)
#define ROUTE_CACHE_SIZE 32
#define ROUTE_CACHE_MAX_LEGS 4
#define ROUTE_PROVIDER_MIN_INTERVAL_MS 1100
#define ROUTE_PROVIDER_MONTHLY_BUDGET 500
