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
// #ifndef-guarded so bench builds can shorten them with -D (see
// [env:esp32dev-test] in platformio.ini) to force-test the recovery paths.
#ifndef STALL_RESET_MS
#define STALL_RESET_MS (5UL * 60UL * 1000UL)
#endif
// 24h, not the original 6h: two soak runs (9h44m pre-verification, then a full 12h run with Diag/Health
// instrumentation on 2026-09-21) both showed a perfectly flat heap_max_alloc across every boot segment -
// no sign of the fragmentation the frequent-reboot mitigation existed for in the first place.
#ifndef SELF_REBOOT_INTERVAL_MS
#define SELF_REBOOT_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)
#endif
// A scheduled reboot is deferred while flights are on screen (see Health::update()), but forced anyway
// after this much extra time so it can't be postponed forever by continuous traffic.
#ifndef SELF_REBOOT_FORCE_EXTRA_MS
#define SELF_REBOOT_FORCE_EXTRA_MS (6UL * 60UL * 60UL * 1000UL)
#endif

// ---- Health (loop-level recovery beyond what the stall ISR can see - see src/watchdog/Health.h) ----
// Each health-triggered restart is counted separately from bootCount and capped at HEALTH_MAX_CONSECUTIVE
// in a row without a stable run resetting it, so a persistent outage causes a few reboots, not a loop -
// after that the outage just shows as stale data on screen (main.cpp) until it clears on its own.
// Every value here is #ifndef-guarded for bench overrides, same as STALL_RESET_MS above.
//
// NET_STUCK: the 2026-09-21 soak caught a real case this exists for - an OpenSky token fetch measured
// taking 120223ms despite HTTPClient being configured with a 15000ms timeout (a known ESP32
// HTTPClient/WiFiClientSecure quirk: the configured timeout is not always honored under some failure
// modes). NetTask::msSinceLoop() only advances between its own outer loop iterations, so a single such
// call - or several chained across a full screen of flights, each individually bounded but not reliably
// so - correctly shows up here even though every per-call timeout looked fine on paper.
#ifndef HEALTH_NET_STUCK_MS
#define HEALTH_NET_STUCK_MS (8UL * 60UL * 1000UL)
#endif
// WiFi configured but not connected for this long.
#ifndef HEALTH_WIFI_DOWN_MS
#define HEALTH_WIFI_DOWN_MS (30UL * 60UL * 1000UL)
#endif
// WiFi up (so NetTask should be able to work) but no successful OpenSky fetch for this long.
#ifndef HEALTH_NO_DATA_MS
#define HEALTH_NO_DATA_MS (20UL * 60UL * 1000UL)
#endif
// Largest free heap block below this for HEALTH_LOW_HEAP_MS. TLS needs ~16KB contiguous blocks.
#ifndef HEALTH_MIN_MAXALLOC
#define HEALTH_MIN_MAXALLOC 24000
#endif
#ifndef HEALTH_LOW_HEAP_MS
#define HEALTH_LOW_HEAP_MS (5UL * 60UL * 1000UL)
#endif
#ifndef HEALTH_MAX_CONSECUTIVE
#define HEALTH_MAX_CONSECUTIVE 3
#endif
// Uptime with at least one good fetch that resets the consecutive-health-reboot count.
#ifndef HEALTH_STABLE_MS
#define HEALTH_STABLE_MS (10UL * 60UL * 1000UL)
#endif

// The network task's own FreeRTOS stack. TLS handshake + HTTPClient + JSON parsing run on it; our own big
// buffers are static, not stack-local (see e.g. OpenSkyClient.cpp) - matches ../mlb-board's NetTask, whose
// TLS/HTTPClient/ArduinoJson usage is comparable. NetTask::stackFreeMin() (in `diag`) reports the real
// high-water mark on hardware.
#define NET_TASK_STACK_BYTES 12288

// While showing the last known flights through an outage (main.cpp), dead-reckoning keeps extrapolating
// from their last real fix - capped at this many seconds so a long outage settles into a fixed, still-
// sane-looking position instead of running away into nonsense (deadReckon() itself already says it isn't
// meant for long extrapolation).
#define DEAD_RECKON_MAX_S (5UL * 60UL)

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
#define OPENSKY_TRACKS_PATH "/api/tracks/all"
#define OPENSKY_TOKEN_URL "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
#define ADSBDB_URL_FMT "https://api.adsbdb.com/v0/callsign/%s"
#define USER_AGENT "Mozilla/5.0 (compatible; cyd_nearby_flights_arduino/1.0)"
#define AERODATABOX_HOST "aerodatabox.p.rapidapi.com"
#define AERODATABOX_URL_FMT "https://aerodatabox.p.rapidapi.com/flights/callsign/%s"

