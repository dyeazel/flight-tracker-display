#include "FlightScreen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "config/CallsignList.h"
#include "config/ConfigStore.h"
#include "flight/FlightMath.h"
#include "render/GenericPlaneImg.h"
#include "render/LogoStore.h"
#include "render/RadarScreen.h"
#include "time/TimeSync.h"

namespace FlightScreen {

namespace {

constexpr int kW = 320, kH = 240;

// Recolor hex strings matching the pre-LVGL DisplayManager's RGB565 palette (white/green/gray/red/yellow).
constexpr const char *kWhite = "ffffff";
constexpr const char *kGreen = "00ff00";
constexpr const char *kGray = "969696";
constexpr const char *kRed = "ff3c3c";
constexpr const char *kYellow = "ffd200";
const lv_color_t kBgBlack = lv_color_black();
const lv_color_t kLvWhite = lv_color_white();
const lv_color_t kLvGreen = lv_color_make(0, 255, 0);
const lv_color_t kLvGray = lv_color_make(150, 150, 150);
const lv_color_t kLvRed = lv_color_make(255, 60, 60);
const lv_color_t kLvYellow = lv_color_make(255, 210, 0);
const lv_color_t kLvMsgGray = lv_color_make(160, 160, 160);  // 0xA0A0A0 - matches ../../mlb-board's own loading-message color

// ---- shared: full-screen message, heartbeat, WiFi status ----
lv_obj_t *gMsgLine1, *gMsgLine2;
lv_obj_t *gHeartbeat;  // small square, top-right
bool gHeartbeatOn = false;
enum class HeartbeatMode { Toggle, Refreshing, Stale } gHeartbeatMode = HeartbeatMode::Toggle;

// WiFi icon, lower-right corner (../../mlb-board's Scoreboard::setWifiStatus port, same corner it uses):
// LVGL's built-in WiFi glyph, green while connected; a diagonal red slash overlays it (and the glyph itself
// turns red) while not. Data-freshness (stale/refreshing) is this app's own separate heartbeat square, not
// conflated here - this is purely link status, matching mlb-board's WiFi icon in that respect too (its own
// separate "stale" tier there is about a specific game's data, which has no equivalent single concept here).
lv_obj_t *gWifiIcon, *gWifiSlash;
int gWifiState = -1;  // -1 unknown, 0 disconnected, 1 connected
lv_point_precise_t gWifiSlashPts[2] = {{0, 0}, {16, 14}};

// ---- mode roots ----
lv_obj_t *gDenseRoot, *gTwoUpRoot, *gOneUpRoot;  // RadarScreen owns its own root

// ---- MaxDensity widgets ----
lv_obj_t *gDenseMissing;                  // top status line (missing targets), recolor
lv_obj_t *gDenseRow[MAX_ROWS];             // one recolor label per flight, UNSCII_8
lv_obj_t *gDenseNoAircraft;                // "No aircraft in range"
lv_obj_t *gDenseLegend;                    // "* = route may be stale"

// ---- TwoUp / OneUp "card" widgets ----
constexpr int kTwoUpSlots = 2;
lv_obj_t *gTwoUpLogo[kTwoUpSlots], *gTwoUpLogoFallback[kTwoUpSlots], *gTwoUpDetail[kTwoUpSlots];
lv_obj_t *gTwoUpPage;
lv_obj_t *gOneUpLogo, *gOneUpLogoFallback, *gOneUpHeader, *gOneUpValues, *gOneUpRoute;
lv_obj_t *gOneUpDepLabel, *gOneUpBar, *gOneUpArrLabel, *gOneUpPage;

// ---- data snapshot (kept so applyGesture() can redraw without a fresh update() call) ----
Flight gFlights[MAX_ROWS];
int gFlightCount = 0;
char gMissing[MAX_ROWS][12];
FlightSelector::MissingStatus gMissingStatus[MAX_ROWS];
time_t gMissingWhenUtc[MAX_ROWS];
int gMissingCount = 0;
bool gAnyFlagged = false;
bool gHaveData = false;

int gPageTwoUp = 0, gPageOneUp = 0;

// ---- text helpers ----

struct Cursor {
    char *buf;
    size_t cap;
    size_t len = 0;
    Cursor(char *b, size_t c) : buf(b), cap(c) { buf[0] = '\0'; }
    void add(const char *s) {
        size_t n = strlen(s);
        if (len + n >= cap) n = cap - len - 1;
        if ((int)n > 0) memcpy(buf + len, s, n);
        len += n;
        buf[len] = '\0';
    }
    void addf(const char *fmt, ...) {
        char tmp[160];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(tmp, sizeof(tmp), fmt, ap);
        va_end(ap);
        add(tmp);
    }
    // "#RRGGBB text#" - LVGL label recolor markup (lv_label_set_recolor(true)).
    void span(const char *colorHex, const char *text) {
        addf("#%s %s#", colorHex, text);
    }
};

const char *missingLabel(FlightSelector::MissingStatus s) {
    switch (s) {
        case FlightSelector::MissingStatus::NotYetDeparted: return "dep";
        case FlightSelector::MissingStatus::Landed: return "arr";
        case FlightSelector::MissingStatus::InProgressNotVisible: return "in flight";
        default: return nullptr;
    }
}

// "UAL1217 dep 3:45", "DAL456 landed", "N441MV" (Unknown - no schedule data at all, same as the old
// Renderer's bare "not found"). Shared by MaxDensity's compact line and TwoUp/OneUp's trailing pages.
void formatMissingText(char *out, size_t cap, const char *callsign, FlightSelector::MissingStatus status, time_t whenUtc) {
    const char *label = missingLabel(status);
    if (!label) {
        snprintf(out, cap, "%s", callsign);
        return;
    }
    if (whenUtc == 0) {
        snprintf(out, cap, "%s %s", callsign, label);
        return;
    }
    struct tm local;
    localtime_r(&whenUtc, &local);
    snprintf(out, cap, "%s %s %d:%02d", callsign, label, local.tm_hour, local.tm_min);
}

// Composes one MaxDensity row's full label text (UNSCII_8, 8px/char - the same fixed grid the pre-LVGL
// drawText8x8()-based Renderer used, so this column math still applies exactly): line 1 (airline+flight#
// or bare callsign), line 2 (route codes, colored, + distance/bearing/altitude/trend/speed), line 3
// (airport full names, colored, if known), line 4 (timeline, if known).
void composeDenseRow(Cursor &c, const Flight &f) {
    const bool commercial = f.isCommercial();
    const char *statusHex = f.onGround ? kGray : kGreen;

    const char *originColor = kGray, *destColor = kGray;
    if (commercial) {
        if (strstr(f.origin, ROUTE_FLAG)) originColor = kRed;
        if (f.destinationGuessed) destColor = kYellow;
        else if (strstr(f.destination, ROUTE_FLAG)) destColor = kRed;
    }

    if (commercial) {
        char line1[40];
        snprintf(line1, sizeof(line1), "%s %s", f.airline, f.callsign + 3);
        c.span(kWhite, line1);
        c.add("\n");
        if (f.origin[0] || f.destination[0]) {
            const char *originText = f.origin[0] ? f.origin : "?";
            const char *destText = f.destination[0] ? f.destination : "?";
            char part1[16], part2[16];
            snprintf(part1, sizeof(part1), "%s->", originText);
            const int part1Len = (int)strlen(part1);
            int destWidth = CALLSIGN_COL_CHARS - part1Len;
            if (destWidth < 0) destWidth = 0;
            snprintf(part2, sizeof(part2), "%-*.*s", destWidth, destWidth, destText);
            c.span(originColor, part1);
            c.span(destColor, part2);
        } else {
            char pad[16];
            snprintf(pad, sizeof(pad), "%-*.*s", CALLSIGN_COL_CHARS, CALLSIGN_COL_CHARS, "");
            c.add(pad);
        }
    } else {
        char callsignField[16];
        snprintf(callsignField, sizeof(callsignField), "%-*.*s", CALLSIGN_COL_CHARS, CALLSIGN_COL_CHARS, f.callsign);
        c.span(kWhite, callsignField);
    }

    // Value fields (dist/bearing/altitude/trend/speed) - one uniform status-color span, single ASCII trend
    // char instead of the old separately-colored/drawn triangle (a deliberate simplification for the LVGL
    // port - the character itself already conveys climbing/level/descending).
    char distField[16], brgField[8], altField[16], spdField[8];
    char distRaw[16];
    snprintf(distRaw, sizeof(distRaw), "%.1fmi", f.distanceMi);
    snprintf(distField, sizeof(distField), "%9s", distRaw);
    const float brg = FlightMath::bearingDeg(ConfigStore::lat(), ConfigStore::lon(), f.lat, f.lon);
    snprintf(brgField, sizeof(brgField), "%3s", FlightMath::cardinalDirection(brg));
    char altRaw[16];
    snprintf(altRaw, sizeof(altRaw), "%dft", f.altitudeFt);
    snprintf(altField, sizeof(altField), "%8s", altRaw);
    const char trend = f.vrateFpm > ALT_TREND_THRESHOLD_FPM ? '^' : f.vrateFpm < -ALT_TREND_THRESHOLD_FPM ? 'v' : ' ';
    char spdRaw[16];
    snprintf(spdRaw, sizeof(spdRaw), "%.0fkt", f.speedKt);
    snprintf(spdField, sizeof(spdField), "%6s", spdRaw);

    char valueLine[64];
    snprintf(valueLine, sizeof(valueLine), "%s%s%s %c%s", distField, brgField, altField, trend, spdField);
    c.span(statusHex, valueLine);
    c.add("\n");

    if (commercial && (f.originName[0] || f.destinationName[0])) {
        const int kNameChars = (TIMELINE_WIDTH_CHARS - 4) / 2;
        const char *originText = f.originName[0] ? f.originName : f.origin;
        const char *destText = f.destinationName[0] ? f.destinationName : f.destination;
        char originField[24], destField2[20];
        snprintf(originField, sizeof(originField), "%-*.*s -> ", kNameChars, kNameChars, originText);
        snprintf(destField2, sizeof(destField2), "%-*.*s", kNameChars, kNameChars, destText);
        c.span(originColor, originField);
        c.span(destColor, destField2);
        c.add("\n");
    }

    if (commercial && f.depUtc != 0 && f.arrUtc != 0 && f.arrUtc > f.depUtc) {
        time_t dep = f.depUtc, arr = f.arrUtc;
        struct tm depTm, arrTm;
        localtime_r(&dep, &depTm);
        localtime_r(&arr, &arrTm);
        char depRaw[8], arrRaw[8], depField[8], arrField[8];
        snprintf(depRaw, sizeof(depRaw), "%d:%02d", depTm.tm_hour, depTm.tm_min);
        snprintf(arrRaw, sizeof(arrRaw), "%d:%02d", arrTm.tm_hour, arrTm.tm_min);
        snprintf(depField, sizeof(depField), "%-5s", depRaw);
        snprintf(arrField, sizeof(arrField), "%5s", arrRaw);
        char line[TIMELINE_WIDTH_CHARS + 1];
        memset(line, '-', TIMELINE_WIDTH_CHARS);
        line[TIMELINE_WIDTH_CHARS] = '\0';
        memcpy(line, depField, 5);
        memcpy(line + TIMELINE_WIDTH_CHARS - 5, arrField, 5);
        time_t now = TimeSync::isSynced() ? TimeSync::nowUtc() : dep;
        double frac = (arr > dep) ? (double)(now - dep) / (double)(arr - dep) : 0;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        const int middleLen = TIMELINE_WIDTH_CHARS - 10;
        int markerPos = 5 + (int)(frac * (middleLen - 1) + 0.5);
        if (markerPos < 5) markerPos = 5;
        if (markerPos > TIMELINE_WIDTH_CHARS - 6) markerPos = TIMELINE_WIDTH_CHARS - 6;
        line[markerPos] = '+';
        c.span(kGray, line);
    }
}

void layoutDense() {
    int y = 4;
    if (gMissingCount > 0) {
        char text[220] = "";
        Cursor c{text, sizeof(text)};
        c.add("Not found: ");
        for (int i = 0; i < gMissingCount; i++) {
            if (i > 0) c.add(", ");
            char tok[24];
            formatMissingText(tok, sizeof(tok), gMissing[i], gMissingStatus[i], gMissingWhenUtc[i]);
            c.span(kRed, tok);
        }
        lv_label_set_text(gDenseMissing, text);
        lv_obj_set_pos(gDenseMissing, 4, y);
        lv_obj_clear_flag(gDenseMissing, LV_OBJ_FLAG_HIDDEN);
        y += 12;
    } else {
        lv_obj_add_flag(gDenseMissing, LV_OBJ_FLAG_HIDDEN);
    }

    if (gFlightCount == 0) {
        lv_obj_clear_flag(gDenseNoAircraft, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(gDenseNoAircraft, 4, y);
        for (int i = 0; i < MAX_ROWS; i++) lv_obj_add_flag(gDenseRow[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gDenseLegend, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(gDenseNoAircraft, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < MAX_ROWS; i++) {
        if (i >= gFlightCount || y > kH - 24) {
            lv_obj_add_flag(gDenseRow[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        char text[400];
        Cursor c{text, sizeof(text)};
        composeDenseRow(c, gFlights[i]);
        // Trim the trailing newline the composer may have left, so the label doesn't reserve an extra blank line.
        while (c.len > 0 && text[c.len - 1] == '\n') text[--c.len] = '\0';
        lv_label_set_text(gDenseRow[i], text);
        lv_obj_set_pos(gDenseRow[i], 4, y);
        lv_obj_clear_flag(gDenseRow[i], LV_OBJ_FLAG_HIDDEN);

        int lines = 2;  // header + value line, always present
        if (gFlights[i].isCommercial() && (gFlights[i].originName[0] || gFlights[i].destinationName[0])) lines++;
        if (gFlights[i].isCommercial() && gFlights[i].depUtc != 0 && gFlights[i].arrUtc != 0 &&
            gFlights[i].arrUtc > gFlights[i].depUtc)
            lines++;
        y += lines == 2 ? 21 : lines == 3 ? 33 : 45;  // 9 (line1) + 12*(lines-1), matching the old per-line spacing
    }

    if (gAnyFlagged) {
        char legend[40];
        snprintf(legend, sizeof(legend), "%s = route may be stale", ROUTE_FLAG);
        lv_label_set_text(gDenseLegend, legend);
        lv_obj_clear_flag(gDenseLegend, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(gDenseLegend, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---- TwoUp / OneUp: a merged, configured-order list of present + missing-tracked flights, one page at a
// time. Missing entries are trailing pages (see FlightScreen.h's "wherever that mode has room" note). ----

constexpr int kMaxCardItems = MAX_ROWS + CallsignList::kMax;

struct CardItem {
    bool present;
    const Flight *flight;  // valid when present
    int missingIdx;        // valid when !present
};

int buildCardItems(CardItem *out, int maxOut) {
    int n = 0;
    for (int i = 0; i < gFlightCount && n < maxOut; i++) out[n++] = {true, &gFlights[i], -1};
    for (int i = 0; i < gMissingCount && n < maxOut; i++) out[n++] = {false, nullptr, i};
    return n;
}

// "Airline FLT#" (commercial) or the bare callsign (GA/unknown) - the header line both TwoUp's single-label
// card and OneUp's dedicated header widget show.
void composeHeaderText(char *out, size_t cap, const Flight &f) {
    if (f.isCommercial()) snprintf(out, cap, "%s %s", f.airline, f.callsign + 3);
    else snprintf(out, cap, "%s", f.callsign);
}

// Origin (if known) then destination (if known), each its own line, full airport name included. Only the
// short airport CODE (<=8 chars incl. any ROUTE_FLAG '*' suffix - see Flight.h) is put in a recolor span;
// the descriptive name that follows is left in the label's plain default color. Verified live that LVGL's
// recolor command state resets per wrapped visual line, so a color span that itself wraps (a long airport
// name easily does, which is exactly why it gets its own line) gets corrupted - the span's closing '#'
// lands on the wrong visual line and throws every later span in the same label out of sync (visible on
// screen as literal "#rrggbb" text and wrong colors from that point on). Keeping colored spans short
// enough to never wrap sidesteps the bug entirely. Caller adds any leading "\n" needed before this block.
void composeRouteText(Cursor &c, const Flight &f) {
    const char *originColor = kGray, *destColor = kGray;
    if (strstr(f.origin, ROUTE_FLAG)) originColor = kRed;
    if (f.destinationGuessed) destColor = kYellow;
    else if (strstr(f.destination, ROUTE_FLAG)) destColor = kRed;

    bool wroteOrigin = false;
    if (f.origin[0]) {
        c.span(originColor, f.origin);
        if (f.originName[0]) {
            c.add("  ");
            c.add(f.originName);
        }
        wroteOrigin = true;
    }
    if (f.destination[0]) {
        if (wroteOrigin) c.add("\n");
        c.add("-> ");
        c.span(destColor, f.destination);
        if (f.destinationName[0]) {
            c.add("  ");
            c.add(f.destinationName);
        }
    }
}

// "14.1mi  WSW\n34025ft  439kt  NE (270\xC2\xB0)" - distance/bearing-from-home on their own line, then
// altitude/speed/heading on a second line below (heading omitted when the source never reported one, e.g.
// some GA traffic) - the fields the user asked to sit beside the OneUp logo. Plain text (no recolor spans):
// it lives in a fixed-width label and doesn't need per-field coloring, and at OneUp's side-of-logo width
// it's long enough that a colored span risks the same wrap/recolor corruption composeRouteText's comment
// above describes.
void composeValuesLine(char *out, size_t cap, const Flight &f) {
    const float brg = FlightMath::bearingDeg(ConfigStore::lat(), ConfigStore::lon(), f.lat, f.lon);
    if (f.hasHeading) {
        snprintf(out, cap, "%.1fmi  %s\n%dft  %.0fkt  %s (%.0f\xC2\xB0)", f.distanceMi,
                 FlightMath::cardinalDirection(brg), f.altitudeFt, f.speedKt,
                 FlightMath::cardinalDirection(f.heading), f.heading);
    } else {
        snprintf(out, cap, "%.1fmi  %s\n%dft  %.0fkt", f.distanceMi, FlightMath::cardinalDirection(brg),
                 f.altitudeFt, f.speedKt);
    }
}

// TwoUp's card: everything in one label (header, route, value line with trend) - the same fields dense
// mode shows, just laid out for a single flight's own card instead of a fixed 8px/char column grid. OneUp
// instead splits these into separate widgets (composeHeaderText/composeValuesLine/composeRouteText above
// plus a real lv_bar for the timeline - see fillOneUpCard) so the header/values can sit beside the logo.
void composeCardText(Cursor &c, const Flight &f) {
    const bool commercial = f.isCommercial();
    char header[40];
    composeHeaderText(header, sizeof(header), f);
    c.span(kWhite, header);

    if (commercial && (f.origin[0] || f.destination[0])) {
        c.add("\n");
        composeRouteText(c, f);
    }

    char valueLine[64];
    const float brg = FlightMath::bearingDeg(ConfigStore::lat(), ConfigStore::lon(), f.lat, f.lon);
    // True chevron icons (LVGL's built-in LV_SYMBOL_UP/DOWN - real vector glyphs, confirmed present in this
    // card's lv_font_montserrat_14) instead of a typed '^'/'v' character.
    const char *trendIcon = f.vrateFpm > ALT_TREND_THRESHOLD_FPM    ? LV_SYMBOL_UP
                             : f.vrateFpm < -ALT_TREND_THRESHOLD_FPM ? LV_SYMBOL_DOWN
                                                                       : "";
    snprintf(valueLine, sizeof(valueLine), "%.1fmi  %s  %dft %s  %.0fkt", f.distanceMi,
             FlightMath::cardinalDirection(brg), f.altitudeFt, trendIcon, f.speedKt);
    c.add("\n");
    c.add(valueLine);
}

void fillCard(lv_obj_t *logo, lv_obj_t *logoFallback, lv_obj_t *detail, const CardItem &item) {
    if (item.present) {
        const Flight &f = *item.flight;
        char text[400];
        Cursor c{text, sizeof(text)};
        composeCardText(c, f);
        lv_label_set_text(detail, text);
        if (f.isCommercial() && f.airlineCode[0]) {
            LogoStore::want(f.airlineCode);
            char path[24];
            LogoStore::fsPath(f.airlineCode, path, sizeof(path));
            if (LogoStore::have(f.airlineCode)) {
                lv_image_set_src(logo, path);
                lv_obj_clear_flag(logo, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(logoFallback, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(logo, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(logoFallback, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(logo, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(logoFallback, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        // No aircraft data at all here (a tracked-but-not-yet-seen entry) - nothing to depict, so neither
        // the real logo nor the generic-airplane placeholder shows.
        lv_obj_add_flag(logo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(logoFallback, LV_OBJ_FLAG_HIDDEN);
        char text[400];
        Cursor c{text, sizeof(text)};
        c.span(kWhite, gMissing[item.missingIdx]);
        char status[32];
        formatMissingText(status, sizeof(status), gMissing[item.missingIdx], gMissingStatus[item.missingIdx],
                          gMissingWhenUtc[item.missingIdx]);
        c.add("\n");
        c.span(kGray, status);
        lv_label_set_text(detail, text);
    }
}

// Wraps `page` into [0, pageCount) - pageCount may be 0 (nothing to show at all).
int wrapPage(int page, int pageCount) {
    if (pageCount <= 0) return 0;
    page %= pageCount;
    if (page < 0) page += pageCount;
    return page;
}

void layoutTwoUp() {
    CardItem items[kMaxCardItems];
    const int total = buildCardItems(items, kMaxCardItems);
    const int pageCount = (total + kTwoUpSlots - 1) / kTwoUpSlots;
    gPageTwoUp = wrapPage(gPageTwoUp, pageCount);

    for (int slot = 0; slot < kTwoUpSlots; slot++) {
        const int idx = gPageTwoUp * kTwoUpSlots + slot;
        if (idx >= total) {
            lv_obj_add_flag(gTwoUpLogo[slot], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(gTwoUpLogoFallback[slot], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(gTwoUpDetail[slot], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(gTwoUpDetail[slot], LV_OBJ_FLAG_HIDDEN);
        fillCard(gTwoUpLogo[slot], gTwoUpLogoFallback[slot], gTwoUpDetail[slot], items[idx]);
    }
    if (pageCount > 1) {
        lv_label_set_text_fmt(gTwoUpPage, "%d/%d", gPageTwoUp + 1, pageCount);
        lv_obj_clear_flag(gTwoUpPage, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(gTwoUpPage, LV_OBJ_FLAG_HIDDEN);
    }
}

// Hides the timeline row's three widgets (dep label, bar, arr label) - shared by both "no schedule known"
// and "no flight at all" cases below.
void hideOneUpBar() {
    lv_obj_add_flag(gOneUpDepLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gOneUpBar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gOneUpArrLabel, LV_OBJ_FLAG_HIDDEN);
}

void fillOneUpCard(const CardItem &item) {
    if (!item.present) {
        // No aircraft data at all here - nothing to depict, so neither the real logo nor the generic
        // placeholder shows (mirrors fillCard()'s !item.present branch).
        lv_obj_add_flag(gOneUpLogo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gOneUpLogoFallback, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(gOneUpHeader, gMissing[item.missingIdx]);
        char status[32];
        formatMissingText(status, sizeof(status), gMissing[item.missingIdx], gMissingStatus[item.missingIdx],
                          gMissingWhenUtc[item.missingIdx]);
        lv_label_set_text(gOneUpValues, status);
        lv_label_set_text(gOneUpRoute, "");
        lv_obj_add_flag(gOneUpRoute, LV_OBJ_FLAG_HIDDEN);
        hideOneUpBar();
        return;
    }

    const Flight &f = *item.flight;
    const bool commercial = f.isCommercial();

    char header[40];
    composeHeaderText(header, sizeof(header), f);
    lv_label_set_text(gOneUpHeader, header);

    char values[64];
    composeValuesLine(values, sizeof(values), f);
    lv_label_set_text(gOneUpValues, values);

    char routeText[200];
    Cursor rc{routeText, sizeof(routeText)};
    if (commercial && (f.origin[0] || f.destination[0])) composeRouteText(rc, f);
    lv_label_set_text(gOneUpRoute, routeText);
    if (routeText[0]) lv_obj_clear_flag(gOneUpRoute, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(gOneUpRoute, LV_OBJ_FLAG_HIDDEN);

    // The progress bar row sits right below the route text, whose height varies with how many lines it
    // wrapped to - force an immediate layout pass so gOneUpRoute's height is current before reading it.
    lv_obj_update_layout(gOneUpRoute);
    const int barY = lv_obj_get_y(gOneUpRoute) + lv_obj_get_height(gOneUpRoute) + 8;

    if (commercial && f.depUtc != 0 && f.arrUtc != 0 && f.arrUtc > f.depUtc) {
        time_t dep = f.depUtc, arr = f.arrUtc;
        struct tm depTm, arrTm;
        localtime_r(&dep, &depTm);
        localtime_r(&arr, &arrTm);
        char depRaw[8], arrRaw[8];
        snprintf(depRaw, sizeof(depRaw), "%d:%02d", depTm.tm_hour, depTm.tm_min);
        snprintf(arrRaw, sizeof(arrRaw), "%d:%02d", arrTm.tm_hour, arrTm.tm_min);
        time_t now = TimeSync::isSynced() ? TimeSync::nowUtc() : dep;
        double frac = (arr > dep) ? (double)(now - dep) / (double)(arr - dep) : 0;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;

        lv_label_set_text(gOneUpDepLabel, depRaw);
        lv_label_set_text(gOneUpArrLabel, arrRaw);
        lv_bar_set_value(gOneUpBar, (int)(frac * 100 + 0.5), LV_ANIM_OFF);
        lv_obj_set_y(gOneUpDepLabel, barY);
        lv_obj_set_y(gOneUpBar, barY - 1);  // the bar is a touch shorter than the label's cap-height
        lv_obj_set_y(gOneUpArrLabel, barY);
        lv_obj_clear_flag(gOneUpDepLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(gOneUpBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(gOneUpArrLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        hideOneUpBar();
    }

    if (commercial && f.airlineCode[0]) {
        LogoStore::want(f.airlineCode);
        char path[24];
        LogoStore::fsPath(f.airlineCode, path, sizeof(path));
        if (LogoStore::have(f.airlineCode)) {
            lv_image_set_src(gOneUpLogo, path);
            lv_obj_clear_flag(gOneUpLogo, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(gOneUpLogoFallback, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(gOneUpLogo, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(gOneUpLogoFallback, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(gOneUpLogo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(gOneUpLogoFallback, LV_OBJ_FLAG_HIDDEN);
    }
}

void layoutOneUp() {
    CardItem items[kMaxCardItems];
    const int total = buildCardItems(items, kMaxCardItems);
    gPageOneUp = wrapPage(gPageOneUp, total);

    if (total == 0) {
        lv_obj_add_flag(gOneUpLogo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gOneUpLogoFallback, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(gOneUpHeader, "No aircraft in range");
        lv_label_set_text(gOneUpValues, "");
        lv_label_set_text(gOneUpRoute, "");
        lv_obj_add_flag(gOneUpRoute, LV_OBJ_FLAG_HIDDEN);
        hideOneUpBar();
        lv_obj_add_flag(gOneUpPage, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const CardItem &item = items[gPageOneUp];
    fillOneUpCard(item);
    if (total > 1) {
        lv_label_set_text_fmt(gOneUpPage, "%d/%d", gPageOneUp + 1, total);
        lv_obj_clear_flag(gOneUpPage, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(gOneUpPage, LV_OBJ_FLAG_HIDDEN);
    }
}

// Only one of {dense, two-up, one-up}'s widget tree exists at a time (built lazily on first use,
// torn down on switching away) - RadarScreen's own widgets are cheap enough (no per-flight labels; one
// custom-draw callback) to stay persistent instead. Building all four modes' widgets up front (as
// mlb-board's single-layout Scoreboard.cpp does for its one layout) exhausted LVGL's own memory pool the
// moment a real render burst hit it (crashed inside lv_draw_rect - LV_MEM_SIZE has to cover whatever's
// live at once, and four full-screen layouts' worth of widgets at once is a lot more than mlb-board ever
// holds). -1 = none of the three built yet.
int gBuiltMode = -1;
constexpr int kModeDense = 0, kModeTwoUp = 1, kModeOneUp = 2;

void buildDenseMode();
void buildTwoUpMode();
void buildOneUpMode();

void teardownBuiltMode() {
    switch (gBuiltMode) {
        case kModeDense:
            lv_obj_delete(gDenseRoot);  // deletes every child (rows, missing line, etc.) with it
            gDenseRoot = gDenseMissing = gDenseNoAircraft = gDenseLegend = nullptr;
            for (auto &r : gDenseRow) r = nullptr;
            break;
        case kModeTwoUp:
            lv_obj_delete(gTwoUpRoot);
            gTwoUpRoot = gTwoUpPage = nullptr;
            for (int i = 0; i < kTwoUpSlots; i++) gTwoUpLogo[i] = gTwoUpLogoFallback[i] = gTwoUpDetail[i] = nullptr;
            break;
        case kModeOneUp:
            lv_obj_delete(gOneUpRoot);
            gOneUpRoot = gOneUpLogo = gOneUpLogoFallback = gOneUpHeader = gOneUpValues = gOneUpRoute = nullptr;
            gOneUpDepLabel = gOneUpBar = gOneUpArrLabel = gOneUpPage = nullptr;
            break;
        default: break;
    }
    gBuiltMode = -1;
}

void ensureMode(int target) {
    if (gBuiltMode == target) return;
    teardownBuiltMode();
    if (target == kModeDense) buildDenseMode();
    else if (target == kModeTwoUp) buildTwoUpMode();
    else if (target == kModeOneUp) buildOneUpMode();
    gBuiltMode = target;
}

void layoutActiveMode() {
    if (!gHaveData) return;  // showMessage() owns the screen until the first real publish
    lv_obj_add_flag(gMsgLine1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gMsgLine2, LV_OBJ_FLAG_HIDDEN);
    switch (ConfigStore::displayMode()) {
        case ConfigStore::DisplayMode::MaxDensity:
            RadarScreen::hide();
            ensureMode(kModeDense);
            layoutDense();
            break;
        case ConfigStore::DisplayMode::TwoUp:
            RadarScreen::hide();
            ensureMode(kModeTwoUp);
            layoutTwoUp();
            break;
        case ConfigStore::DisplayMode::OneUp:
            RadarScreen::hide();
            ensureMode(kModeOneUp);
            layoutOneUp();
            break;
        case ConfigStore::DisplayMode::Radar:
            teardownBuiltMode();
            RadarScreen::show();
            RadarScreen::update(gFlights, gFlightCount);
            break;
    }
    lv_obj_move_foreground(gHeartbeat);
    lv_obj_move_foreground(gWifiIcon);
    lv_obj_move_foreground(gWifiSlash);
}

// ---- widget construction ----

lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, bool recolor) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    if (recolor) lv_label_set_recolor(l, true);
    // Default long_mode (WRAP) + default width (SIZE_CONTENT) - every field is already fixed-width
    // padded by the caller, so nothing needs wrapping/clipping; LV_LABEL_LONG_CLIP was tried here first
    // but clips against the object's own width, which content-sized auto-width makes moot anyway.
    return l;
}

// ---- generic airplane placeholder (shown wherever a real airline logo isn't available - no logo asset
// for non-commercial traffic, or a commercial logo that hasn't finished downloading yet) ----

// A baked-in 70x70 image (matches LogoStore's airline-logo footprint) rather than a downloaded one -
// see GenericPlaneImg.h: this one graphic is static and always needed, so unlike airline logos it's never
// mid-download.
lv_obj_t *mkLogoFallback(lv_obj_t *parent, int x, int y) {
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, &kGenericPlaneImg);
    lv_obj_set_pos(img, x, y);
    return img;
}

void buildDenseMode() {
    gDenseRoot = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(gDenseRoot);
    lv_obj_set_size(gDenseRoot, kW, kH);
    lv_obj_set_pos(gDenseRoot, 0, 0);
    lv_obj_set_style_bg_color(gDenseRoot, kBgBlack, 0);
    lv_obj_set_style_bg_opa(gDenseRoot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(gDenseRoot, LV_OBJ_FLAG_SCROLLABLE);

    gDenseMissing = mkLabel(gDenseRoot, &lv_font_unscii_8, kLvWhite, true);
    for (int i = 0; i < MAX_ROWS; i++) gDenseRow[i] = mkLabel(gDenseRoot, &lv_font_unscii_8, kLvWhite, true);
    gDenseNoAircraft = mkLabel(gDenseRoot, &lv_font_unscii_8, kLvGray, false);
    lv_label_set_text(gDenseNoAircraft, "No aircraft in range");
    gDenseLegend = mkLabel(gDenseRoot, &lv_font_unscii_8, kLvRed, false);
    lv_obj_set_pos(gDenseLegend, 4, kH - 10);
}

void buildTwoUpMode() {
    gTwoUpRoot = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(gTwoUpRoot);
    lv_obj_set_size(gTwoUpRoot, kW, kH);
    lv_obj_set_style_bg_color(gTwoUpRoot, kBgBlack, 0);
    lv_obj_set_style_bg_opa(gTwoUpRoot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(gTwoUpRoot, LV_OBJ_FLAG_SCROLLABLE);

    constexpr int kCardH = kH / kTwoUpSlots;
    for (int i = 0; i < kTwoUpSlots; i++) {
        const int y0 = i * kCardH;
        gTwoUpLogo[i] = lv_image_create(gTwoUpRoot);
        lv_obj_set_pos(gTwoUpLogo[i], 8, y0 + (kCardH - 70) / 2);
        gTwoUpLogoFallback[i] = mkLogoFallback(gTwoUpRoot, 8, y0 + (kCardH - 70) / 2);
        // Full flight detail (header/origin/destination/values - see composeCardText) needs more lines
        // than the old title+one-line-detail split had room for, so this is one label given an explicit
        // width: LVGL's default LV_LABEL_LONG_WRAP mode only wraps when a width is set (otherwise it
        // auto-sizes past the screen edge instead) - needed so a long airport name wraps rather than
        // getting cut off, per the user's request.
        gTwoUpDetail[i] = mkLabel(gTwoUpRoot, &lv_font_montserrat_14, kLvGray, true);
        lv_obj_set_pos(gTwoUpDetail[i], 90, y0 + 4);
        lv_obj_set_width(gTwoUpDetail[i], kW - 90 - 8);
        if (i > 0) {
            lv_obj_t *sep = lv_obj_create(gTwoUpRoot);
            lv_obj_remove_style_all(sep);
            lv_obj_set_size(sep, kW, 1);
            lv_obj_set_pos(sep, 0, y0);
            lv_obj_set_style_bg_color(sep, kLvGray, 0);
            lv_obj_set_style_bg_opa(sep, LV_OPA_50, 0);
        }
    }
    gTwoUpPage = mkLabel(gTwoUpRoot, &lv_font_montserrat_14, kLvGray, false);
    lv_obj_set_pos(gTwoUpPage, kW - 40, kH - 18);
}

void buildOneUpMode() {
    gOneUpRoot = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(gOneUpRoot);
    lv_obj_set_size(gOneUpRoot, kW, kH);
    lv_obj_set_style_bg_color(gOneUpRoot, kBgBlack, 0);
    lv_obj_set_style_bg_opa(gOneUpRoot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(gOneUpRoot, LV_OBJ_FLAG_SCROLLABLE);

    gOneUpLogo = lv_image_create(gOneUpRoot);
    lv_obj_set_pos(gOneUpLogo, 16, 16);
    gOneUpLogoFallback = mkLogoFallback(gOneUpRoot, 16, 16);

    // Header ("Airline FLT#") and the distance/bearing/altitude/speed/heading line sit to the right of the
    // logo, in that order - confirmed with the user - spanning roughly the logo's own height.
    constexpr int kSideX = 16 + 70 + 10;  // 96
    gOneUpHeader = mkLabel(gOneUpRoot, &lv_font_montserrat_16, kLvWhite, false);
    lv_obj_set_pos(gOneUpHeader, kSideX, 16);
    gOneUpValues = mkLabel(gOneUpRoot, &lv_font_montserrat_14, kLvGray, false);
    lv_obj_set_pos(gOneUpValues, kSideX, 38);
    lv_obj_set_width(gOneUpValues, kW - kSideX - 8);

    // Origin/destination route lines (each own line, wraps for long names - see composeRouteText) run the
    // full width below the logo/header block. Explicit width is what makes LVGL's default
    // LV_LABEL_LONG_WRAP mode actually wrap long airport names instead of auto-sizing past the screen edge.
    gOneUpRoute = mkLabel(gOneUpRoot, &lv_font_montserrat_14, kLvGray, true);
    lv_obj_set_pos(gOneUpRoute, 16, 96);
    lv_obj_set_width(gOneUpRoute, kW - 32);

    // Elapsed-flight progress bar - a real lv_bar, not a text rendition - sits below the route text, whose
    // height varies with how many lines it wrapped to; fillOneUpCard() repositions this row's Y under
    // gOneUpRoute on every update. X positions are fixed here since only the dep/arr time text changes.
    gOneUpDepLabel = mkLabel(gOneUpRoot, &lv_font_montserrat_14, kLvGray, false);
    lv_obj_set_pos(gOneUpDepLabel, 16, 0);
    gOneUpBar = lv_bar_create(gOneUpRoot);
    lv_obj_set_pos(gOneUpBar, 60, 0);
    lv_obj_set_size(gOneUpBar, 176, 12);
    lv_bar_set_range(gOneUpBar, 0, 100);
    lv_obj_set_style_radius(gOneUpBar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(gOneUpBar, lv_color_make(60, 60, 60), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gOneUpBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(gOneUpBar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(gOneUpBar, kLvGreen, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(gOneUpBar, LV_OPA_COVER, LV_PART_INDICATOR);
    gOneUpArrLabel = mkLabel(gOneUpRoot, &lv_font_montserrat_14, kLvGray, false);
    lv_obj_set_pos(gOneUpArrLabel, 244, 0);

    gOneUpPage = mkLabel(gOneUpRoot, &lv_font_montserrat_14, kLvGray, false);
    lv_obj_set_pos(gOneUpPage, kW - 40, kH - 18);
}

}  // namespace

void begin() {
    // Every per-mode root container explicitly disables scrolling on itself (see buildDenseMode() etc.,
    // RadarScreen's own gArea) - but a few widgets (this file's message/heartbeat/WiFi labels,
    // RadarScreen's compass labels) are children of the screen itself, which never got the same
    // treatment. RadarScreen's "S" compass label sits 2px past the bottom edge (kCenterY + ring radius +
    // 6, plus a 16px line height, vs this panel's 240px height) - harmless clipping on its own, but it's
    // just enough overflow for LVGL's default auto-scrollbar to kick in on the screen, drawn as a stray
    // gray bar down the right edge whenever Radar mode (the only mode that shows those labels) is active.
    lv_obj_clear_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    // The screen itself never had an explicit background - every mode's own root container paints its
    // own black bg (see buildDenseMode() etc.), but showMessage()'s labels have no root of their own, so
    // LVGL's default (light grey) theme background showed through behind them. Same fix
    // ../../mlb-board's Scoreboard::begin() applies to its own screen.
    lv_obj_set_style_bg_color(lv_screen_active(), kBgBlack, 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    // Styled like ../../mlb-board's own loading/status message (Scoreboard::showMessage): big, centered,
    // muted gray rather than this app's small left-aligned white-on-(accidentally-light-grey) text.
    // line2 is smaller since it's sometimes a full instructional sentence (see main.cpp's "Setup needed"
    // message) that needs to wrap, not a short status word.
    gMsgLine1 = mkLabel(lv_screen_active(), &lv_font_montserrat_28, kLvMsgGray, false);
    lv_obj_set_width(gMsgLine1, kW);
    lv_obj_set_style_text_align(gMsgLine1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(gMsgLine1, 0, 84);
    gMsgLine2 = mkLabel(lv_screen_active(), &lv_font_montserrat_14, kLvMsgGray, false);
    lv_obj_set_width(gMsgLine2, kW - 24);
    lv_obj_set_style_text_align(gMsgLine2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(gMsgLine2, 12, 120);
    lv_obj_add_flag(gMsgLine1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gMsgLine2, LV_OBJ_FLAG_HIDDEN);

    // Dense/two-up/one-up are built lazily on first use (see ensureMode()) - only RadarScreen's own
    // (cheap) widgets are created up front.
    RadarScreen::begin(lv_screen_active());

    gHeartbeat = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(gHeartbeat);
    lv_obj_set_size(gHeartbeat, HEARTBEAT_SIZE, HEARTBEAT_SIZE);
    lv_obj_set_pos(gHeartbeat, kW - HEARTBEAT_SIZE - 2, 2);
    lv_obj_set_style_bg_color(gHeartbeat, kBgBlack, 0);
    lv_obj_set_style_bg_opa(gHeartbeat, LV_OPA_COVER, 0);

    // Lower-right corner, like ../../mlb-board's Scoreboard - just above TwoUp/OneUp's page-counter row
    // (kH - 18, see buildTwoUpMode()/buildOneUpMode()) so the two don't overlap on those modes.
    constexpr int kWifiX = kW - 22, kWifiY = kH - 36;
    gWifiIcon = mkLabel(lv_screen_active(), &lv_font_montserrat_14, kLvGreen, false);
    lv_label_set_text(gWifiIcon, LV_SYMBOL_WIFI);
    lv_obj_set_pos(gWifiIcon, kWifiX, kWifiY);
    gWifiSlash = lv_line_create(lv_screen_active());
    lv_line_set_points(gWifiSlash, gWifiSlashPts, 2);
    lv_obj_set_style_line_width(gWifiSlash, 2, 0);
    lv_obj_set_style_line_color(gWifiSlash, kLvRed, 0);
    lv_obj_set_style_line_rounded(gWifiSlash, true, 0);
    lv_obj_set_pos(gWifiSlash, kWifiX - 1, kWifiY + 1);
    lv_obj_add_flag(gWifiSlash, LV_OBJ_FLAG_HIDDEN);
}

#ifdef TEST_HOOKS
const char *debugCardText(int slot) {
    if (slot >= 0 && slot < kTwoUpSlots && gBuiltMode == kModeTwoUp && gTwoUpDetail[slot]) {
        return lv_label_get_text(gTwoUpDetail[slot]);
    }
    if (slot == kTwoUpSlots && gBuiltMode == kModeOneUp && gOneUpHeader) {
        static char buf[256];
        const bool barShown = !lv_obj_has_flag(gOneUpBar, LV_OBJ_FLAG_HIDDEN);
        snprintf(buf, sizeof(buf), "%s | %s | %s | %s", lv_label_get_text(gOneUpHeader),
                 lv_label_get_text(gOneUpValues), lv_label_get_text(gOneUpRoute),
                 barShown ? lv_label_get_text(gOneUpDepLabel) : "(no bar)");
        if (barShown) {
            char tail[48];
            snprintf(tail, sizeof(tail), " %d%% %s", (int)lv_bar_get_value(gOneUpBar), lv_label_get_text(gOneUpArrLabel));
            strncat(buf, tail, sizeof(buf) - strlen(buf) - 1);
        }
        return buf;
    }
    return "";
}
#endif

void showMessage(const char *line1, const char *line2, uint32_t line2Color) {
    gHaveData = false;
    teardownBuiltMode();
    RadarScreen::hide();
    lv_label_set_text(gMsgLine1, line1);
    lv_obj_clear_flag(gMsgLine1, LV_OBJ_FLAG_HIDDEN);
    if (line2 && line2[0]) {
        lv_obj_set_style_text_color(gMsgLine2, line2Color ? lv_color_hex(line2Color) : kLvMsgGray, 0);
        lv_label_set_text(gMsgLine2, line2);
        lv_obj_clear_flag(gMsgLine2, LV_OBJ_FLAG_HIDDEN);
    }
}

void update(Flight *flights, int count, const char missing[][12], const FlightSelector::MissingStatus *missingStatus,
            const time_t *missingWhenUtc, int missingCount, bool anyFlagged) {
    gFlightCount = count < MAX_ROWS ? count : MAX_ROWS;
    for (int i = 0; i < gFlightCount; i++) gFlights[i] = flights[i];
    // gMissing/gMissingStatus/gMissingWhenUtc are all sized MAX_ROWS (matching every caller's own arrays,
    // e.g. NetTask's gWorkingMissing[MAX_ROWS]) - NOT CallsignList::kMax, which is a tracked-callsign-count
    // bound, not a missing-array-size bound (see the near-identical bug just fixed in SimFlights::fill()).
    gMissingCount = missingCount < MAX_ROWS ? missingCount : MAX_ROWS;
    for (int i = 0; i < gMissingCount; i++) {
        memcpy(gMissing[i], missing[i], sizeof(gMissing[i]));
        gMissingStatus[i] = missingStatus[i];
        gMissingWhenUtc[i] = missingWhenUtc[i];
    }
    gAnyFlagged = anyFlagged;
    gHaveData = true;

    gHeartbeatMode = HeartbeatMode::Toggle;
    gHeartbeatOn = !gHeartbeatOn;
    lv_obj_set_style_bg_color(gHeartbeat, gHeartbeatOn ? kLvGreen : kBgBlack, 0);

    layoutActiveMode();
}

void applyGesture(Gesture::Action action) {
    if (action == Gesture::Action::None || !gHaveData) return;
    switch (action) {
        case Gesture::Action::ModePrev:
        case Gesture::Action::ModeNext: {
            int mode = (int)ConfigStore::displayMode();
            constexpr int kModeCount = 4;
            mode = (mode + (action == Gesture::Action::ModeNext ? 1 : -1) + kModeCount) % kModeCount;
            ConfigStore::setDisplayMode((ConfigStore::DisplayMode)mode);
            break;
        }
        case Gesture::Action::PagePrev:
            if (ConfigStore::displayMode() == ConfigStore::DisplayMode::TwoUp) gPageTwoUp--;
            else if (ConfigStore::displayMode() == ConfigStore::DisplayMode::OneUp) gPageOneUp--;
            break;
        case Gesture::Action::PageNext:
            if (ConfigStore::displayMode() == ConfigStore::DisplayMode::TwoUp) gPageTwoUp++;
            else if (ConfigStore::displayMode() == ConfigStore::DisplayMode::OneUp) gPageOneUp++;
            break;
        default: break;
    }
    layoutActiveMode();
}

void drawRefreshing() {
    gHeartbeatMode = HeartbeatMode::Refreshing;
    lv_obj_set_style_bg_color(gHeartbeat, kLvYellow, 0);
}

void drawStale() {
    gHeartbeatMode = HeartbeatMode::Stale;
    lv_obj_set_style_bg_color(gHeartbeat, kLvRed, 0);
}

void setWifiStatus(bool connected) {
    const int st = connected ? 1 : 0;
    if (st == gWifiState) return;  // skip the redundant restyle every redraw tick while nothing changed
    gWifiState = st;
    lv_obj_set_style_text_color(gWifiIcon, connected ? kLvGreen : kLvRed, 0);
    if (connected) lv_obj_add_flag(gWifiSlash, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(gWifiSlash, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace FlightScreen
