#include "Ascii.h"

namespace Ascii {

namespace {

// Latin-1 Supplement (U+00C0-U+00FF) stripped to a base ASCII letter -
// covers accented Latin names ("Malaga", "Dusseldorf", "Sao Paulo",
// "Zurich", etc.). UTF-8 encodes this whole block as lead byte 0xC3 +
// continuation byte 0x80-0xBF, with codepoint = continuation + 0x40, so
// index this table by (continuation - 0x80).
const char kLatin1[64] = {
    // 0x80-0x8F: A U+00C0-U+00CF
    'A', 'A', 'A', 'A', 'A', 'A', 'A', 'C', 'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I',
    // 0x90-0x9F: U+00D0-U+00DF
    'D', 'N', 'O', 'O', 'O', 'O', 'O', '?', 'O', 'U', 'U', 'U', 'U', 'Y', 'T', 's',
    // 0xA0-0xAF: U+00E0-U+00EF
    'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
    // 0xB0-0xBF: U+00F0-U+00FF
    'd', 'n', 'o', 'o', 'o', 'o', 'o', '?', 'o', 'u', 'u', 'u', 'u', 'y', 't', 'y',
};

// U+2000-series "General Punctuation" third bytes (lead 0xE2, second
// 0x80) worth keeping a meaningful ASCII equivalent for - dashes and
// curly quotes, the ones actually seen coming back from adsbdb/
// AeroDataBox. Anything else in that block (bullets, ellipsis, etc.)
// falls through to the generic 3-byte drop below.
char punctuationReplacement(unsigned char third) {
    switch (third) {
        case 0x93:  // U+2013 en dash
        case 0x94:  // U+2014 em dash
            return '-';
        case 0x98:  // U+2018 left single quote
        case 0x99:  // U+2019 right single quote
            return '\'';
        case 0x9C:  // U+201C left double quote
        case 0x9D:  // U+201D right double quote
            return '"';
        default:
            return '\0';  // no good single-char equivalent - drop it
    }
}

}  // namespace

void sanitize(char *s) {
    unsigned char *src = reinterpret_cast<unsigned char *>(s);
    unsigned char *dst = reinterpret_cast<unsigned char *>(s);

    while (*src) {
        unsigned char b = *src;

        if (b < 0x80) {
            *dst++ = b;
            src++;
        } else if ((b & 0xE0) == 0xC0 && (src[1] & 0xC0) == 0x80) {
            // 2-byte sequence.
            if (b == 0xC3) {
                char repl = kLatin1[src[1] - 0x80];
                if (repl != '?') *dst++ = repl;
            }
            src += 2;
        } else if ((b & 0xF0) == 0xE0 && (src[1] & 0xC0) == 0x80 && (src[2] & 0xC0) == 0x80) {
            // 3-byte sequence.
            if (b == 0xE2 && src[1] == 0x80) {
                char repl = punctuationReplacement(src[2]);
                if (repl != '\0') *dst++ = repl;
            }
            src += 3;
        } else if ((b & 0xF8) == 0xF0 && (src[1] & 0xC0) == 0x80 && (src[2] & 0xC0) == 0x80 &&
                   (src[3] & 0xC0) == 0x80) {
            // 4-byte sequence (e.g. emoji) - no ASCII equivalent, drop whole.
            src += 4;
        } else {
            // Stray continuation byte or malformed/truncated lead byte
            // (can happen right at a strncpy truncation boundary) - drop
            // just this one byte rather than getting stuck.
            src++;
        }
    }
    *dst = '\0';
}

}  // namespace Ascii
