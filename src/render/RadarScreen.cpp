#include "RadarScreen.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "config/ConfigStore.h"
#include "data/AirportTable.h"
#include "flight/FlightMath.h"
#include "render/RadarMath.h"

namespace RadarScreen {

namespace {

constexpr int kCenterX = 160, kCenterY = 124;  // slightly below screen center, leaving room for a top label
constexpr float kMaxRadiusPx = 96.0f;          // the outermost ring's radius, whatever range it represents
// Doubled per Dean's request - was 24px/600kt, visually too short to read a fast jet's track at a glance.
constexpr float kMaxHeadingLenPx = 48.0f;
constexpr float kPxPerKt = kMaxHeadingLenPx / 600.0f;  // a fast jet (~500-600kt) reaches close to the cap
constexpr int kRingCount = 5;
constexpr int kSquareHalf = 4;

const lv_color_t kRingColor = lv_color_make(0, 110, 0);
const lv_color_t kAxisColor = lv_color_make(0, 70, 0);
const lv_color_t kSquareColor = lv_color_make(0, 255, 0);
const lv_color_t kHeadingColor = lv_color_make(255, 255, 255);
const lv_color_t kLabelColor = lv_color_make(200, 200, 200);
const lv_color_t kCompassColor = lv_color_make(120, 120, 120);
const lv_color_t kWhite = lv_color_make(255, 255, 255);

lv_obj_t *gArea;
lv_obj_t *gCompassN, *gCompassE, *gCompassS, *gCompassW;

Flight gFlights[MAX_ROWS];
int gFlightCount = 0;

void drawRing(lv_layer_t *layer, int radiusPx) {
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = kRingColor;
    d.width = 1;
    d.start_angle = 0;
    d.end_angle = 360;
    d.center.x = kCenterX;
    d.center.y = kCenterY;
    d.radius = (uint16_t)radiusPx;
    d.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &d);
}

void drawAxisLine(lv_layer_t *layer, int x1, int y1, int x2, int y2) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = kAxisColor;
    d.width = 1;
    d.opa = LV_OPA_COVER;
    d.p1 = {(lv_value_precise_t)x1, (lv_value_precise_t)y1};
    d.p2 = {(lv_value_precise_t)x2, (lv_value_precise_t)y2};
    lv_draw_line(layer, &d);
}

void drawSquare(lv_layer_t *layer, int cx, int cy, bool dim) {
    lv_draw_border_dsc_t d;
    lv_draw_border_dsc_init(&d);
    d.color = dim ? kAxisColor : kSquareColor;
    d.width = 1;
    d.opa = LV_OPA_COVER;
    d.side = LV_BORDER_SIDE_FULL;
    lv_area_t a = {cx - kSquareHalf, cy - kSquareHalf, cx + kSquareHalf, cy + kSquareHalf};
    lv_draw_border(layer, &d, &a);
}

void drawHeadingLine(lv_layer_t *layer, int cx, int cy, RadarMath::Point v) {
    if (v.x == 0 && v.y == 0) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = kHeadingColor;
    d.width = 1;
    d.opa = LV_OPA_COVER;
    d.p1 = {(lv_value_precise_t)cx, (lv_value_precise_t)cy};
    d.p2 = {(lv_value_precise_t)(cx + v.x), (lv_value_precise_t)(cy + v.y)};
    lv_draw_line(layer, &d);
}

void drawText(lv_layer_t *layer, int x, int y, int w, int h, const lv_color_t &color, const char *text) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = color;
    d.font = &lv_font_montserrat_14;
    d.text = text;
    lv_area_t a = {x, y, x + w, y + h};
    lv_draw_label(layer, &d, &a);
}

// White "+" and code label at an airport's projected position (same projection flights use) - the radar's
// always-shown reference airports (ConfigStore::radarAirportCount()/radarAirport()), resolved against the
// on-device AirportTable at draw time. A linear scan over ~3269 entries per configured airport, done only
// once per redraw at poll cadence - negligible.
void drawAirports(lv_layer_t *layer, float pxPerMile) {
    for (int i = 0; i < ConfigStore::radarAirportCount(); i++) {
        const AirportTable::AirportEntry *ap = AirportTable::findByCode(ConfigStore::radarAirport(i));
        if (!ap) continue;
        const float bearing = FlightMath::bearingDeg(ConfigStore::lat(), ConfigStore::lon(), ap->lat, ap->lon);
        const float distMi = FlightMath::haversineMiles(ConfigStore::lat(), ConfigStore::lon(), ap->lat, ap->lon);
        const RadarMath::Point p = RadarMath::project(bearing, distMi, pxPerMile, kMaxRadiusPx);
        const int cx = kCenterX + (int)p.x, cy = kCenterY + (int)p.y;
        drawText(layer, cx - 6, cy - 6, 12, 12, kWhite, LV_SYMBOL_PLUS);

        // Label goes on whichever side has more room, so it doesn't run off the panel edge - same rule
        // the flight labels below use.
        const bool leftHalf = cx > 320 / 2;
        const int labelW = 40;
        const int lx = leftHalf ? cx - 8 - labelW : cx + 8;
        drawText(layer, lx, cy - 6, labelW, 12, kWhite, ap->code);
    }
}

