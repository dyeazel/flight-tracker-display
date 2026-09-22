#include <ArduinoJson.h>
#include <unity.h>

#include "network/AeroDataBoxParse.h"

void setUp() {}
void tearDown() {}

// Not a live-captured payload: AeroDataBox is billed against a tightly-tracked monthly RapidAPI budget
// (see config.h's ROUTE_PROVIDER_MONTHLY_BUDGET comment on a real past incident from spending it on
// development testing) - not worth spending a real call just for a test fixture. Shaped to exactly match
// the field names AeroDataBoxClient.h documents as confirmed against real live responses (2026-09-16/17):
// departure/arrival.airport.{iata,icao,name,location.{lat,lon}}, {runwayTime,...}.utc,
// airline.{name,iata,icao} - and the real two-leg-per-callsign shape that comment also documents
// (UAL1283 -> DEN->EWR, EWR->LAS).
static const char *kTwoLegs = R"JSON([
{"departure":{"airport":{"iata":"DEN","icao":"KDEN","name":"Denver","location":{"lat":39.861698,"lon":-104.673164}},
              "scheduledTime":{"utc":"2026-09-20T14:00Z"},"runwayTime":{"utc":"2026-09-20T14:12Z"}},
 "arrival":{"airport":{"iata":"EWR","icao":"KEWR","name":"Newark Liberty","location":{"lat":40.6925,"lon":-74.168667}},
            "scheduledTime":{"utc":"2026-09-20T19:45Z"}},
 "airline":{"name":"United Airlines","iata":"UA","icao":"UAL"}},
{"departure":{"airport":{"iata":"EWR","icao":"KEWR","name":"Newark Liberty","location":{"lat":40.6925,"lon":-74.168667}},
              "scheduledTime":{"utc":"2026-09-20T21:00Z"}},
 "arrival":{"airport":{"iata":"LAS","icao":"KLAS","name":"Las Vegas","location":{"lat":36.08,"lon":-115.152222}},
            "revisedTime":{"utc":"2026-09-21T00:20Z"}},
 "airline":{"name":"United Airlines","iata":"UA","icao":"UAL"}}
])JSON";

// The real gotcha documented in AeroDataBoxClient.cpp's pickTime() comment: confirmed live 2026-09-15
// that some movements carry ONLY predictedTime, none of runwayTime/revisedTime/scheduledTime - an
// earlier version of the time-picking list omitted it and silently left arrUtc at 0 for this case.
static const char *kPredictedTimeOnly = R"JSON([
{"departure":{"airport":{"iata":"ORD","icao":"KORD","name":"Chicago O'Hare","location":{"lat":41.9786,"lon":-87.9048}}},
 "arrival":{"airport":{"iata":"LAX","icao":"KLAX","name":"Los Angeles","location":{"lat":33.9425,"lon":-118.408056}},
            "predictedTime":{"utc":"2026-09-20T22:15Z"}},
 "airline":{"name":"Republic Airways","iata":"","icao":"RPA"}}
])JSON";

static const char *kSingleObject = R"JSON(
{"departure":{"airport":{"iata":"MSP","icao":"KMSP","name":"Minneapolis-Saint Paul","location":{"lat":44.882,"lon":-93.222}}},
 "arrival":{"airport":{"iata":"ORD","icao":"KORD","name":"Chicago O'Hare","location":{"lat":41.9786,"lon":-87.9048}},
            "scheduledTime":{"utc":"2026-09-20T10:00Z"}},
 "airline":{"name":"American Airlines","iata":"AA","icao":"AAL"}}
)JSON";

static const char *kEmptyArray = "[]";

void test_parses_two_legs_in_document_order() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kTwoLegs));
    RouteLeg legs[4];
    int count = 0;
    TEST_ASSERT_TRUE(AeroDataBoxParse::parseLegs(doc.as<JsonVariant>(), legs, 4, &count));
    TEST_ASSERT_EQUAL(2, count);

    TEST_ASSERT_EQUAL_STRING("DEN", legs[0].originCode);
    TEST_ASSERT_EQUAL_STRING("EWR", legs[0].destinationCode);
    TEST_ASSERT_TRUE(legs[0].haveOriginCoords);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 39.861698f, legs[0].originLat);
    // runwayTime (actual) beats scheduledTime - see pickTime()'s priority order.
    TEST_ASSERT_EQUAL_INT64(1789913520, (long long)legs[0].depUtc);  // 2026-09-20T14:12:00Z
    TEST_ASSERT_EQUAL_STRING("UA", legs[0].airlineCode);

    TEST_ASSERT_EQUAL_STRING("EWR", legs[1].originCode);
    TEST_ASSERT_EQUAL_STRING("LAS", legs[1].destinationCode);
    // revisedTime beats scheduledTime when runwayTime is absent.
    TEST_ASSERT_EQUAL_INT64(1789950000, (long long)legs[1].arrUtc);  // 2026-09-21T00:20:00Z
}

void test_respects_max_legs_cap() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kTwoLegs));
    RouteLeg legs[1];
    int count = 0;
    TEST_ASSERT_TRUE(AeroDataBoxParse::parseLegs(doc.as<JsonVariant>(), legs, 1, &count));
    TEST_ASSERT_EQUAL(1, count);
}

void test_predicted_time_only_still_resolves_arrival_time() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kPredictedTimeOnly));
    RouteLeg legs[2];
    int count = 0;
    TEST_ASSERT_TRUE(AeroDataBoxParse::parseLegs(doc.as<JsonVariant>(), legs, 2, &count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_EQUAL(0, (long long)legs[0].arrUtc);
    TEST_ASSERT_EQUAL_INT64(1789942500, (long long)legs[0].arrUtc);  // 2026-09-20T22:15:00Z
}

void test_single_object_root_is_accepted() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kSingleObject));
    RouteLeg legs[2];
    int count = 0;
    TEST_ASSERT_TRUE(AeroDataBoxParse::parseLegs(doc.as<JsonVariant>(), legs, 2, &count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_STRING("MSP", legs[0].originCode);
    TEST_ASSERT_EQUAL_STRING("ORD", legs[0].destinationCode);
}

void test_empty_array_yields_no_legs() {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, kEmptyArray));
    RouteLeg legs[2];
    int count = 0;
    TEST_ASSERT_FALSE(AeroDataBoxParse::parseLegs(doc.as<JsonVariant>(), legs, 2, &count));
    TEST_ASSERT_EQUAL(0, count);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parses_two_legs_in_document_order);
    RUN_TEST(test_respects_max_legs_cap);
    RUN_TEST(test_predicted_time_only_still_resolves_arrival_time);
    RUN_TEST(test_single_object_root_is_accepted);
    RUN_TEST(test_empty_array_yields_no_legs);
    return UNITY_END();
}
