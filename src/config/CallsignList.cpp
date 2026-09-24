#include "CallsignList.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace CallsignList {

namespace {
bool contains(const Buf *cs, int n, const char *x) {
    for (int i = 0; i < n; i++)
        if (strcmp(cs[i], x) == 0) return true;
    return false;
}
}  // namespace

bool valid(const char *callsign) {
    if (!callsign) return false;
    const size_t len = strlen(callsign);
    if (len < 2 || len > (size_t)kCallsignChars) return false;
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)callsign[i];
        if (!isupper(c) && !isdigit(c)) return false;
    }
    return true;
}

const char *validate(const Buf *callsigns, int count) {
    if (count < 0 || count > kMax) return "too_many_callsigns";
    for (int i = 0; i < count; i++) {
        if (!valid(callsigns[i])) return "bad_callsign";
        if (contains(callsigns, i, callsigns[i])) return "duplicate_callsign";
    }
    return nullptr;
}

size_t encode(const Buf *callsigns, int count, char *out, size_t cap) {
    size_t len = 0;
    for (int i = 0; i < count; i++) {
        const int n = snprintf(out + len, cap - len, "%s%s", i ? "," : "", callsigns[i]);
        if (n < 0 || (size_t)n >= cap - len) {
            if (cap) out[0] = '\0';
            return 0;
        }
        len += (size_t)n;
    }
    if (cap) out[len < cap ? len : cap - 1] = '\0';
    return len;
}

int decode(const char *s, Buf *out, int max) {
    int n = 0;
    if (!s) return 0;
    while (*s && n < max) {
        if (*s == ',') {  // empty token
            ++s;
            continue;
        }
        char tok[kCallsignChars + 2];  // +1 to detect an over-length token, +1 for NUL
        size_t len = 0;
        while (*s && *s != ',') {
            if (len < sizeof(tok) - 1) tok[len++] = *s;
            ++s;
        }
        tok[len] = '\0';
        if (*s == ',') ++s;
        if (valid(tok) && !contains(out, n, tok)) strcpy(out[n++], tok);
    }
    return n;
}

}  // namespace CallsignList
