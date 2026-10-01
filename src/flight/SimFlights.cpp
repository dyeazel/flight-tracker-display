#include "SimFlights.h"

#ifdef SIM_DATA

#include <stdio.h>
#include <string.h>

#include "FlightMath.h"
#include "config.h"
#include "config/ConfigStore.h"

namespace SimFlights {

namespace {

const char *const kNames[] = {
    "commercial",   "stale-route",     "origin-corrected", "dest-guessed",  "landed",     "long-names",
    "codes-only",   "no-timeline",     "six-commercial",   "ga-only",       "no-aircraft", "missing-target",
    "error",        "refreshing",      "one-name",         "missing-not-yet-departed", "missing-landed",
};
constexpr int kCount = sizeof(kNames) / sizeof(kNames[0]);
int gHeld = -1;

void copy(char *dst, size_t n, const char *src) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

// Places the flight `distMi` from home on compass bearing `brg` (so the bearing column reads sensibly).
void placeAt(Flight &f, float brg, float distMi) {
    FlightMath::deadReckon(ConfigStore::lat(), ConfigStore::lon(), brg, distMi / 1.15078f, 3600.0f, &f.lat, &f.lon);
    f.distanceMi = distMi;
}

struct Route {
    const char *origin, *originName, *dest, *destName;
};

// A commercial flight. Airline + flight number render on line 1; codes and values on line 2; names on line 3.
Flight &commercial(Flight *fl, int &n, const char *callsign, const char *airline, const Route &r, float brg,
                   float distMi, int altFt, float vrateFpm, float speedKt, long nowUtc, float depH, float arrH) {
    Flight &f = fl[n++];
    f = Flight();
    copy(f.callsign, sizeof(f.callsign), callsign);
    copy(f.airline, sizeof(f.airline), airline);
    copy(f.origin, sizeof(f.origin), r.origin);
    copy(f.destination, sizeof(f.destination), r.dest);
    copy(f.originName, sizeof(f.originName), r.originName);
    copy(f.destinationName, sizeof(f.destinationName), r.destName);
    placeAt(f, brg, distMi);
    f.altitudeFt = altFt;
    f.fixAltitudeFt = altFt;
    f.vrateFpm = vrateFpm;
    f.speedKt = speedKt;
    f.hasHeading = true;
    f.heading = 270;
    if (nowUtc > 0 && arrH != 0) {  // hours relative to now: dep in the past (negative), arr in the future
        f.depUtc = nowUtc + (long)(depH * 3600);
        f.arrUtc = nowUtc + (long)(arrH * 3600);
    }
    return f;
}

Flight &ga(Flight *fl, int &n, const char *callsign, float brg, float distMi, int altFt, float vrateFpm,
           float speedKt) {
    Flight &f = fl[n++];
    f = Flight();
    copy(f.callsign, sizeof(f.callsign), callsign);
    placeAt(f, brg, distMi);
    f.altitudeFt = altFt;
    f.fixAltitudeFt = altFt;
    f.vrateFpm = vrateFpm;
    f.speedKt = speedKt;
    return f;
}

const Route kDenDtw = {"DEN", "Denver Int'l Airport", "DTW", "Detroit Metropolitan Wayne County Airport"};
const Route kIahBos = {"IAH", "George Bush Intercontinental Houston Airport", "BOS", "Logan Int'l Airport"};
const Route kMspOrd = {"MSP", "Minneapolis-Saint Paul Int'l Airport / Wold-Chamberlain Field", "ORD",
                       "Chicago O'Hare Int'l Airport"};
const Route kAtlLax = {"ATL", "Hartsfield Jackson Atlanta Int'l Airport", "LAX", "Los Angeles Int'l Airport"};
const Route kOrdMsp = {"ORD", "Chicago O'Hare Int'l Airport", "MSP", "Minneapolis-Saint Paul Int'l Airport"};

}  // namespace

int count() { return kCount; }
const char *name(int scene) { return (scene >= 0 && scene < kCount) ? kNames[scene] : "?"; }

int current(uint32_t nowMs) { return gHeld >= 0 ? gHeld : (int)((nowMs / kStepMs) % kCount); }
void hold(int n) { gHeld = (n >= 0 && n < kCount) ? n : -1; }

Scene fill(int scene, Flight *fl, int maxFlights, char missing[][12], FlightSelector::MissingStatus *missingStatus,
           time_t *missingWhenUtc, long nowUtc) {
    Scene s;
    int n = 0;
    (void)maxFlights;  // every scene is <= MAX_ROWS (6) by construction
    // missing[]/missingStatus[]/missingWhenUtc[] are parallel arrays the caller sizes to MAX_ROWS (see
    // main.cpp's simMissing/simMissingStatus/simMissingWhen) - NOT CallsignList::kMax (8), which overran
    // both by 2 entries and corrupted whatever static memory followed them (caught live: it didn't crash,
    // it just silently broke FlightScreen's rendering - nothing showed up on any density mode's screen).
    for (int i = 0; i < MAX_ROWS; i++) {
        missingStatus[i] = FlightSelector::MissingStatus::Unknown;
        missingWhenUtc[i] = 0;
    }
    switch (scene) {
        case 0:  // a normal commercial flight, timeline present, plus GA traffic
            commercial(fl, n, "DAL1745", "Delta Air Lines", kDenDtw, 200, 18.7f, 34180, -500, 512, nowUtc, -2.0f, 1.7f);
            ga(fl, n, "N2380M", 110, 7.9f, 1880, 0, 84);
            ga(fl, n, "N441MV", 20, 20.0f, 16510, -400, 300);
            break;
        case 1: {  // UA1217-like: adsbdb route flagged implausible, nothing could correct it -> red '*' on both
            Flight &f = commercial(fl, n, "UAL1217", "United Airlines", kIahBos, 285, 12.0f, 35000, 0, 428, nowUtc, -1.2f, 2.9f);
            strncat(f.origin, ROUTE_FLAG, sizeof(f.origin) - strlen(f.origin) - 1);
            strncat(f.destination, ROUTE_FLAG, sizeof(f.destination) - strlen(f.destination) - 1);
            s.anyFlagged = true;
            break;
        }
        case 2: {  // origin corrected via the live track (gray, no star); destination still unverified (red '*')
            Flight &f = commercial(fl, n, "DAL2969", "Delta Air Lines", kMspOrd, 340, 25.0f, 33000, 0, 471, nowUtc, -0.9f, 1.4f);
            copy(f.origin, sizeof(f.origin), "MSP");
            strncat(f.destination, ROUTE_FLAG, sizeof(f.destination) - strlen(f.destination) - 1);
            s.anyFlagged = true;
            break;
        }
        case 3: {  // low + descending near a different airport than claimed -> destination is a guess (yellow)
            Flight &f = commercial(fl, n, "SKW4136", "SkyWest Airlines", kOrdMsp, 95, 9.5f, 3200, -900, 210, nowUtc, -1.1f, 0.15f);
            f.destinationGuessed = true;
            break;
        }
        case 4:  // landed: on the ground, gray values
            commercial(fl, n, "AAL1481", "American Airlines", kOrdMsp, 45, 3.1f, 850, 0, 14, nowUtc, -1.6f, 0.0f);
            fl[0].onGround = true;
            fl[0].destinationGuessed = false;
            break;
        case 5:  // very long names: truncated to the 17-char per-name budget
            commercial(fl, n, "EDV4972", "Endeavor Air", kMspOrd, 260, 30.0f, 31000, 0, 445, nowUtc, -0.7f, 1.6f);
            commercial(fl, n, "DAL884", "Delta Air Lines", kAtlLax, 300, 41.0f, 36000, 0, 480, nowUtc, -1.5f, 3.4f);
            break;
        case 6: {  // provider gave codes only: no names row (it would just repeat the codes line)
            Route codes = {"ORD", "", "LAX", ""};
            commercial(fl, n, "UAL466", "United Airlines", codes, 240, 15.0f, 37000, 0, 470, nowUtc, -1.0f, 3.2f);
            break;
        }
        case 14: {  // only one name known: the other side falls back to its code
            Route half = {"ORD", "Chicago O'Hare Int'l Airport", "LAX", ""};
            commercial(fl, n, "UAL466", "United Airlines", half, 240, 15.0f, 37000, 0, 470, nowUtc, -1.0f, 3.2f);
            break;
        }
        case 7:  // no dep/arr times known: no timeline row (3 lines instead of 4)
            commercial(fl, n, "SWA2903", "Southwest Airlines", kOrdMsp, 150, 22.0f, 28000, 300, 410, nowUtc, 0, 0);
            ga(fl, n, "N895JH", 310, 30.0f, 13000, 0, 214);
            break;
        case 8:  // a full screen of commercial flights: exercises the row cutoff
            for (int i = 0; i < 6; i++) {
                char cs[12];
                snprintf(cs, sizeof(cs), "DAL%d", 1200 + i * 37);
                commercial(fl, n, cs, "Delta Air Lines", i % 2 ? kAtlLax : kDenDtw, 30 + i * 55, 10.0f + i * 6, 30000 + i * 800,
                           0, 450, nowUtc, -1.0f, 1.0f + i * 0.4f);
            }
            break;
        case 9:  // general aviation only
            ga(fl, n, "N2380M", 110, 7.9f, 1880, 0, 84);
            ga(fl, n, "N441MV", 20, 20.0f, 16510, -400, 300);
            ga(fl, n, "00000000", 250, 20.4f, 17630, 700, 350);
            ga(fl, n, "N6561C", 335, 26.3f, 15800, 0, 194);
            ga(fl, n, "N4727H", 270, 27.2f, 1080, 0, 73);
            break;
        case 10:  // nothing in range
            break;
        case 11:  // a tracked callsign that isn't in range, and no schedule data is known for it either
            copy(missing[0], 12, "UAL1217");
            s.missingCount = 1;
            commercial(fl, n, "DAL1745", "Delta Air Lines", kDenDtw, 200, 18.7f, 34180, -500, 512, nowUtc, -2.0f, 1.7f);
            break;
        case 12:  // the fetch failed
            s.error = true;
            break;
        case 13:  // a real poll is in flight: yellow heartbeat over an otherwise normal screen
            commercial(fl, n, "DAL1745", "Delta Air Lines", kDenDtw, 200, 18.7f, 34180, -500, 512, nowUtc, -2.0f, 1.7f);
            s.refreshing = true;
            break;
        case 15:  // Phase 8: a tracked flight with a known future departure - "departs" status, not "not found"
            copy(missing[0], 12, "UAL1217");
            missingStatus[0] = FlightSelector::MissingStatus::NotYetDeparted;
            missingWhenUtc[0] = nowUtc > 0 ? nowUtc + 5400 : 0;  // 1h30m from now
            s.missingCount = 1;
            commercial(fl, n, "DAL1745", "Delta Air Lines", kDenDtw, 200, 18.7f, 34180, -500, 512, nowUtc, -2.0f, 1.7f);
            break;
        case 16:  // Phase 8: a tracked flight that already landed - "landed" status, not "not found"
            copy(missing[0], 12, "UAL1217");
            missingStatus[0] = FlightSelector::MissingStatus::Landed;
            missingWhenUtc[0] = nowUtc > 0 ? nowUtc - 3600 : 0;  // 1h ago
            s.missingCount = 1;
            commercial(fl, n, "DAL1745", "Delta Air Lines", kDenDtw, 200, 18.7f, 34180, -500, 512, nowUtc, -2.0f, 1.7f);
            break;
        default:
            break;
    }
    s.count = n;
    return s;
}

}  // namespace SimFlights

#endif  // SIM_DATA
