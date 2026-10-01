#pragma once

#include <lvgl.h>

#include "flight/Flight.h"

// The radar view (Phase 10): bearing/distance polar plot of every flight in range, custom-drawn (LVGL has
// no polar/radar widget) on one full-screen object's LV_EVENT_DRAW_MAIN callback - the same technique
// ../../mlb-board's Scoreboard.cpp uses for its bases/outs diamonds/triangles. The coordinate math itself
// (RadarMath.h) is pure and native-tested; this file is just the LVGL drawing layer around it.
namespace RadarScreen {

// Creates the radar's widgets as children of `parent` (FlightScreen's lv_screen_active()). Call once.
void begin(lv_obj_t *parent);

void show();
void hide();

// Adopts the latest flights and redraws. Home location comes from ConfigStore, read at draw time.
void update(const Flight *flights, int count);

}  // namespace RadarScreen
