#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "network/OpenSkyParse.h"

void setUp() {}
void tearDown() {}

// Real response captured live from opensky-network.org/api/states/all (2026-09-22), a box around
// O'Hare (~100mi from this project's HOME_LAT/HOME_LON) - includes real ground traffic (on_ground=true,
// null baro_altitude) and real airborne traffic, and every real callsign here happens to carry OpenSky's
// usual trailing-space padding, exercising the trim path with genuine data rather than a synthetic case.
// Two rows appended after it are deliberately synthetic edge cases (malformed/short row, missing
// lat/lon) to cover paths the live capture didn't happen to exercise.
static const char *kStatesNearOrd = R"JSON({"time":1790101409,"states":[
["adb7ee","SKW4740 ","United States",1790101278,1790101278,-87.9274,41.9715,null,true,9.26,101.25,null,null,null,null,false,0],
["accd13","AAL2302 ","United States",1790101307,1790101307,-87.8986,41.9738,null,true,0,326.25,null,null,null,null,false,0],
["aa0909","ENY4275 ","United States",1790101409,1790101409,-87.8384,41.7715,2247.9,false,144.09,149.77,13,null,2362.2,null,false,0],
["aa2b21","RPA3512 ","United States",1790101408,1790101409,-87.8563,41.841,1417.32,false,140.44,180.21,-0.65,null,1508.76,null,false,0],
["a36661","AAL1985 ","United States",1790101409,1790101409,-88.0249,41.954,541.02,false,77.25,87.33,-4.55,null,624.84,"2266",false,0],
["short01","N/A",-87.9,41.9,null,true,0,0,0],
["missingpos","MISSING1","United States",0,0,null,null,null,true,0,0,0,0,0,0,false,0]
]})JSON";

static const char *kNoStates = R"JSON({"time":1790101409,"states":null})JSON";

void test_parses_real_states_sorted_by_distance_ascending() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kStatesNearOrd));

    Flight out[10];
    int count = 0;
    OpenSkyParse::parseStates(doc.as<JsonVariant>(), /*radiusMi=*/150.0f, out, 10, &count);

    TEST_ASSERT_EQUAL(5, count);  // the short row and the missing-lat/lon row are both skipped
    // Closest-first: AAL1985 (98.2mi) < SKW4740 (99.8) < AAL2302 (100.4) < RPA3512 (109.3) < ENY4275 (113.9).
    TEST_ASSERT_EQUAL_STRING("AAL1985", out[0].callsign);
    TEST_ASSERT_EQUAL_STRING("SKW4740", out[1].callsign);
    TEST_ASSERT_EQUAL_STRING("AAL2302", out[2].callsign);
    TEST_ASSERT_EQUAL_STRING("RPA3512", out[3].callsign);
    TEST_ASSERT_EQUAL_STRING("ENY4275", out[4].callsign);
    for (int i = 1; i < count; i++) TEST_ASSERT_TRUE(out[i].distanceMi >= out[i - 1].distanceMi);
}

void test_callsign_trailing_space_trimmed() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kStatesNearOrd));
    Flight out[10];
    int count = 0;
    OpenSkyParse::parseStates(doc.as<JsonVariant>(), 150.0f, out, 10, &count);
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_TRUE(out[i].callsign[strlen(out[i].callsign) - 1] != ' ');
    }
}

void test_radius_filters_out_farther_flights() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kStatesNearOrd));
    Flight out[10];
    int count = 0;
    // 105mi excludes RPA3512 (109.3mi) and ENY4275 (113.9mi), keeps the other three.
    OpenSkyParse::parseStates(doc.as<JsonVariant>(), 105.0f, out, 10, &count);
    TEST_ASSERT_EQUAL(3, count);
    for (int i = 0; i < count; i++) TEST_ASSERT_TRUE(out[i].distanceMi <= 105.0f);
}

void test_max_out_caps_result_count_to_closest() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kStatesNearOrd));
    Flight out[2];
    int count = 0;
    OpenSkyParse::parseStates(doc.as<JsonVariant>(), 150.0f, out, 2, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL_STRING("AAL1985", out[0].callsign);  // the two closest, still ascending
    TEST_ASSERT_EQUAL_STRING("SKW4740", out[1].callsign);
}

