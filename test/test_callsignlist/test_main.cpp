#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "config/CallsignList.h"

void setUp() {}
void tearDown() {}

// ---- valid ----

void test_valid_accepts_ordinary_callsign() { TEST_ASSERT_TRUE(CallsignList::valid("UAL1217")); }
void test_valid_accepts_all_digit_callsign() { TEST_ASSERT_TRUE(CallsignList::valid("00000000")); }
void test_valid_rejects_empty() { TEST_ASSERT_FALSE(CallsignList::valid("")); }
void test_valid_rejects_single_char() { TEST_ASSERT_FALSE(CallsignList::valid("N")); }
void test_valid_rejects_too_long() { TEST_ASSERT_FALSE(CallsignList::valid("ABCDEFGHIJKL")); }  // 12 > kCallsignChars(11)
void test_valid_rejects_lowercase() { TEST_ASSERT_FALSE(CallsignList::valid("ual1217")); }
void test_valid_rejects_punctuation() { TEST_ASSERT_FALSE(CallsignList::valid("UAL-1217")); }
void test_valid_rejects_null() { TEST_ASSERT_FALSE(CallsignList::valid(nullptr)); }

// ---- validate ----

void test_validate_empty_list_is_valid() { TEST_ASSERT_NULL(CallsignList::validate(nullptr, 0)); }

void test_validate_accepts_full_list() {
    CallsignList::Buf cs[CallsignList::kMax];
    for (int i = 0; i < CallsignList::kMax; i++) snprintf(cs[i], sizeof(cs[i]), "UAL%d", 1000 + i);
    TEST_ASSERT_NULL(CallsignList::validate(cs, CallsignList::kMax));
}

void test_validate_rejects_too_many() {
    CallsignList::Buf cs[CallsignList::kMax + 1];
    for (int i = 0; i < CallsignList::kMax + 1; i++) snprintf(cs[i], sizeof(cs[i]), "UAL%d", 1000 + i);
    TEST_ASSERT_EQUAL_STRING("too_many_callsigns", CallsignList::validate(cs, CallsignList::kMax + 1));
}

void test_validate_rejects_invalid_entry() {
    CallsignList::Buf cs[1] = {"bad-cs"};
    TEST_ASSERT_EQUAL_STRING("bad_callsign", CallsignList::validate(cs, 1));
}

void test_validate_rejects_duplicate() {
    CallsignList::Buf cs[2] = {"UAL1217", "UAL1217"};
    TEST_ASSERT_EQUAL_STRING("duplicate_callsign", CallsignList::validate(cs, 2));
}

// ---- encode/decode round-trip ----

void test_encode_empty_list_is_empty_string() {
    char out[CallsignList::kEncodedMax];
    TEST_ASSERT_EQUAL(0, CallsignList::encode(nullptr, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_encode_joins_with_commas() {
    CallsignList::Buf cs[2] = {"UAL1217", "DAL456"};
    char out[CallsignList::kEncodedMax];
    size_t len = CallsignList::encode(cs, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("UAL1217,DAL456", out);
    TEST_ASSERT_EQUAL(strlen(out), len);
}

void test_decode_round_trips_encode() {
    CallsignList::Buf cs[3] = {"UAL1217", "DAL456", "N441MV"};
    char enc[CallsignList::kEncodedMax];
    CallsignList::encode(cs, 3, enc, sizeof(enc));

    CallsignList::Buf out[CallsignList::kMax];
    int n = CallsignList::decode(enc, out, CallsignList::kMax);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_STRING("UAL1217", out[0]);
    TEST_ASSERT_EQUAL_STRING("DAL456", out[1]);
    TEST_ASSERT_EQUAL_STRING("N441MV", out[2]);
}

void test_decode_skips_malformed_and_duplicate_entries() {
    CallsignList::Buf out[CallsignList::kMax];
    int n = CallsignList::decode("UAL1217,bad-cs,UAL1217,DAL456", out, CallsignList::kMax);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("UAL1217", out[0]);
    TEST_ASSERT_EQUAL_STRING("DAL456", out[1]);
}

void test_decode_empty_string_is_zero() {
    CallsignList::Buf out[CallsignList::kMax];
    TEST_ASSERT_EQUAL(0, CallsignList::decode("", out, CallsignList::kMax));
}

void test_decode_null_is_zero() {
    CallsignList::Buf out[CallsignList::kMax];
    TEST_ASSERT_EQUAL(0, CallsignList::decode(nullptr, out, CallsignList::kMax));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_accepts_ordinary_callsign);
    RUN_TEST(test_valid_accepts_all_digit_callsign);
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