// ---- OAuth2 token cache ----
#define OPENSKY_TOKEN_TTL_MS (25UL * 60UL * 1000UL)

// ---- Rate limiting ----
#define RATE_LIMIT_LOW_WATER 20
#define RATE_LIMIT_BACKOFF_SECONDS 600

// ---- Plain HTTPS retries (adsbdb, OAuth token endpoint) ----
#define HTTPS_RETRIES 4
#define HTTPS_RETRY_DELAY_MS 400

// A single network call slower than this is logged as a [net-t] line even when it succeeds (failures are
// always logged) - see Diag::FetchTimer. Normal calls take ~1-2 s; ../mlb-board measured one taking 57 s
// on this same board and network.
#define SLOW_FETCH_LOG_MS 5000

// ---- Route logic ----
#define CHARTER_OPERATOR_PREFIXES_COUNT 5
extern const char *const CHARTER_OPERATOR_PREFIXES[CHARTER_OPERATOR_PREFIXES_COUNT];
#define ROUTE_FLAG "*"

// How far (degrees) an aircraft's actual heading may diverge from the
// bearing toward its claimed destination before routeIsPlausible() flags
// the route - added after a live case (UA1217, 2026-09-19) where adsbdb
// claimed IAH->BOS for a flight actually cruising over Wisconsin heading
// almost exactly AWAY from Boston (heading 302 deg vs. an 88 deg bearing
// to BOS - a 146 deg mismatch), which the distance-only check missed
// entirely (IAH-BOS is a long enough trip that the Wisconsin detour didn't
// exceed the 50%+150mi slack). Kept generous (90 deg, i.e. "more moving
// away than toward") specifically to tolerate ordinary route curvature -
// SIDs/STARs, wind-optimized routing, ATC vectoring - which can
// legitimately diverge 30-45 deg from a straight bearing but essentially
// never more than that for a real point-to-point commercial flight.
#define HEADING_MISMATCH_THRESHOLD_DEG 90.0f

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
// and the README's "Secondary route provider" section. Backed by
// AeroDataBoxClient (via RapidAPI). Its confirmed "Basic" plan (a genuine
// ongoing free tier, not a trial) caps out at 400 API units / 1,600
// requests per month and 1,000 requests/hour.
//
// The original budget of 60 assumed a worst-case 6 units/request (tiers
// bill 1/2/6) - real cost was confirmed 2026-09-16 by reading RapidAPI's
// own response headers (x-ratelimit-api-units-remaining vs.
// x-ratelimit-requests-remaining) after live usage: ~1.7 units/request
// actual, not 6. At that real rate, 400 units supports roughly 200
// requests/month - 150 leaves a safety margin under that. (That same
// 60-request budget being hit in a single day of active *development*
// testing - not steady-state operation - is exactly what exhausted it
// and caused new/uncached callsigns to silently stop resolving via
// RouteCache until this was diagnosed live on 2026-09-16.)
//
// Separately noted but NOT fixed here: RapidAPI's quota resets on a
// rolling ~30-day window from account creation
// (x-ratelimit-api-units-reset was ~2,555,377s, ~29.6 days, when checked)
// - not the calendar-month boundary budgetAllows() currently assumes
// (`localDay / 100`). A reset near the calendar month's end could
// therefore fire a few days early or late relative to RapidAPI's actual
// billing cycle; low-stakes today since the recalibrated budget has
// headroom either way, but worth an exact fix (store/track the real
// reset epoch) if this ever gets tight again.
//
// ROUTE_PROVIDER_MIN_INTERVAL_MS was originally set to ~3.7s to respect
// the 1,000/hour cap on its own - but that's checked inline in enrich()'s
// tight per-flight loop, so when several distinct flagged callsigns
// needed a fresh lookup in the SAME poll cycle, only the first got
// through and the rest stayed stuck showing adsbdb's flagged/stale route
// for cycle after cycle (confirmed live: AAL1841/EDV4900/DAL1356 all
// legitimately flagged - their current position was nowhere near adsbdb's
// claimed route - but only ever resolved one at a time, one per ~30s
// poll). The monthly budget (60) already caps total volume far below what
// the hourly limit could ever bind on regardless of burst timing, making
// that wide a per-call spacing unnecessary - narrowed to just enough to
// avoid firing requests literally simultaneously.
#define ROUTE_CACHE_SIZE 32
#define ROUTE_CACHE_MAX_LEGS 4
#define ROUTE_PROVIDER_MIN_INTERVAL_MS 500
#define ROUTE_PROVIDER_MONTHLY_BUDGET 150

