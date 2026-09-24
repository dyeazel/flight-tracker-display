#include <math.h>
#include <unity.h>

#include "config.h"
#include "flight/FlightMath.h"

void setUp() {}
void tearDown() {}

// ---- haversineMiles ----

void test_haversine_same_point_is_zero() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, FlightMath::haversineMiles(41.9786f, -87.9048f, 41.9786f, -87.9048f));
}

void test_haversine_symmetric() {
    float ab = FlightMath::haversineMiles(41.9786f, -87.9048f, 42.36197f, -71.0079f);   // ORD -> BOS
    float ba = FlightMath::haversineMiles(42.36197f, -71.0079f, 41.9786f, -87.9048f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, ab, ba);
}

// ORD-BOS great-circle distance is a well-known ~867 statute miles.
void test_haversine_ord_bos_known_distance() {
    float mi = FlightMath::haversineMiles(41.9786f, -87.9048f, 42.36197f, -71.0079f);
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 867.0f, mi);
}

// ---- bearingDeg ----

void test_bearing_cardinal_directions() {
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, FlightMath::bearingDeg(0, 0, 1, 0));      // due north
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 90.0f, FlightMath::bearingDeg(0, 0, 0, 1));     // due east
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 180.0f, FlightMath::bearingDeg(0, 0, -1, 0));   // due south
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 270.0f, FlightMath::bearingDeg(0, 0, 0, -1));   // due west
}

void test_bearing_always_in_0_360() {
    float b = FlightMath::bearingDeg(41.9786f, -87.9048f, 39.860027f, -104.673792f);  // ORD -> DEN
    TEST_ASSERT_TRUE(b >= 0.0f && b < 360.0f);
}

// ---- deadReckon ----

void test_dead_reckon_heading_north_increases_lat_only() {
    float outLat, outLon;
    FlightMath::deadReckon(41.0f, -87.0f, /*headingDeg=*/0.0f, /*speedKt=*/120.0f, /*elapsedS=*/60.0f,
                            &outLat, &outLon);
    TEST_ASSERT_TRUE(outLat > 41.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -87.0f, outLon);  // due north: longitude essentially unchanged
}

void test_dead_reckon_matches_distance_traveled() {
    // 120kt for 30 minutes = 60 nautical miles = 69.0 statute miles.
    float outLat, outLon;
    FlightMath::deadReckon(41.0f, -87.0f, 0.0f, 120.0f, 1800.0f, &outLat, &outLon);
    float traveled = FlightMath::haversineMiles(41.0f, -87.0f, outLat, outLon);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 69.0f, traveled);
}

// ---- isScheduledCallsign ----

void test_scheduled_callsign_recognized() {
    TEST_ASSERT_TRUE(FlightMath::isScheduledCallsign("UAL1217"));
    TEST_ASSERT_TRUE(FlightMath::isScheduledCallsign("DAL123"));
}

void test_scheduled_callsign_rejects_short_or_non_letter_prefix() {
    TEST_ASSERT_FALSE(FlightMath::isScheduledCallsign("AB1"));      // too short
    TEST_ASSERT_FALSE(FlightMath::isScheduledCallsign("N12345"));   // tail number, not 3 letters + digit
    TEST_ASSERT_FALSE(FlightMath::isScheduledCallsign(""));
}

void test_scheduled_callsign_rejects_charter_prefix() {
    TEST_ASSERT_FALSE(FlightMath::isScheduledCallsign("EJA123"));  // NetJets, in CHARTER_OPERATOR_PREFIXES
}

// ---- cardinalDirection ----

void test_cardinal_direction_matches_16_point_compass() {
    TEST_ASSERT_EQUAL_STRING("N", FlightMath::cardinalDirection(0.0f));
    TEST_ASSERT_EQUAL_STRING("E", FlightMath::cardinalDirection(90.0f));
    TEST_ASSERT_EQUAL_STRING("S", FlightMath::cardinalDirection(180.0f));
    TEST_ASSERT_EQUAL_STRING("W", FlightMath::cardinalDirection(270.0f));
    TEST_ASSERT_EQUAL_STRING("NE", FlightMath::cardinalDirection(45.0f));
    TEST_ASSERT_EQUAL_STRING("N", FlightMath::cardinalDirection(360.0f));
    TEST_ASSERT_EQUAL_STRING("N", FlightMath::cardinalDirection(-1.0f));  // wraps negative
}

// ---- routeIsPlausible ----

// The real case that added the heading check (see config.h's HEADING_MISMATCH_THRESHOLD_DEG comment):
// UA1217, 2026-09-19, adsbdb claimed IAH->BOS while the aircraft was actually cruising over Wisconsin
// heading 302 deg - almost exactly away from Boston (bearing ~88 deg from that position, a ~146 deg
// mismatch). The distance-only check alone missed this (IAH-BOS is long enough that the Wisconsin
// detour didn't blow the 50%+150mi slack) - this is the regression test for that miss.
void test_route_implausible_heading_away_from_destination() {
    const float iahLat = 29.9844f, iahLon = -95.3414f;
    const float bosLat = 42.36197f, bosLon = -71.0079f;
    const float curLat = 43.184601f, curLon = -88.990422f;  // over Wisconsin (this project's HOME_LAT/LON)
    bool plausible = FlightMath::routeIsPlausible(curLat, curLon, /*hasHeading=*/true, /*heading=*/302.0f,
                                                    /*speedKt=*/450.0f, iahLat, iahLon, bosLat, bosLon);
    TEST_ASSERT_FALSE(plausible);
}

