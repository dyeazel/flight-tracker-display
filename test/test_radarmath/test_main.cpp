#include <unity.h>

#include "render/RadarMath.h"

using namespace RadarMath;

void setUp() {}
void tearDown() {}

constexpr float kEps = 0.01f;

void test_due_north_is_straight_up() {
    Point p = project(0, 10, 2.0f, 100);  // 10mi * 2px/mi = 20px
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, p.x);
    TEST_ASSERT_FLOAT_WITHIN(kEps, -20, p.y);
    TEST_ASSERT_FALSE(p.clipped);
}
void test_due_east_is_straight_right() {
    Point p = project(90, 10, 2.0f, 100);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 20, p.x);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, p.y);
}
void test_due_south_is_straight_down() {
    Point p = project(180, 10, 2.0f, 100);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, p.x);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 20, p.y);
}
void test_due_west_is_straight_left() {
    Point p = project(270, 10, 2.0f, 100);
    TEST_ASSERT_FLOAT_WITHIN(kEps, -20, p.x);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, p.y);
}
void test_northeast_quadrant() {
    Point p = project(45, 10, 2.0f, 100);
    TEST_ASSERT_TRUE(p.x > 0);
    TEST_ASSERT_TRUE(p.y < 0);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, fabsf(p.x), fabsf(p.y));  // 45 degrees: equal magnitude
}
void test_at_exactly_the_ring_radius_is_not_clipped() {
    Point p = project(0, 50, 2.0f, 100);  // 50mi * 2px/mi = exactly 100px
    TEST_ASSERT_FALSE(p.clipped);
    TEST_ASSERT_FLOAT_WITHIN(kEps, -100, p.y);
}
void test_beyond_max_radius_is_clipped_to_the_edge_on_the_same_bearing() {
    Point p = project(90, 80, 2.0f, 100);  // 80mi * 2px/mi = 160px, clipped to 100px, still due east
    TEST_ASSERT_TRUE(p.clipped);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 100, p.x);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, p.y);
}
void test_zero_distance_is_the_center() {
    Point p = project(123, 0, 2.0f, 100);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, p.x);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, p.y);
    TEST_ASSERT_FALSE(p.clipped);
}

void test_heading_vector_no_heading_is_zero() {
    Point v = headingVector(false, 90, 400, 0.1f, 50);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, v.x);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, v.y);
}
void test_heading_vector_scales_with_speed() {
    Point v = headingVector(true, 90, 100, 0.1f, 50);  // 100kt * 0.1 = 10px
    TEST_ASSERT_FLOAT_WITHIN(kEps, 10, v.x);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, v.y);
}
void test_heading_vector_caps_at_max_length() {
    Point v = headingVector(true, 90, 1000, 0.1f, 50);  // 1000kt * 0.1 = 100px, capped to 50
    TEST_ASSERT_FLOAT_WITHIN(kEps, 50, v.x);
}
void test_heading_vector_points_north() {
    Point v = headingVector(true, 0, 200, 0.1f, 50);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 0, v.x);
    TEST_ASSERT_FLOAT_WITHIN(kEps, -20, v.y);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_due_north_is_straight_up);
    RUN_TEST(test_due_east_is_straight_right);
    RUN_TEST(test_due_south_is_straight_down);
    RUN_TEST(test_due_west_is_straight_left);
    RUN_TEST(test_northeast_quadrant);
    RUN_TEST(test_at_exactly_the_ring_radius_is_not_clipped);
    RUN_TEST(test_beyond_max_radius_is_clipped_to_the_edge_on_the_same_bearing);
    RUN_TEST(test_zero_distance_is_the_center);
    RUN_TEST(test_heading_vector_no_heading_is_zero);
    RUN_TEST(test_heading_vector_scales_with_speed);
    RUN_TEST(test_heading_vector_caps_at_max_length);
    RUN_TEST(test_heading_vector_points_north);
    return UNITY_END();
}