// A cached leg more than this long past its arrival time (with no other
// cached leg still upcoming/active) is treated as aged-out - RouteCache
// re-checks with the provider rather than keep serving arrived data, but
// never more often than once per callsign per
// ROUTE_CACHE_MIN_REFETCH_INTERVAL_MS (15 min), so a callsign with no new
// leg yet doesn't get hammered every ~30s poll waiting for one. This is a
// per-callsign floor, distinct from ROUTE_PROVIDER_MIN_INTERVAL_MS's
// global inter-request spacing.
#define ROUTE_CACHE_STALE_GRACE_SEC (10UL * 60UL)
#define ROUTE_CACHE_MIN_REFETCH_INTERVAL_MS (15UL * 60UL * 1000UL)

// ---- Departure verification (OpenSky live track vs. claimed origin) ----
// A last-resort check for a flight whose route already looks suspect (see
// FlightMath::routeIsPlausible()) and that RouteCache/AeroDataBox couldn't
// resolve: fetch the aircraft's actual live track from OpenSky and compare
// its first waypoint (real evidence of where it started, per OpenSky's own
// docs - see OpenSkyClient::fetchLiveTrackFirstPoint()) against the
// claimed origin's coordinates. If they disagree, reverse-lookup the
// static AirportTable for the real nearest airport and correct
// Flight::origin/originName to it - see DepartureVerifier.
//
// Kill switch: OpenSky's own docs call /tracks "purely experimental" and
// warn it "can be out of order at any time" - flip this off if that ever
// causes trouble in the field without wanting to touch the rest of the
// route-lookup pipeline.
#define VERIFY_DEPARTURE_VIA_TRACK true

// Unlike AeroDataBox, OpenSky bills /tracks/* against its own credit
// bucket entirely separate from /states/* (confirmed via their docs' "API
// Credits" section: three independent buckets, one per endpoint group) -
// so this doesn't compete with the states/all polling budget at all, and
// needs no analogous monthly cap. Still worth a minimum spacing between
// calls, for the exact same reason ROUTE_PROVIDER_MIN_INTERVAL_MS exists:
// enrich() can flag several distinct callsigns as suspect in one poll
// cycle, and firing this back-to-back for cache misses in a tight loop
// would be needlessly bursty.
#define TRACK_PROVIDER_MIN_INTERVAL_MS 500

// How close (in miles) the track's first waypoint must be to a coordinate
// to call it "the same airport" - used both to decide the claimed origin
// is already correct (no lookup needed) and to accept an AirportTable
// match as the correction. Loose enough to cover a few miles of climb-out
// drift past the runway; tight enough to still usually distinguish
// close-together airports in the same metro area (not guaranteed in
// unusually dense multi-airport metros - a known, accepted limitation of
// this coarse a check).
#define DEPARTURE_VERIFY_MATCH_RADIUS_MI 15.0f

// Per-callsign-per-day cache so an already-resolved verification (whether
// confirmed correct or corrected) isn't re-fetched from OpenSky on every
// ~30s poll for the rest of that flight - mirrors RouteCache's per-day
// caching rationale, just without the multi-leg complexity that needs.
#define DEPARTURE_VERIFY_CACHE_SIZE 16

// ---- Destination verification (position/altitude vs. claimed arrival) ----
// Unlike departure verification, this needs no network call at all - every
// input (current lat/lon/altitude/vertical rate/on-ground state) is
// already fetched every poll, and the claimed destination's own
// coordinates/elevation come from the same static AirportTable
// DepartureVerifier uses. See DestinationVerifier.
//
// A flight already on the ground: its current position IS the landing
// position, a settled fact - if that's far from the claimed destination,
// correct it via AirportTable::findNearest() on the current position.
#define DESTINATION_MATCH_RADIUS_MI 15.0f

// A still-airborne flight below this height above the airport's OWN field
// elevation (not sea level - see AirportEntry::elevationFt) while
// descending (same ALT_TREND_THRESHOLD_FPM used for the on-screen
// climb/descend icon) is very likely on final approach into it - a guess,
// not a settled fact (a go-around/hold could still happen), so a
// correction made this way should render as unconfirmed (see
// Flight::destinationGuessed) rather than being treated like a landed
// correction.
#define DESCENT_GUESS_MAX_AGL_FT 5000.0f
#define DESCENT_GUESS_MAX_RADIUS_MI 20.0f

// ---- Departure/arrival timeline row ----
// Rendered as a fixed-width text bar for a commercial flight with known
// AeroDataBox times: left-justified departure "H:mm", dashes, a '+' at
// the current-time position between departure and arrival, right-
// justified arrival "H:mm". 38 chars * 8px/char = 304px; from the x=4
// left margin that ends at x=308, the same verified-safe right edge
// drawValueLine's speed column already uses on this 320px display.
#define TIMELINE_WIDTH_CHARS 38