// Same route, but heading genuinely toward the destination (computed via bearingDeg itself, so this is
// self-consistently "on course") - a real IAH->BOS flight partway along its great-circle path should not
// be flagged.
void test_route_plausible_heading_toward_destination() {
    const float iahLat = 29.9844f, iahLon = -95.3414f;
    const float bosLat = 42.36197f, bosLon = -71.0079f;
    // A point roughly on the great-circle path partway between the two (simple midpoint approximation is
    // fine here - it's close enough to the real path for the distance-slack check to pass easily).
    const float curLat = (iahLat + bosLat) / 2.0f, curLon = (iahLon + bosLon) / 2.0f;
    float headingTowardDest = FlightMath::bearingDeg(curLat, curLon, bosLat, bosLon);
    bool plausible = FlightMath::routeIsPlausible(curLat, curLon, /*hasHeading=*/true, headingTowardDest,
                                                    /*speedKt=*/450.0f, iahLat, iahLon, bosLat, bosLon);
    TEST_ASSERT_TRUE(plausible);
}

// A big detour that fails on distance alone, independent of heading (hasHeading=false skips that check
// entirely - this exercises the original, pre-heading-check code path).
void test_route_implausible_on_distance_alone() {
    const float ordLat = 41.9786f, ordLon = -87.9048f;
    const float denLat = 39.860027f, denLon = -104.673792f;
    const float bosLat = 42.36197f, bosLon = -71.0079f;  // wildly out of the way for ORD->DEN
    bool plausible = FlightMath::routeIsPlausible(bosLat, bosLon, /*hasHeading=*/false, 0.0f, 0.0f, ordLat,
                                                    ordLon, denLat, denLon);
    TEST_ASSERT_FALSE(plausible);
}

// No heading data (e.g. OpenSky gave none): only the distance check applies, and a short regional hop
// with ordinary vectoring should still pass.
void test_route_plausible_no_heading_data() {
    const float ordLat = 41.9786f, ordLon = -87.9048f;
    const float mspLat = 44.880081f, mspLon = -93.221741f;
    bool plausible = FlightMath::routeIsPlausible(43.0f, -90.5f, /*hasHeading=*/false, 0.0f, 0.0f, ordLat,
                                                    ordLon, mspLat, mspLon);
    TEST_ASSERT_TRUE(plausible);
}

// Heading check is skipped below MIN_DEAD_RECKON_KT (too slow for heading to mean anything) - a wildly
// wrong heading at low speed must not flag an otherwise-fine route.
void test_route_plausible_heading_ignored_below_min_speed() {
    const float ordLat = 41.9786f, ordLon = -87.9048f;
    const float mspLat = 44.880081f, mspLon = -93.221741f;
    bool plausible = FlightMath::routeIsPlausible(43.0f, -90.5f, /*hasHeading=*/true, /*heading=*/180.0f,
                                                    /*speedKt=*/5.0f, ordLat, ordLon, mspLat, mspLon);
    TEST_ASSERT_TRUE(plausible);
}

// ---- effectiveRadius (Phase 7: takes ConfigStore's values as parameters, not compile-time macros) ----

void test_effective_radius_uses_override_when_set() {
    TEST_ASSERT_EQUAL_FLOAT(75.0f, FlightMath::effectiveRadius(75.0f, 0));
}

void test_effective_radius_defaults_when_auto_and_no_targets() {
    TEST_ASSERT_EQUAL_FLOAT(DEFAULT_RADIUS_MI, FlightMath::effectiveRadius(RADIUS_MI_AUTO, 0));
}

void test_effective_radius_widens_when_auto_and_targets_configured() {
    TEST_ASSERT_EQUAL_FLOAT(CALLSIGN_SEARCH_RADIUS_MI, FlightMath::effectiveRadius(RADIUS_MI_AUTO, 3));
}

void test_effective_radius_override_wins_even_with_targets_configured() {
    TEST_ASSERT_EQUAL_FLOAT(20.0f, FlightMath::effectiveRadius(20.0f, 3));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_haversine_same_point_is_zero);
    RUN_TEST(test_haversine_symmetric);
    RUN_TEST(test_haversine_ord_bos_known_distance);
    RUN_TEST(test_bearing_cardinal_directions);
    RUN_TEST(test_bearing_always_in_0_360);
    RUN_TEST(test_dead_reckon_heading_north_increases_lat_only);
    RUN_TEST(test_dead_reckon_matches_distance_traveled);
    RUN_TEST(test_scheduled_callsign_recognized);
    RUN_TEST(test_scheduled_callsign_rejects_short_or_non_letter_prefix);
    RUN_TEST(test_scheduled_callsign_rejects_charter_prefix);
    RUN_TEST(test_cardinal_direction_matches_16_point_compass);
    RUN_TEST(test_route_implausible_heading_away_from_destination);
    RUN_TEST(test_route_plausible_heading_toward_destination);
    RUN_TEST(test_route_implausible_on_distance_alone);
    RUN_TEST(test_route_plausible_no_heading_data);
    RUN_TEST(test_route_plausible_heading_ignored_below_min_speed);
    RUN_TEST(test_effective_radius_uses_override_when_set);
    RUN_TEST(test_effective_radius_defaults_when_auto_and_no_targets);
    RUN_TEST(test_effective_radius_widens_when_auto_and_targets_configured);
    RUN_TEST(test_effective_radius_override_wins_even_with_targets_configured);
    return UNITY_END();
}
