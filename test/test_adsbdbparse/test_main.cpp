#include <ArduinoJson.h>
#include <unity.h>

#include "network/AdsbdbParse.h"

void setUp() {}
void tearDown() {}

// Real response captured live from api.adsbdb.com/v0/callsign/UAL1217 (2026-09-22) - this is the exact
// callsign involved in the live route-plausibility bug this project's heading-consistency check exists
// for (see config.h's HEADING_MISMATCH_THRESHOLD_DEG comment and test_flightmath's
// test_route_implausible_heading_away_from_destination): adsbdb's static table claims IAH->BOS for it.
static const char *kUal1217 =
    R"JSON({"response":{"flightroute":{"callsign":"UAL1217","callsign_icao":"UAL1217","callsign_iata":"UA1217",)JSON"
    R"JSON("airline":{"name":"United Airlines","icao":"UAL","iata":"UA","country":"United States","country_iso":"US","callsign":"UNITED"},)JSON"
    R"JSON("origin":{"country_iso_name":"US","country_name":"United States","elevation":97,"iata_code":"IAH","icao_code":"KIAH",)JSON"
    R"JSON("latitude":29.984399795532227,"longitude":-95.34140014648438,"municipality":"Houston","name":"George Bush Intercontinental Houston Airport"},)JSON"
    R"JSON("destination":{"country_iso_name":"US","country_name":"United States","elevation":20,"iata_code":"BOS","icao_code":"KBOS",)JSON"
    R"JSON("latitude":42.3643,"longitude":-71.005203,"municipality":"Boston","name":"Logan International Airport"}}}})JSON";

// adsbdb's definitive "no route on file" answer for an unrecognized/private callsign.
static const char *kUnknownCallsign = R"JSON({"response":"unknown callsign"})JSON";

void test_parses_real_ual1217_response() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kUal1217));

    AdsbdbClient::RouteInfo info;
    TEST_ASSERT_TRUE(AdsbdbParse::parseRoute(doc.as<JsonVariant>(), &info));

    TEST_ASSERT_EQUAL_STRING("United Airlines", info.airline);
    TEST_ASSERT_EQUAL_STRING("UA", info.airlineCode);  // iata preferred over icao ("UAL")

    TEST_ASSERT_EQUAL_STRING("IAH", info.originCode);
    // Truncated to RouteInfo::originName's 40-byte buffer (39 chars + nul) - the real name is 44 chars.
    TEST_ASSERT_EQUAL_STRING("George Bush Intercontinental Houston Ai", info.originName);
    TEST_ASSERT_TRUE(info.haveOriginCoords);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 29.9844f, info.originLat);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -95.3414f, info.originLon);

    TEST_ASSERT_EQUAL_STRING("BOS", info.destinationCode);
    // "International" -> "Int'l" via Ascii::sanitize (see test_ascii) - real name was "Logan
    // International Airport".
    TEST_ASSERT_EQUAL_STRING("Logan Int'l Airport", info.destinationName);
    TEST_ASSERT_TRUE(info.haveDestCoords);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.3643f, info.destLat);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -71.005203f, info.destLon);
}

void test_unknown_callsign_returns_false() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kUnknownCallsign));

    AdsbdbClient::RouteInfo info;
    TEST_ASSERT_FALSE(AdsbdbParse::parseRoute(doc.as<JsonVariant>(), &info));
    TEST_ASSERT_FALSE(info.haveOriginCoords);
    TEST_ASSERT_FALSE(info.haveDestCoords);
}

void test_missing_response_key_returns_false() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, "{}"));
    AdsbdbClient::RouteInfo info;
    TEST_ASSERT_FALSE(AdsbdbParse::parseRoute(doc.as<JsonVariant>(), &info));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parses_real_ual1217_response);
    RUN_TEST(test_unknown_callsign_returns_false);
    RUN_TEST(test_missing_response_key_returns_false);
    return UNITY_END();
}
