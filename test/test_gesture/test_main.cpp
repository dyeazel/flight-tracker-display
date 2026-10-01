#include <unity.h>

#include "input/Gesture.h"

using namespace Gesture;

void setUp() {}
void tearDown() {}

// Feeds: settling sample (discarded), started sample, released - a minimal single-point tap.
static Action tap(int x, int y, uint32_t dt = 50) {
    Tracker t;
    t.feed(true, x, y, 0);       // discarded (settling)
    t.feed(true, x, y, 0);       // starts the touch at (x, y), t0 = 0
    return t.feed(false, x, y, dt);
}

void test_left_edge_tap_is_mode_prev() { TEST_ASSERT_EQUAL(Action::ModePrev, tap(10, 120)); }
void test_right_edge_tap_is_mode_next() { TEST_ASSERT_EQUAL(Action::ModeNext, tap(310, 120)); }
void test_top_edge_tap_is_page_prev() { TEST_ASSERT_EQUAL(Action::PagePrev, tap(160, 5)); }
void test_bottom_edge_tap_is_page_next() { TEST_ASSERT_EQUAL(Action::PageNext, tap(160, 235)); }
void test_center_tap_is_none() { TEST_ASSERT_EQUAL(Action::None, tap(160, 120)); }

void test_exact_edge_zone_boundary() {
    TEST_ASSERT_EQUAL(Action::ModePrev, tap(kEdgeZone - 1, 120));
    TEST_ASSERT_EQUAL(Action::None, tap(kEdgeZone, 120));
    TEST_ASSERT_EQUAL(Action::ModeNext, tap(kScreenW - kEdgeZone, 120));
    TEST_ASSERT_EQUAL(Action::None, tap(kScreenW - kEdgeZone - 1, 120));
}

void test_corner_resolves_to_mode_not_page() {
    // Top-left corner: inside both the left edge band and the top band - left/right wins (checked first).
    TEST_ASSERT_EQUAL(Action::ModePrev, tap(5, 5));
    TEST_ASSERT_EQUAL(Action::ModeNext, tap(315, 235));
}

void test_slow_press_in_a_zone_is_not_a_tap() {
    Tracker t;
    t.feed(true, 10, 120, 0);
    t.feed(true, 10, 120, 0);
    TEST_ASSERT_EQUAL(Action::None, t.feed(false, 10, 120, kTapMaxMs + 1));
}

void test_moved_too_far_is_not_a_tap_even_in_a_zone() {
    Tracker t;
    t.feed(true, 10, 120, 0);
    t.feed(true, 10, 120, 0);
    t.feed(true, 10, 120 + kTapMaxMove + 5, 10);
    TEST_ASSERT_EQUAL(Action::None, t.feed(false, 10, 120 + kTapMaxMove + 5, 20));
}

void test_first_sample_after_touch_down_is_discarded() {
    // A bogus first reading far from where the finger actually is must not affect classification -
    // only the second sample (start of `started_`) sets x0_/y0_.
    Tracker t;
    t.feed(true, 999, 999, 0);   // discarded settling sample, however wild
    t.feed(true, 10, 120, 0);    // real start
    TEST_ASSERT_EQUAL(Action::ModePrev, t.feed(false, 10, 120, 30));
}

void test_release_without_any_press_is_none() {
    Tracker t;
    TEST_ASSERT_EQUAL(Action::None, t.feed(false, 10, 120, 0));
}

void test_still_down_reports_none() {
    Tracker t;
    t.feed(true, 10, 120, 0);
    TEST_ASSERT_EQUAL(Action::None, t.feed(true, 10, 120, 10));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_left_edge_tap_is_mode_prev);
    RUN_TEST(test_right_edge_tap_is_mode_next);
    RUN_TEST(test_top_edge_tap_is_page_prev);
    RUN_TEST(test_bottom_edge_tap_is_page_next);
    RUN_TEST(test_center_tap_is_none);
    RUN_TEST(test_exact_edge_zone_boundary);
    RUN_TEST(test_corner_resolves_to_mode_not_page);
    RUN_TEST(test_slow_press_in_a_zone_is_not_a_tap);
    RUN_TEST(test_moved_too_far_is_not_a_tap_even_in_a_zone);
    RUN_TEST(test_first_sample_after_touch_down_is_discarded);
    RUN_TEST(test_release_without_any_press_is_none);
    RUN_TEST(test_still_down_reports_none);
    return UNITY_END();
}
