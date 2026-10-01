#pragma once

#include <lvgl.h>

// 70x70 RGB565 image (matches LogoStore's airline-logo footprint) - the airport "flight" road-sign
// pictogram (green square, white airplane) FlightScreen.cpp's TwoUp/OneUp cards show in place of an
// airline logo when none is available. Baked into flash at compile time, unlike airline logos (which
// LogoStore downloads into LittleFS on demand): this one graphic is static and always needed, so it's
// always present, no network/first-download gap. Generated from tools/generic_plane_icon.png - see
// tools/generate_generic_plane_icon.py; GenericPlaneImg.cpp is generated, do not edit by hand.
extern const lv_image_dsc_t kGenericPlaneImg;
