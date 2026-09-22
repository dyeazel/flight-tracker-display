#include <string.h>
#include <unity.h>

#include "data/AirportTable.h"

void setUp() {}
void tearDown() {}

void test_find_by_code_known_airport() {
    const AirportTable::AirportEntry *e = AirportTable::findByCode("ORD");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("ORD", e->code);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 41.9786f, e->lat);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -87.9048f, e->lon);
}

void test_find_by_code_unknown_returns_null() {
    TEST_ASSERT_NULL(AirportTable::findByCode("ZZZ_NOT_REAL"));
}

void test_find_by_code_empty_or_null_returns_null() {
    TEST_ASSERT_NULL(AirportTable::findByCode(""));
    TEST_ASSERT_NULL(AirportTable::findByCode(nullptr));
}

void test_find_by_code_is_case_sensitive() {
    // Table codes are consistently upper-case (adsbdb/AeroDataBox convention) - a lower-case query
    // should not match, per findByCode()'s own doc comment.
    TEST_ASSERT_NULL(AirportTable::findByCode("ord"));
}

void test_find_nearest_returns_the_airport_itself_at_its_own_coordinates() {
    const AirportTable::AirportEntry *ord = AirportTable::findByCode("ORD");
    TEST_ASSERT_NOT_NULL(ord);
    const AirportTable::AirportEntry *nearest = AirportTable::findNearest(ord->lat, ord->lon, 5.0f);
    TEST_ASSERT_NOT_NULL(nearest);
    TEST_ASSERT_EQUAL_STRING("ORD", nearest->code);
}

void test_find_nearest_beyond_radius_returns_null() {
    // The middle of the Pacific Ocean, far from any airport in the table.
    TEST_ASSERT_NULL(AirportTable::findNearest(0.0f, -160.0f, 50.0f));
}

void test_find_nearest_picks_closer_of_two_known_airports() {
    // A point roughly between ORD and MSP, but much closer to ORD.
    const AirportTable::AirportEntry *ord = AirportTable::findByCode("ORD");
    TEST_ASSERT_NOT_NULL(ord);
    const AirportTable::AirportEntry *nearest = AirportTable::findNearest(ord->lat + 0.05f, ord->lon + 0.05f, 20.0f);
    TEST_ASSERT_NOT_NULL(nearest);
    TEST_ASSERT_EQUAL_STRING("ORD", nearest->code);
}

void test_table_is_populated() {
    TEST_ASSERT_TRUE(AirportTable::kAirportCount > 1000);  // ~3,269 entries per AirportTable.h's comment
    TEST_ASSERT_NOT_NULL(AirportTable::kAirportTable);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_find_by_code_known_airport);
    RUN_TEST(test_find_by_code_unknown_returns_null);
    RUN_TEST(test_find_by_code_empty_or_null_returns_null);
    RUN_TEST(test_find_by_code_is_case_sensitive);
    RUN_TEST(test_find_nearest_returns_the_airport_itself_at_its_own_coordinates);
    RUN_TEST(test_find_nearest_beyond_radius_returns_null);
    RUN_TEST(test_find_nearest_picks_closer_of_two_known_airports);
    RUN_TEST(test_table_is_populated);
    return UNITY_END();
}
