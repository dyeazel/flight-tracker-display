#include <string.h>
#include <unity.h>

#include "data/AirportTable.h"
#include "flight/DestinationVerifier.h"

void setUp() {}
void tearDown() {}

static Flight commercialFlight(const char *destCode, float lat, float lon, int altitudeFt,
                                float vrateFpm, bool onGround) {
    Flight f;
    strncpy(f.airline, "Test Airline", sizeof(f.airline) - 1);  // isCommercial() checks airline[0]
    strncpy(f.destination, destCode, sizeof(f.destination) - 1);
    f.lat = lat;
    f.lon = lon;
    f.altitudeFt = altitudeFt;
    f.vrateFpm = vrateFpm;
    f.onGround = onGround;
    return f;
}

void test_noop_for_non_commercial_flight() {
    Flight f;  // airline left "" - not commercial
    strncpy(f.destination, "ORD", sizeof(f.destination) - 1);
    f.onGround = true;
    f.lat = 0;
    f.lon = 0;  // wildly wrong position - would trigger a correction if this ran
    DestinationVerifier::verifyOrCorrectDestination(f);
    TEST_ASSERT_EQUAL_STRING("ORD", f.destination);
    TEST_ASSERT_FALSE(f.destinationGuessed);
}

void test_noop_when_no_destination_code() {
    Flight f = commercialFlight("", 0, 0, 0, 0, true);
    DestinationVerifier::verifyOrCorrectDestination(f);
    TEST_ASSERT_EQUAL_STRING("", f.destination);
}

void test_on_ground_at_claimed_destination_confirms_no_change() {
    const AirportTable::AirportEntry *ord = AirportTable::findByCode("ORD");
    TEST_ASSERT_NOT_NULL(ord);
    Flight f = commercialFlight("ORD", ord->lat, ord->lon, (int)ord->elevationFt, 0, /*onGround=*/true);
    DestinationVerifier::verifyOrCorrectDestination(f);
    TEST_ASSERT_EQUAL_STRING("ORD", f.destination);
    TEST_ASSERT_FALSE(f.destinationGuessed);
}

// Landed position IS a settled fact - if it doesn't match the claimed destination, correct to the real
// nearest airport there, not marked as a guess.
void test_on_ground_far_from_claimed_corrects_to_actual_landing_airport() {
    const AirportTable::AirportEntry *msp = AirportTable::findByCode("MSP");
    TEST_ASSERT_NOT_NULL(msp);
    Flight f = commercialFlight("ORD", msp->lat, msp->lon, (int)msp->elevationFt, 0, /*onGround=*/true);
    DestinationVerifier::verifyOrCorrectDestination(f);
    TEST_ASSERT_EQUAL_STRING("MSP", f.destination);
    TEST_ASSERT_FALSE(f.destinationGuessed);  // landed = settled fact, not a guess
}

// A trailing ROUTE_FLAG ('*') suffix (from FlightSelector's plausibility flagging) must be stripped
// before the code lookup, but is not itself removed from f.destination when nothing needs correcting.
void test_route_flag_suffix_stripped_for_lookup_only() {
    const AirportTable::AirportEntry *ord = AirportTable::findByCode("ORD");
    TEST_ASSERT_NOT_NULL(ord);
    Flight f = commercialFlight("ORD*", ord->lat, ord->lon, (int)ord->elevationFt, 0, /*onGround=*/true);
    DestinationVerifier::verifyOrCorrectDestination(f);
    TEST_ASSERT_EQUAL_STRING("ORD*", f.destination);  // unchanged - flag not stripped from the field itself
}

void test_airborne_not_descending_is_noop_even_far_off_course() {
    Flight f = commercialFlight("ORD", 0, -160, 30000, /*vrateFpm=*/-50, /*onGround=*/false);
    DestinationVerifier::verifyOrCorrectDestination(f);
    TEST_ASSERT_EQUAL_STRING("ORD", f.destination);
    TEST_ASSERT_FALSE(f.destinationGuessed);
}

// Descending, low over the claimed destination's own field elevation, close by: looks like a normal
// final approach into what was already claimed - no correction needed.
void test_airborne_descending_near_claimed_destination_confirms_no_change() {
    const AirportTable::AirportEntry *ord = AirportTable::findByCode("ORD");
    TEST_ASSERT_NOT_NULL(ord);
    Flight f = commercialFlight("ORD", ord->lat, ord->lon, (int)ord->elevationFt + 2000,
                                 /*vrateFpm=*/-1500, /*onGround=*/false);
    DestinationVerifier::verifyOrCorrectDestination(f);
    TEST_ASSERT_EQUAL_STRING("ORD", f.destination);
    TEST_ASSERT_FALSE(f.destinationGuessed);
}

// Descending, low, near a DIFFERENT real airport than claimed: an unconfirmed guess (a go-around/hold
// could still happen), so destinationGuessed must be set true.
void test_airborne_descending_near_different_airport_is_a_guess() {
    const AirportTable::AirportEntry *msp = AirportTable::findByCode("MSP");
    TEST_ASSERT_NOT_NULL(msp);
    Flight f = commercialFlight("ORD", msp->lat, msp->lon, (int)msp->elevationFt + 2000,
                                 /*vrateFpm=*/-1500, /*onGround=*/false);
    DestinationVerifier::verifyOrCorrectDestination(f);
    TEST_ASSERT_EQUAL_STRING("MSP", f.destination);
    TEST_ASSERT_TRUE(f.destinationGuessed);
}

// Descending but nowhere near any table airport (e.g. still cruising down over open water) - nothing
// useful to say, so no-op.
void test_airborne_descending_far_from_any_airport_is_noop() {
    Flight f = commercialFlight("ORD", 0.0f, -160.0f, 20000, /*vrateFpm=*/-1500, /*onGround=*/false);
    DestinationVerifier::verifyOrCorrectDestination(f);
    TEST_ASSERT_EQUAL_STRING("ORD", f.destination);
    TEST_ASSERT_FALSE(f.destinationGuessed);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_noop_for_non_commercial_flight);
    RUN_TEST(test_noop_when_no_destination_code);
    RUN_TEST(test_on_ground_at_claimed_destination_confirms_no_change);
    RUN_TEST(test_on_ground_far_from_claimed_corrects_to_actual_landing_airport);
    RUN_TEST(test_route_flag_suffix_stripped_for_lookup_only);
    RUN_TEST(test_airborne_not_descending_is_noop_even_far_off_course);
    RUN_TEST(test_airborne_descending_near_claimed_destination_confirms_no_change);
    RUN_TEST(test_airborne_descending_near_different_airport_is_a_guess);
    RUN_TEST(test_airborne_descending_far_from_any_airport_is_noop);
    return UNITY_END();
}
