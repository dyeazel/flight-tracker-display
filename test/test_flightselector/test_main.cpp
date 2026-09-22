#include <string.h>
#include <unity.h>

#include "flight/FlightSelector.h"

void setUp() {}
void tearDown() {}

// This deployment's TARGET_CALLSIGNS_COUNT is 0 (config.cpp) - track-specific-callsigns mode isn't
// exercised by this build, only the "always zero missing" degenerate case findMissing's loop bound
// produces at count 0.
void test_find_missing_with_no_target_callsigns_configured() {
    char missing[4][12];
    Flight flights[2];
    strncpy(flights[0].callsign, "UAL1217", sizeof(flights[0].callsign) - 1);
    strncpy(flights[1].callsign, "DAL456", sizeof(flights[1].callsign) - 1);
    int found = FlightSelector::findMissing(flights, 2, missing, 4);
    TEST_ASSERT_EQUAL(0, found);
}

void test_find_missing_with_zero_flights() {
    char missing[4][12];
    int found = FlightSelector::findMissing(nullptr, 0, missing, 4);
    TEST_ASSERT_EQUAL(0, found);
}

static Flight commercial(const char *callsign) {
    Flight f;
    strncpy(f.callsign, callsign, sizeof(f.callsign) - 1);
    strncpy(f.airline, "Test Airline", sizeof(f.airline) - 1);
    return f;
}

static Flight nonCommercial(const char *callsign) {
    Flight f;
    strncpy(f.callsign, callsign, sizeof(f.callsign) - 1);
    return f;
}

void test_sort_commercial_first_moves_commercial_flights_to_front() {
    Flight flights[4] = {nonCommercial("N123AB"), commercial("UAL1217"), nonCommercial("N456CD"),
                          commercial("DAL456")};
    FlightSelector::sortCommercialFirst(flights, 4);
    TEST_ASSERT_TRUE(flights[0].isCommercial());
    TEST_ASSERT_TRUE(flights[1].isCommercial());
    TEST_ASSERT_FALSE(flights[2].isCommercial());
    TEST_ASSERT_FALSE(flights[3].isCommercial());
    // Stable: relative order within each group is preserved.
    TEST_ASSERT_EQUAL_STRING("UAL1217", flights[0].callsign);
    TEST_ASSERT_EQUAL_STRING("DAL456", flights[1].callsign);
    TEST_ASSERT_EQUAL_STRING("N123AB", flights[2].callsign);
    TEST_ASSERT_EQUAL_STRING("N456CD", flights[3].callsign);
}

void test_sort_commercial_first_all_same_kind_is_a_noop_order() {
    Flight flights[2] = {nonCommercial("N123AB"), nonCommercial("N456CD")};
    FlightSelector::sortCommercialFirst(flights, 2);
    TEST_ASSERT_EQUAL_STRING("N123AB", flights[0].callsign);
    TEST_ASSERT_EQUAL_STRING("N456CD", flights[1].callsign);
}

void test_sort_commercial_first_empty_is_safe() { FlightSelector::sortCommercialFirst(nullptr, 0); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_find_missing_with_no_target_callsigns_configured);
    RUN_TEST(test_find_missing_with_zero_flights);
    RUN_TEST(test_sort_commercial_first_moves_commercial_flights_to_front);
    RUN_TEST(test_sort_commercial_first_all_same_kind_is_a_noop_order);
    RUN_TEST(test_sort_commercial_first_empty_is_safe);
    return UNITY_END();
}
