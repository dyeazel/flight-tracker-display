#include <unity.h>

#include "time/Iso8601.h"

void setUp() {}
void tearDown() {}

// Deliberately does NOT setenv("TZ", ...) around these calls - Iso8601::parseUtc computes days-since-
// epoch directly (Howard Hinnant's days_from_civil), with no dependency on libc timezone globals, per
// its own header comment. If a future change accidentally routed this through mktime()/localtime(), a
// non-UTC TZ in the test environment would shift these results and catch the regression.

void test_parse_with_seconds_and_z() {
    time_t t = 0;
    TEST_ASSERT_TRUE(Iso8601::parseUtc("2026-09-20T01:10:00Z", &t));
    TEST_ASSERT_EQUAL_INT64(1789866600, (long long)t);
}

void test_parse_without_seconds() {
    // AeroDataBox has been observed to omit seconds in some fields (Iso8601.h's doc comment).
    time_t t = 0;
    TEST_ASSERT_TRUE(Iso8601::parseUtc("2026-09-20T01:10Z", &t));
    TEST_ASSERT_EQUAL_INT64(1789866600, (long long)t);
}

void test_parse_with_space_separator_no_z() {
    // AeroDataBox has also been observed to vary the date/time separator and trailing Z presence.
    time_t t = 0;
    TEST_ASSERT_TRUE(Iso8601::parseUtc("2026-09-20 01:10:00", &t));
    TEST_ASSERT_EQUAL_INT64(1789866600, (long long)t);
}

void test_parse_year_boundaries() {
    time_t t = 0;
    TEST_ASSERT_TRUE(Iso8601::parseUtc("2026-01-01T00:00:00Z", &t));
    TEST_ASSERT_EQUAL_INT64(1767225600, (long long)t);
    TEST_ASSERT_TRUE(Iso8601::parseUtc("2026-12-31T23:59:59Z", &t));
    TEST_ASSERT_EQUAL_INT64(1798761599, (long long)t);
}

void test_parse_rejects_garbage() {
    time_t t = 12345;
    TEST_ASSERT_FALSE(Iso8601::parseUtc("not a date", &t));
    TEST_ASSERT_EQUAL_INT64(12345, (long long)t);  // *out left unchanged on failure
    TEST_ASSERT_FALSE(Iso8601::parseUtc("", &t));
    TEST_ASSERT_FALSE(Iso8601::parseUtc(nullptr, &t));
}

void test_parse_rejects_out_of_range_fields() {
    time_t t = 0;
    TEST_ASSERT_FALSE(Iso8601::parseUtc("2026-13-01T00:00:00Z", &t));  // month 13
    TEST_ASSERT_FALSE(Iso8601::parseUtc("2026-09-20T25:00:00Z", &t));  // hour 25
    TEST_ASSERT_FALSE(Iso8601::parseUtc("2026-09-20T01:60:00Z", &t));  // minute 60
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_with_seconds_and_z);
    RUN_TEST(test_parse_without_seconds);
    RUN_TEST(test_parse_with_space_separator_no_z);
    RUN_TEST(test_parse_year_boundaries);
    RUN_TEST(test_parse_rejects_garbage);
    RUN_TEST(test_parse_rejects_out_of_range_fields);
    return UNITY_END();
}
