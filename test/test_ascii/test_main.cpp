#include <string.h>
#include <unity.h>

#include "util/Ascii.h"

void setUp() {}
void tearDown() {}

static void expect(const char *input, const char *expected) {
    char buf[128];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    Ascii::sanitize(buf);
    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

// The bug this module exists for: adsbdb returned "Minneapolis-Saint Paul Int'l..." with UTF-8 en dashes
// (U+2013) instead of plain hyphens, which rendered as garbage glyphs on the 8x8 ASCII-only font -
// "garbage text between Minneapolis and Saint Paul".
void test_en_dash_becomes_hyphen() {
    expect("Minneapolis\xE2\x80\x93Saint Paul Intl Airport", "Minneapolis-Saint Paul Intl Airport");
}

void test_em_dash_becomes_hyphen() { expect("Chicago\xE2\x80\x94OHare", "Chicago-OHare"); }

void test_curly_quotes_become_straight() {
    expect("O\xE2\x80\x99Hare", "O'Hare");                                    // right single quote
    expect("\xE2\x80\x9CInternational\xE2\x80\x9D Airport", "\"Int'l\" Airport");  // curly double quotes
}

// Latin-1 accented letters (real airport-name examples per Ascii.h's doc comment).
void test_latin1_accents_stripped_to_base_letter() {
    expect("M\xC3\xA1laga Airport", "Malaga Airport");        // a with acute
    expect("D\xC3\xBCsseldorf Airport", "Dusseldorf Airport");  // u with diaeresis
    expect("Z\xC3\xBCrich Airport", "Zurich Airport");
}

// The actual replacement this module exists to do per the plan: shorten "International" so more of a
// long airport name survives the display's tight per-name truncation.
void test_international_shortened_to_intl() {
    expect("Chicago O'Hare International Airport", "Chicago O'Hare Int'l Airport");
    expect("International", "Int'l");
}

void test_four_byte_sequence_dropped_whole() {
    expect("Test\xF0\x9F\x9B\xAB Plane", "Test Plane");  // airplane emoji U+1F6EB
}

// A stray/truncated continuation byte (can happen right at a strncpy truncation boundary) must be
// dropped without getting stuck or leaving a garbage byte behind.
void test_stray_continuation_byte_dropped() {
    char buf[] = "Test\x80Name";
    Ascii::sanitize(buf);
    TEST_ASSERT_EQUAL_STRING("TestName", buf);
}

void test_plain_ascii_unchanged() { expect("Chicago O'Hare", "Chicago O'Hare"); }

void test_empty_string() {
    char buf[] = "";
    Ascii::sanitize(buf);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_en_dash_becomes_hyphen);
    RUN_TEST(test_em_dash_becomes_hyphen);
    RUN_TEST(test_curly_quotes_become_straight);
    RUN_TEST(test_latin1_accents_stripped_to_base_letter);
    RUN_TEST(test_international_shortened_to_intl);
    RUN_TEST(test_four_byte_sequence_dropped_whole);
    RUN_TEST(test_stray_continuation_byte_dropped);
    RUN_TEST(test_plain_ascii_unchanged);
    RUN_TEST(test_empty_string);
    return UNITY_END();
}
