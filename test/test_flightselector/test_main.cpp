#include <string.h>
#include <unity.h>

#include "config/CallsignList.h"
#include "flight/FlightSelector.h"

void setUp() {}
void tearDown() {}

// Phase 7: tracked callsigns are runtime-configurable (ConfigStore) but threaded through as a parameter
// here so findMissing() stays pure/native-testable. Zero configured callsigns (this deployment's default)
// hits the "always zero missing" degenerate case at count 0.
void test_find_missing_with_no_target_callsigns_configured() {
    char missing[4][12];
    Flight flights[2];
    strncpy(flights[0].callsign, "UAL1217", sizeof(flights[0].callsign) - 1);
    strncpy(flights[1].callsign, "DAL456", sizeof(flights[1].callsign) - 1);
    int found = FlightSelector::findMissing(flights, 2, missing, 4, nullptr, 0);
    TEST_ASSERT_EQUAL(0, found);
}

void test_find_missing_with_zero_flights_reports_all_targets_missing() {
    char missing[4][12];
    CallsignList::Buf targets[1] = {"UAL1217"};
    int found = FlightSelector::findMissing(nullptr, 0, missing, 4, targets, 1);
    TEST_ASSERT_EQUAL(1, found);  // no flights present at all (null/0) - every tracked callsign is missing
}

// Exercises the "track specific flights" branch: one tracked callsign present, one not.
void test_find_missing_reports_only_absent_tracked_callsigns() {
    char missing[4][12];
    Flight flights[1];
    strncpy(flights[0].callsign, "UAL1217", sizeof(flights[0].callsign) - 1);
    CallsignList::Buf targets[2] = {"UAL1217", "DAL456"};
    int found = FlightSelector::findMissing(flights, 1, missing, 4, targets, 2);
    TEST_ASSERT_EQUAL(1, found);
    TEST_ASSERT_EQUAL_STRING("DAL456", missing[0]);
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
    RUN_TEST(test_find_missing_with_zero_flights_reports_all_targets_missing);
    RUN_TEST(test_find_missing_reports_only_absent_tracked_callsigns);
    RUN_TEST(test_sort_commercial_first_moves_commercial_flights_to_front);
    RUN_TEST(test_sort_commercial_first_all_same_kind_is_a_noop_order);
    RUN_TEST(test_sort_commercial_first_empty_is_safe);
    return UNITY_END();
}
