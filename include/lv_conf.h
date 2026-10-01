// Minimal LVGL 9 config, mirroring ../../mlb-board's: anything not set here uses lv_conf_internal.h defaults.
#if 1  /* enabled */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

// Fixed, built-in allocator: no libc malloc in the LVGL path (heap fragmentation was the reason for
// leaving MicroPython in the first place; see this project's README).
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (18U * 1024U)

// Airline logos are LVGL .bin images on LittleFS ("L:/logos/{code}.bin", see render/LogoStore.h). The
// built-in Arduino LittleFS driver mounts the filesystem itself. Image cache stays off (LV_CACHE_DEF_SIZE
// default 0): logos are decoded only while drawn, so no persistent buffers eat the 48 KB LVGL pool.
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 1
#define LV_FS_ARDUINO_ESP_LITTLEFS_LETTER 'L'

#define LV_DEF_REFR_PERIOD 33
#define LV_USE_LOG 0
#define LV_USE_OS LV_OS_NONE

// Fonts: UNSCII_8 is a true monospace bitmap font, used only for the max-density layout's column-aligned
// fields (distance/bearing/altitude/speed) - the same fixed 8px-per-character grid the pre-LVGL
// drawText8x8() port relied on; Montserrat is proportional and wouldn't line those columns up. Montserrat
// sizes are for the roomier two-per/one-per-screen layouts and the radar view's labels, where proportional
// text is fine.
#define LV_FONT_UNSCII_8 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif
#endif