void test_unit_conversions_match_known_row() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kStatesNearOrd));
    Flight out[10];
    int count = 0;
    OpenSkyParse::parseStates(doc.as<JsonVariant>(), 150.0f, out, 10, &count);
    const Flight *eny = nullptr;
    for (int i = 0; i < count; i++) {
        if (strcmp(out[i].callsign, "ENY4275") == 0) eny = &out[i];
    }
    TEST_ASSERT_NOT_NULL(eny);
    TEST_ASSERT_EQUAL_STRING("aa0909", eny->icao24);
    TEST_ASSERT_FALSE(eny->onGround);
    TEST_ASSERT_TRUE(eny->hasHeading);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 149.77f, eny->heading);
    TEST_ASSERT_INT_WITHIN(1, 7375, eny->altitudeFt);     // baro_altitude(m) -> ft
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 280.1f, eny->speedKt);  // velocity(m/s) -> kt
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 2559.0f, eny->vrateFpm);  // vertical_rate(m/s) -> fpm
}

void test_on_ground_row_has_no_altitude_but_can_still_have_ground_track() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kStatesNearOrd));
    Flight out[10];
    int count = 0;
    OpenSkyParse::parseStates(doc.as<JsonVariant>(), 150.0f, out, 10, &count);
    const Flight *skw = nullptr;
    for (int i = 0; i < count; i++) {
        if (strcmp(out[i].callsign, "SKW4740") == 0) skw = &out[i];
    }
    TEST_ASSERT_NOT_NULL(skw);
    TEST_ASSERT_TRUE(skw->onGround);
    // true_track (index 10) is 101.25 in the real capture - an aircraft taxiing/rolling on the ground
    // can still report a ground track, distinct from baro_altitude (index 7), which is null here.
    TEST_ASSERT_TRUE(skw->hasHeading);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 101.25f, skw->heading);
    TEST_ASSERT_EQUAL(0, skw->altitudeFt);  // baro_altitude null -> 0, per fetchOnce()'s "baroAlt ? .. : 0"
}

void test_null_states_yields_zero_flights() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kNoStates));
    Flight out[10];
    int count = -1;
    OpenSkyParse::parseStates(doc.as<JsonVariant>(), 150.0f, out, 10, &count);
    TEST_ASSERT_EQUAL(0, count);
}

// ---- parseTrackFirstPoint ----

// Shaped per OpenSky's documented /tracks waypoint schema (time, lat, lon, baro_altitude, true_track,
// on_ground) - not a live capture: a real request against a specific icao24/time=0 needs that aircraft
// to have a track on file right now, which isn't reproducible as a fixed test fixture.
static const char *kTrackWithWaypoints =
    R"JSON({"icao24":"a36661","callsign":"AAL1985 ","startTime":1790100000,"endTime":1790101500,)JSON"
    R"JSON("path":[[1790100000,29.9844,-95.3414,97.0,45.0,true],[1790100300,30.5,-95.0,2000.0,50.0,false]]})JSON";

static const char *kTrackNoPath = R"JSON({"icao24":"a36661","path":[]})JSON";
static const char *kTrackNotFound = "null";

void test_track_first_point_parses_first_waypoint() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kTrackWithWaypoints));
    float lat = 0, lon = 0;
    TEST_ASSERT_TRUE(OpenSkyParse::parseTrackFirstPoint(doc.as<JsonVariant>(), &lat, &lon));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 29.9844f, lat);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -95.3414f, lon);
}

void test_track_empty_path_returns_false() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kTrackNoPath));
    float lat = 0, lon = 0;
    TEST_ASSERT_FALSE(OpenSkyParse::parseTrackFirstPoint(doc.as<JsonVariant>(), &lat, &lon));
}

void test_track_null_document_returns_false() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kTrackNotFound));
    float lat = 0, lon = 0;
    TEST_ASSERT_FALSE(OpenSkyParse::parseTrackFirstPoint(doc.as<JsonVariant>(), &lat, &lon));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parses_real_states_sorted_by_distance_ascending);
    RUN_TEST(test_callsign_trailing_space_trimmed);
    RUN_TEST(test_radius_filters_out_farther_flights);
    RUN_TEST(test_max_out_caps_result_count_to_closest);
    RUN_TEST(test_unit_conversions_match_known_row);
    RUN_TEST(test_on_ground_row_has_no_altitude_but_can_still_have_ground_track);
    RUN_TEST(test_null_states_yields_zero_flights);
    RUN_TEST(test_track_first_point_parses_first_waypoint);
    RUN_TEST(test_track_empty_path_returns_false);
    RUN_TEST(test_track_null_document_returns_false);
    return UNITY_END();
}
