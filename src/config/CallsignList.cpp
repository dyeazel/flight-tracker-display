#include "CallsignList.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace CallsignList {

namespace {

bool containsCallsign(const Entry *e, int n, const char *x) {
    for (int i = 0; i < n; i++)
        if (strcmp(e[i].callsign, x) == 0) return true;
    return false;
}

bool validChars(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (!isupper(c) && !isdigit(c)) return false;
    }
    return true;
}

// "" (unset) or 2..kCodeChars uppercase letters/digits.
bool validCode(const char *code) {
    if (!code) return false;
    const size_t len = strlen(code);
    if (len == 0) return true;
    if (len < 2 || len > (size_t)kCodeChars) return false;
    return validChars(code, len);
}

}  // namespace

bool valid(const char *callsign) {
    if (!callsign) return false;
    const size_t len = strlen(callsign);
    if (len < 2 || len > (size_t)kCallsignChars) return false;
    return validChars(callsign, len);
}

const char *validate(const Entry *entries, int count) {
    if (count < 0 || count > kMax) return "too_many_callsigns";
    for (int i = 0; i < count; i++) {
        if (!valid(entries[i].callsign)) return "bad_callsign";
        if (containsCallsign(entries, i, entries[i].callsign)) return "duplicate_callsign";
        if (!validCode(entries[i].origin)) return "bad_origin";
        if (!validCode(entries[i].destination)) return "bad_destination";
    }
    return nullptr;
}

size_t encode(const Entry *entries, int count, char *out, size_t cap) {
    size_t len = 0;
    for (int i = 0; i < count; i++) {
        const int n = snprintf(out + len, cap - len, "%s%s:%s:%s", i ? "," : "", entries[i].callsign,
                               entries[i].origin, entries[i].destination);
        if (n < 0 || (size_t)n >= cap - len) {
            if (cap) out[0] = '\0';
            return 0;
        }
        len += (size_t)n;
    }
    if (cap) out[len < cap ? len : cap - 1] = '\0';
    return len;
}

int decode(const char *s, Entry *out, int max) {
    int n = 0;
    if (!s) return 0;
    while (*s && n < max) {
        if (*s == ',') {  // empty token
            ++s;
            continue;
        }
        // Split one comma-delimited token into up to 3 colon-delimited fields: callsign[:origin[:destination]].
        // A bare callsign (no colons at all) is Phase 7's original encoding - still valid, just unpinned.
        // Sized generously (kCallsignChars, the largest field, +1 to detect an over-length field, +1 NUL)
        // for all three fields - a pin field only ever needs kCodeChars of it.
        char fields[3][kCallsignChars + 2] = {};
        int field = 0;
        size_t flen = 0;
        while (*s && *s != ',') {
            if (*s == ':' && field < 2) {
                field++;
                flen = 0;
            } else if (flen < sizeof(fields[field]) - 1) {
                fields[field][flen++] = *s;
            }
            ++s;
        }
        if (*s == ',') ++s;

        if (valid(fields[0]) && !containsCallsign(out, n, fields[0])) {
            strcpy(out[n].callsign, fields[0]);
            strcpy(out[n].origin, validCode(fields[1]) ? fields[1] : "");
            strcpy(out[n].destination, validCode(fields[2]) ? fields[2] : "");
            n++;
        }
    }
    return n;
}

}  // namespace CallsignList
