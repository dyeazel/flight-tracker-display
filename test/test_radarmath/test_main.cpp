#include <math.h>
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

// ---- placeLabels ----

constexpr float kW = 320, kH = 240, kHalf = 4;

static bool rectsOverlap(const Rect &a, const Rect &b) {
    return a.x1 < b.x2 && b.x1 < a.x2 && a.y1 < b.y2 && b.y1 < a.y2;
}
static bool vectorHitsLabel(const LabelInput &f, const Rect &r) {
    // Sample along the vector - coarse but plenty for 40-50px segments vs. label-sized rects.
    for (int i = 0; i <= 50; i++) {
        const float x = f.cx + f.vx * i / 50, y = f.cy + f.vy * i / 50;
        if (x >= r.x1 && x <= r.x2 && y >= r.y1 && y <= r.y2) return true;
    }
    return false;
}
static LabelInput flight(float cx, float cy, float vx, float vy) {
    LabelInput f;
    f.cx = cx; f.cy = cy; f.vx = vx; f.vy = vy; f.w = 60; f.h = 27;
    return f;
}

void test_label_defaults_to_preferred_side_when_nothing_collides() {
    LabelInput in[1] = {flight(100, 120, 0, 0)};  // left of center -> prefers right
    Rect out[1];
    placeLabels(in, 1, kW, kH, kHalf, nullptr, 0, out);
    TEST_ASSERT_TRUE(out[0].x1 > 100);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 120 - 13.5f, out[0].y1);
}
void test_label_moves_off_its_own_velocity_vector() {
    // Vector points straight right from a left-of-center marker - exactly where the preferred label goes.
    LabelInput in[1] = {flight(100, 120, 48, 0)};
    Rect out[1];
    placeLabels(in, 1, kW, kH, kHalf, nullptr, 0, out);
    TEST_ASSERT_FALSE(vectorHitsLabel(in[0], out[0]));
}
void test_label_avoids_vector_for_every_heading() {
    for (int deg = 0; deg < 360; deg += 15) {
        const float rad = deg * 3.14159265f / 180.0f;
        LabelInput in[1] = {flight(160, 124, 48 * sinf(rad), -48 * cosf(rad))};
        Rect out[1];
        placeLabels(in, 1, kW, kH, kHalf, nullptr, 0, out);
        TEST_ASSERT_FALSE(vectorHitsLabel(in[0], out[0]));
    }
}
void test_two_close_flights_do_not_overlap_labels() {
    LabelInput in[2] = {flight(120, 120, 0, 0), flight(130, 128, 0, 0)};
    Rect out[2];
    placeLabels(in, 2, kW, kH, kHalf, nullptr, 0, out);
    TEST_ASSERT_FALSE(rectsOverlap(out[0], out[1]));
}
void test_three_clustered_flights_do_not_overlap_labels_or_markers() {
    LabelInput in[3] = {flight(150, 120, 20, 10), flight(158, 126, -10, 20), flight(145, 130, 0, -20)};
    Rect out[3];
    placeLabels(in, 3, kW, kH, kHalf, nullptr, 0, out);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (i != j) TEST_ASSERT_FALSE(rectsOverlap(out[i], out[j]));
            Rect m = {in[j].cx - kHalf, in[j].cy - kHalf, in[j].cx + kHalf, in[j].cy + kHalf};
            TEST_ASSERT_FALSE(rectsOverlap(out[i], m));
        }
    }
}
void test_label_stays_on_screen_at_the_edge() {
    LabelInput in[2] = {flight(312, 120, 0, 0), flight(8, 232, 0, 0)};
    Rect out[2];
    placeLabels(in, 2, kW, kH, kHalf, nullptr, 0, out);
    for (int i = 0; i < 2; i++) {
        TEST_ASSERT_TRUE(out[i].x1 >= 0 && out[i].y1 >= 0 && out[i].x2 <= kW && out[i].y2 <= kH);
    }
}
void test_label_avoids_obstacle() {
    LabelInput in[1] = {flight(100, 120, 0, 0)};
    Rect obstacle = {104, 106, 170, 134};  // sits exactly on the preferred (right) spot
    Rect out[1];
    placeLabels(in, 1, kW, kH, kHalf, &obstacle, 1, out);
    TEST_ASSERT_FALSE(rectsOverlap(out[0], obstacle));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_label_defaults_to_preferred_side_when_nothing_collides);
    RUN_TEST(test_label_moves_off_its_own_velocity_vector);
    RUN_TEST(test_label_avoids_vector_for_every_heading);
    RUN_TEST(test_two_close_flights_do_not_overlap_labels);
    RUN_TEST(test_three_clustered_flights_do_not_overlap_labels_or_markers);
    RUN_TEST(test_label_stays_on_screen_at_the_edge);
    RUN_TEST(test_label_avoids_obstacle);
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
