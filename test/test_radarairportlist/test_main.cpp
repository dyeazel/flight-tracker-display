#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "config/RadarAirportList.h"

void setUp() {}
void tearDown() {}

// ---- valid ----

void test_valid_accepts_iata_code() { TEST_ASSERT_TRUE(RadarAirportList::valid("ORD")); }
void test_valid_accepts_icao_code() { TEST_ASSERT_TRUE(RadarAirportList::valid("KMKE")); }
void test_valid_rejects_empty() { TEST_ASSERT_FALSE(RadarAirportList::valid("")); }
void test_valid_rejects_single_char() { TEST_ASSERT_FALSE(RadarAirportList::valid("O")); }
void test_valid_rejects_too_long() { TEST_ASSERT_FALSE(RadarAirportList::valid("ABCDE")); }  // 5 > kCodeChars(4)
void test_valid_rejects_lowercase() { TEST_ASSERT_FALSE(RadarAirportList::valid("ord")); }
void test_valid_rejects_punctuation() { TEST_ASSERT_FALSE(RadarAirportList::valid("OR-D")); }
void test_valid_rejects_null() { TEST_ASSERT_FALSE(RadarAirportList::valid(nullptr)); }

// ---- validate ----

void test_validate_empty_list_is_valid() { TEST_ASSERT_NULL(RadarAirportList::validate(nullptr, 0)); }

void test_validate_accepts_full_list() {
    RadarAirportList::Code cs[RadarAirportList::kMax];
    for (int i = 0; i < RadarAirportList::kMax; i++) snprintf(cs[i], sizeof(cs[i]), "A%02d", i);
    TEST_ASSERT_NULL(RadarAirportList::validate(cs, RadarAirportList::kMax));
}

void test_validate_rejects_too_many() {
    RadarAirportList::Code cs[RadarAirportList::kMax + 1];
    for (int i = 0; i < RadarAirportList::kMax + 1; i++) snprintf(cs[i], sizeof(cs[i]), "A%02d", i);
    TEST_ASSERT_EQUAL_STRING("too_many_airports", RadarAirportList::validate(cs, RadarAirportList::kMax + 1));
}

void test_validate_rejects_invalid_entry() {
    RadarAirportList::Code cs[1];
    strcpy(cs[0], "o1");
    TEST_ASSERT_EQUAL_STRING("bad_airport", RadarAirportList::validate(cs, 1));
}

void test_validate_rejects_duplicate() {
    RadarAirportList::Code cs[2];
    strcpy(cs[0], "ORD");
    strcpy(cs[1], "ORD");
    TEST_ASSERT_EQUAL_STRING("duplicate_airport", RadarAirportList::validate(cs, 2));
}

// ---- encode/decode round-trip ----

void test_encode_empty_list_is_empty_string() {
    char out[RadarAirportList::kEncodedMax];
    TEST_ASSERT_EQUAL(0, RadarAirportList::encode(nullptr, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_encode_joins_with_commas() {
    RadarAirportList::Code cs[2];
    strcpy(cs[0], "ORD");
    strcpy(cs[1], "MKE");
    char out[RadarAirportList::kEncodedMax];
    size_t len = RadarAirportList::encode(cs, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("ORD,MKE", out);
    TEST_ASSERT_EQUAL(strlen(out), len);
}

void test_decode_round_trips_encode() {
    RadarAirportList::Code cs[3];
    strcpy(cs[0], "ORD");
    strcpy(cs[1], "MKE");
    strcpy(cs[2], "KMSP");
    char enc[RadarAirportList::kEncodedMax];
    RadarAirportList::encode(cs, 3, enc, sizeof(enc));

    RadarAirportList::Code out[RadarAirportList::kMax];
    int n = RadarAirportList::decode(enc, out, RadarAirportList::kMax);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_STRING("ORD", out[0]);
    TEST_ASSERT_EQUAL_STRING("MKE", out[1]);
    TEST_ASSERT_EQUAL_STRING("KMSP", out[2]);
}

void test_decode_skips_malformed_and_duplicate_entries() {
    RadarAirportList::Code out[RadarAirportList::kMax];
    int n = RadarAirportList::decode("ORD,bad-code,ORD,MKE", out, RadarAirportList::kMax);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("ORD", out[0]);
    TEST_ASSERT_EQUAL_STRING("MKE", out[1]);
}

void test_decode_empty_string_is_zero() {
    RadarAirportList::Code out[RadarAirportList::kMax];
    TEST_ASSERT_EQUAL(0, RadarAirportList::decode("", out, RadarAirportList::kMax));
}

void test_decode_null_is_zero() {
    RadarAirportList::Code out[RadarAirportList::kMax];
    TEST_ASSERT_EQUAL(0, RadarAirportList::decode(nullptr, out, RadarAirportList::kMax));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_accepts_iata_code);
    RUN_TEST(test_valid_accepts_icao_code);
    RUN_TEST(test_valid_rejects_empty);
    RUN_TEST(test_valid_rejects_single_char);
    RUN_TEST(test_valid_rejects_too_long);
    RUN_TEST(test_valid_rejects_lowercase);
    RUN_TEST(test_valid_rejects_punctuation);
    RUN_TEST(test_valid_rejects_null);
    RUN_TEST(test_validate_empty_list_is_valid);
    RUN_TEST(test_validate_accepts_full_list);
    RUN_TEST(test_validate_rejects_too_many);
    RUN_TEST(test_validate_rejects_invalid_entry);
    RUN_TEST(test_validate_rejects_duplicate);
    RUN_TEST(test_encode_empty_list_is_empty_string);
    RUN_TEST(test_encode_joins_with_commas);
    RUN_TEST(test_decode_round_trips_encode);
    RUN_TEST(test_decode_skips_malformed_and_duplicate_entries);
    RUN_TEST(test_decode_empty_string_is_zero);
    RUN_TEST(test_decode_null_is_zero);
    return UNITY_END();
}