void onDraw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    const float pxPerMile = kMaxRadiusPx / ConfigStore::radarRangeMi();

    drawAxisLine(layer, kCenterX - (int)kMaxRadiusPx - 6, kCenterY, kCenterX + (int)kMaxRadiusPx + 6, kCenterY);
    drawAxisLine(layer, kCenterX, kCenterY - (int)kMaxRadiusPx - 6, kCenterX, kCenterY + (int)kMaxRadiusPx + 6);
    for (int i = 1; i <= kRingCount; i++) drawRing(layer, (int)(kMaxRadiusPx * i / kRingCount));

    for (int i = 0; i < gFlightCount; i++) {
        const Flight &f = gFlights[i];
        const float bearing = FlightMath::bearingDeg(ConfigStore::lat(), ConfigStore::lon(), f.lat, f.lon);
        const RadarMath::Point p = RadarMath::project(bearing, f.distanceMi, pxPerMile, kMaxRadiusPx);
        const int cx = kCenterX + (int)p.x, cy = kCenterY + (int)p.y;
        drawSquare(layer, cx, cy, p.clipped);
        const RadarMath::Point hv =
            RadarMath::headingVector(f.hasHeading, f.heading, f.speedKt, kPxPerKt, kMaxHeadingLenPx);
        drawHeadingLine(layer, cx, cy, hv);

        // Labels go on whichever side has more room, so they don't run off the panel edge.
        const bool leftHalf = cx > 320 / 2;
        const int labelW = 64;
        const int lx = leftHalf ? cx - kSquareHalf - 4 - labelW : cx + kSquareHalf + 4;
        char line1[16];
        snprintf(line1, sizeof(line1), "%s", f.callsign);
        drawText(layer, lx, cy - 12, labelW, 12, kLabelColor, line1);
        // True chevron icons (LVGL's built-in LV_SYMBOL_UP/DOWN - real vector glyphs, confirmed present in
        // this build's montserrat_14) instead of a typed '^'/'v' character.
        // " " (not "") for the level case - the old plain-ASCII version used the char itself as the
        // separator between speed and altitude ('^'/'v'/' '); losing that as empty ran them together
        // ("30016" instead of "300 16") once this became an icon that can be zero-width.
        const char *trendIcon = f.vrateFpm > ALT_TREND_THRESHOLD_FPM    ? LV_SYMBOL_UP
                                 : f.vrateFpm < -ALT_TREND_THRESHOLD_FPM ? LV_SYMBOL_DOWN
                                                                          : " ";
        char line2[24];
        snprintf(line2, sizeof(line2), "%.0f%s%d", f.speedKt, trendIcon, f.altitudeFt / 1000);
        drawText(layer, lx, cy + 1, labelW, 12, kLabelColor, line2);
    }

    drawAirports(layer, pxPerMile);
}

}  // namespace

void begin(lv_obj_t *parent) {
    gArea = lv_obj_create(parent);
    lv_obj_remove_style_all(gArea);
    lv_obj_set_size(gArea, 320, 240);
    lv_obj_set_pos(gArea, 0, 0);
    lv_obj_set_style_bg_color(gArea, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(gArea, LV_OPA_COVER, 0);
    lv_obj_clear_flag(gArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(gArea, onDraw, LV_EVENT_DRAW_MAIN, nullptr);

    auto mkCompass = [&](const char *text) {
        lv_obj_t *l = lv_label_create(parent);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, kCompassColor, 0);
        lv_label_set_text(l, text);
        return l;
    };
    gCompassN = mkCompass("N");
    gCompassE = mkCompass("E");
    gCompassS = mkCompass("S");
    gCompassW = mkCompass("W");
    lv_obj_set_pos(gCompassN, kCenterX - 4, kCenterY - (int)kMaxRadiusPx - 20);
    lv_obj_set_pos(gCompassE, kCenterX + (int)kMaxRadiusPx + 8, kCenterY - 8);
    lv_obj_set_pos(gCompassS, kCenterX - 4, kCenterY + (int)kMaxRadiusPx + 6);
    lv_obj_set_pos(gCompassW, kCenterX - (int)kMaxRadiusPx - 18, kCenterY - 8);

    hide();
}

void show() {
    lv_obj_clear_flag(gArea, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(gCompassN, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(gCompassE, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(gCompassS, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(gCompassW, LV_OBJ_FLAG_HIDDEN);
}

void hide() {
    lv_obj_add_flag(gArea, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gCompassN, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gCompassE, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gCompassS, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gCompassW, LV_OBJ_FLAG_HIDDEN);
}

void update(const Flight *flights, int count) {
    gFlightCount = count < MAX_ROWS ? count : MAX_ROWS;
    for (int i = 0; i < gFlightCount; i++) gFlights[i] = flights[i];
    lv_obj_invalidate(gArea);
}

}  // namespace RadarScreen
