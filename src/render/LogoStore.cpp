#include "LogoStore.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <new>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <rom/miniz.h>  // the ESP32 ROM's tinfl inflate

#include "network/WifiManager.h"
#include "render/RgbaPng.h"

namespace LogoStore {

namespace {

// LittleFS.exists()/remove() print a "does not exist" line to the serial port whenever the file is
// missing (the normal case here) - the same reply-corruption risk mlb-board's LogoStore documented for its
// own setup-protocol replies. Use the POSIX calls underneath, which are silent.
constexpr const char *kMountPoint = "/littlefs";  // LittleFS.begin() default
void vfsPath(const char *fsPath, char *out, size_t n) { snprintf(out, n, "%s%s", kMountPoint, fsPath); }
bool fsFileExists(const char *fsPath) {
    char p[64];
    vfsPath(fsPath, p, sizeof(p));
    struct stat st;
    return stat(p, &st) == 0;
}
void fsRemoveQuiet(const char *fsPath) {
    char p[64];
    vfsPath(fsPath, p, sizeof(p));
    unlink(p);
}

constexpr int kLogo = 70;  // gstatic's flight-logo images are exactly 70x70 - no tile padding needed
constexpr uint8_t kLvMagic = 0x19, kLvRgb565 = 0x12;
constexpr int kMaxPngBytes = 8192;  // real logos checked ranged ~0.4-5.3 KB
constexpr uint32_t kRetryMinMs = 30UL * 1000UL, kRetryMaxMs = 10UL * 60UL * 1000UL;

using Code = char[8];  // airline codes are 2-3 chars in practice; generous margin

SemaphoreHandle_t gMux = nullptr;
Code gWanted[kMaxWanted];
int gWantedN = 0;
volatile bool gInstalled = false;

struct FailInfo {
    Code code = "";
    uint8_t count = 0;
    uint32_t until = 0;
    bool used = false;
};
FailInfo gFail[kMaxWanted * 2];

FailInfo *failFor(const char *code, bool create) {
    FailInfo *free_ = nullptr;
    for (FailInfo &f : gFail) {
        if (f.used && !strcmp(f.code, code)) return &f;
        if (!f.used && !free_) free_ = &f;
    }
    if (!create) return nullptr;
    if (!free_) free_ = &gFail[0];
    *free_ = FailInfo();
    free_->used = true;
    strncpy(free_->code, code, sizeof(free_->code) - 1);
    return free_;
}

char gUrl[128];
constexpr const char *kDlPath = "/logos/dl.png";

struct Ctx {
    File file;
    bool bad = false;
    size_t bytes = 0;
    const char *why = "";
    char whyBuf[64];
};

uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

// Composites one RGBA row onto a white background (most brand logos assume a light background) and
// appends it to the .bin being written.
void writeRow(void *ctxv, int, const uint8_t *rgba) {
    Ctx *c = static_cast<Ctx *>(ctxv);
    uint16_t row[kLogo];
    for (int x = 0; x < kLogo; x++) {
        const uint8_t *p = rgba + x * 4;
        const int a = p[3];
        row[x] = rgb565((p[0] * a + 255 * (255 - a) + 127) / 255, (p[1] * a + 255 * (255 - a) + 127) / 255,
                        (p[2] * a + 255 * (255 - a) + 127) / 255);
    }
    if (c->file.write((const uint8_t *)row, sizeof(row)) != sizeof(row)) {
        c->bad = true;
        c->why = "write row";
    } else {
        c->bytes += sizeof(row);
    }
}

int romInflate(void *ctx, const uint8_t *in, size_t n, uint8_t *out, size_t *outPos, size_t cap) {
    tinfl_decompressor *d = static_cast<tinfl_decompressor *>(ctx);
    while (n) {
        size_t inSize = n, outSize = cap - *outPos;
        const tinfl_status st = tinfl_decompress(d, in, &inSize, out, out + *outPos, &outSize,
                                                 TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        *outPos += outSize;
        in += inSize;
        n -= inSize;
        if (st == TINFL_STATUS_DONE) return 1;
        if (st < 0) return -1;
        if (st == TINFL_STATUS_NEEDS_MORE_INPUT) return 0;
        if (outSize == 0) return -1;
    }
    return 0;
}
int fileRead(void *ctx, uint8_t *buf, int n) { return static_cast<File *>(ctx)->read(buf, n); }

bool decodeFile(Ctx *c) {
    File f = LittleFS.open(kDlPath, "r");
    if (!f) {
        c->why = "open png";
        return false;
    }
    uint8_t *scratch = (uint8_t *)malloc(RgbaPng::scratchBytes(kLogo, kLogo));
    tinfl_decompressor *inf = new (std::nothrow) tinfl_decompressor;
    if (!scratch || !inf) {
        c->why = "no memory";
        free(scratch);
        delete inf;
        return false;
    }
    tinfl_init(inf);
    const RgbaPng::Status st = RgbaPng::decode(fileRead, &f, romInflate, inf, kLogo, kLogo, scratch, writeRow, c);
    free(scratch);
    delete inf;
    if (st != RgbaPng::Status::Ok) {
        c->bad = true;
        snprintf(c->whyBuf, sizeof(c->whyBuf), "%s", RgbaPng::statusName(st));
        c->why = c->whyBuf;
        return false;
    }
    return true;
}

void binPath(const char *code, const char *ext, char *out, size_t n) { snprintf(out, n, "/logos/%s.%s", code, ext); }

// Google's flight-logo CDN, keyed by 2-letter IATA code - verified live (2026-09-24). Not an official
// public API (no SLA), but the same class of dependency mlb-board already has on ESPN/MLB's own logo URLs.
void buildUrl(const char *code) {
    snprintf(gUrl, sizeof(gUrl), "https://www.gstatic.com/flights/airline_logos/70px/%s.png", code);
}

size_t download(const char *code) {
    buildUrl(code);
    WiFiClientSecure client;
    client.setInsecure();  // public image, no secrets sent
    client.setTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setTimeout(15000);
    http.useHTTP10(true);
    if (!http.begin(client, gUrl)) return 0;
    const int code_ = http.GET();
    const int size = http.getSize();
    if (code_ != 200 || size < 64 || size > kMaxPngBytes) {
        Serial.printf("[logo] %s: HTTP %d, size %d\n", code, code_, size);
        http.end();
        return 0;
    }
    LittleFS.mkdir("/logos");
    File f = LittleFS.open(kDlPath, "w");
    if (!f) {
        http.end();
        return 0;
    }
    WiFiClient *s = http.getStreamPtr();
    uint8_t buf[256];
    size_t got = 0;
    bool writeOk = true;
    const uint32_t t0 = millis();
    while (got < (size_t)size && millis() - t0 < 15000) {
        const size_t want_ = size - got < sizeof(buf) ? size - got : sizeof(buf);
        const int n = s->readBytes(buf, want_);
        if (n > 0) {
            if (f.write(buf, n) != (size_t)n) {
                writeOk = false;
                break;
            }
            got += n;
        } else {
            delay(5);
        }
    }
    f.close();
    http.end();
    if (!writeOk || got != (size_t)size) {
        Serial.printf("[logo] %s: short read/write %u/%d\n", code, (unsigned)got, size);
        fsRemoveQuiet(kDlPath);
        return 0;
    }
    return got;
}

#ifdef TEST_HOOKS
Code gTestFail = "";
#endif

bool install(const char *code) {
#ifdef TEST_HOOKS
    if (gTestFail[0] && !strcmp(code, gTestFail)) {
        Serial.printf("[logo] %s: FAILED: test hook\n", code);
        return false;
    }
#endif
    const uint32_t heapBefore = ESP.getFreeHeap();
    const size_t len = download(code);
    if (!len) return false;

    Ctx *ctx = new (std::nothrow) Ctx();
    if (!ctx) {
        Serial.printf("[logo] %s: out of memory (maxAlloc=%u)\n", code, ESP.getMaxAllocHeap());
        fsRemoveQuiet(kDlPath);
        return false;
    }

    bool ok = false;
    char tmp[32], fin[32];
    binPath(code, "tmp", tmp, sizeof(tmp));
    binPath(code, "bin", fin, sizeof(fin));

    do {
        LittleFS.mkdir("/logos");
        ctx->file = LittleFS.open(tmp, "w");
        if (!ctx->file) {
            ctx->why = "open tmp";
            break;
        }
        // lv_image_header_t, little-endian: magic:8 cf:8 flags:16 | w:16 h:16 | stride:16 reserved:16
        const uint8_t hdr[12] = {kLvMagic, kLvRgb565, 0, 0, kLogo & 0xFF, kLogo >> 8, kLogo & 0xFF, kLogo >> 8,
                                 (kLogo * 2) & 0xFF, (kLogo * 2) >> 8, 0, 0};
        if (ctx->file.write(hdr, sizeof(hdr)) != sizeof(hdr)) {
            ctx->why = "write header";
            break;
        }
        ctx->bytes = sizeof(hdr);
        if (!decodeFile(ctx)) break;
        ctx->file.close();
        if (ctx->bad || ctx->bytes != 12 + (size_t)kLogo * kLogo * 2) {
            if (!ctx->why[0]) ctx->why = "wrong size";
            break;
        }
        fsRemoveQuiet(fin);
        ok = LittleFS.rename(tmp, fin);
        if (!ok) ctx->why = "rename";
    } while (false);

    if (ctx->file) ctx->file.close();
    if (!ok) fsRemoveQuiet(tmp);
    fsRemoveQuiet(kDlPath);
    Serial.printf("[logo] %s: %s%s%s (png %u B, wrote %u B, heap -%d, maxAlloc %u)\n", code, ok ? "installed" : "FAILED",
                  ok ? "" : ": ", ok ? "" : ctx->why, (unsigned)len, (unsigned)ctx->bytes,
                  (int)((int32_t)heapBefore - (int32_t)ESP.getFreeHeap()), ESP.getMaxAllocHeap());
    delete ctx;
    return ok;
}

}  // namespace

void fsPath(const char *code, char *out, size_t n) {
    char rel[24];
    binPath(code, "bin", rel, sizeof(rel));
    snprintf(out, n, "L:%s", rel);
}

bool have(const char *code) {
    char p[24];
    binPath(code, "bin", p, sizeof(p));
    return fsFileExists(p);
}

void begin() { gMux = xSemaphoreCreateMutex(); }

bool takeInstalled() {
    const bool v = gInstalled;
    gInstalled = false;
    return v;
}

void want(const char *code) {
    if (!code || !code[0]) return;
    if (gMux) xSemaphoreTake(gMux, portMAX_DELAY);
    bool found = false;
    for (int i = 0; i < gWantedN; i++)
        if (!strcmp(gWanted[i], code)) found = true;
    if (!found && gWantedN < kMaxWanted) strncpy(gWanted[gWantedN++], code, sizeof(gWanted[0]) - 1);
    if (gMux) xSemaphoreGive(gMux);
}

bool service() {
    if (!WifiManager::isConnected()) return false;
    Code wanted[kMaxWanted];
    int n;
    if (gMux) xSemaphoreTake(gMux, portMAX_DELAY);
    n = gWantedN;
    for (int i = 0; i < n; i++) strcpy(wanted[i], gWanted[i]);
    if (gMux) xSemaphoreGive(gMux);

    for (int i = 0; i < n; i++) {
        if (have(wanted[i])) {
            if (FailInfo *f = failFor(wanted[i], false)) f->used = false;
            continue;
        }
        FailInfo *f = failFor(wanted[i], false);
        if (f && (int32_t)(millis() - f->until) < 0) continue;  // backing off - try the next code instead
        if (install(wanted[i])) {
            if (f) f->used = false;
            gInstalled = true;
            return true;
        }
        f = failFor(wanted[i], true);
        if (f->count < 255) f->count++;
        uint32_t backoff = kRetryMinMs << (f->count > 5 ? 4 : f->count - 1);
        if (backoff > kRetryMaxMs) backoff = kRetryMaxMs;
        f->until = millis() + backoff;
        return false;  // one attempt per call; the next call moves on to the next code
    }
    return false;
}

#ifdef TEST_HOOKS
void testFailCode(const char *code) { strncpy(gTestFail, code ? code : "", sizeof(gTestFail) - 1); }

void testWipe() {
    char names[24][24];
    int n = 0;
    File dir = LittleFS.open("/logos");
    if (dir && dir.isDirectory())
        for (File f = dir.openNextFile(); f && n < 24; f = dir.openNextFile()) {
            const char *b = strrchr(f.name(), '/');
            b = b ? b + 1 : f.name();
            if (strlen(b) < sizeof(names[0])) strcpy(names[n++], b);
        }
    for (int i = 0; i < n; i++) {
        char p[40];
        snprintf(p, sizeof(p), "/logos/%s", names[i]);
        fsRemoveQuiet(p);
    }
    gInstalled = true;
}
#endif

}  // namespace LogoStore
