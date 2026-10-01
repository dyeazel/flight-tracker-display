#include "RadarAirportList.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace RadarAirportList {

namespace {

bool containsCode(const Code *codes, int n, const char *x) {
    for (int i = 0; i < n; i++)
        if (strcmp(codes[i], x) == 0) return true;
    return false;
}

}  // namespace

bool valid(const char *code) {
    if (!code) return false;
    const size_t len = strlen(code);
    if (len < 2 || len > (size_t)kCodeChars) return false;
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)code[i];
        if (!isupper(c) && !isdigit(c)) return false;
    }
    return true;
}

const char *validate(const Code *codes, int count) {
    if (count < 0 || count > kMax) return "too_many_airports";
    for (int i = 0; i < count; i++) {
        if (!valid(codes[i])) return "bad_airport";
        if (containsCode(codes, i, codes[i])) return "duplicate_airport";
    }
    return nullptr;
}

size_t encode(const Code *codes, int count, char *out, size_t cap) {
    size_t len = 0;
    for (int i = 0; i < count; i++) {
        const int n = snprintf(out + len, cap - len, "%s%s", i ? "," : "", codes[i]);
        if (n < 0 || (size_t)n >= cap - len) {
            if (cap) out[0] = '\0';
            return 0;
        }
        len += (size_t)n;
    }
    if (cap) out[len < cap ? len : cap - 1] = '\0';
    return len;
}

int decode(const char *s, Code *out, int max) {
    int n = 0;
    if (!s) return 0;
    while (*s && n < max) {
        if (*s == ',') {  // empty token
            ++s;
            continue;
        }
        char token[kCodeChars + 2] = {};
        size_t tlen = 0;
        while (*s && *s != ',') {
            if (tlen < sizeof(token) - 1) token[tlen++] = *s;
            ++s;
        }
        if (*s == ',') ++s;

        if (valid(token) && !containsCode(out, n, token)) {
            strcpy(out[n], token);
            n++;
        }
    }
    return n;
}

}  // namespace RadarAirportList
