#pragma once

#include <stddef.h>

// The tracked-callsigns list: up to kMax entries, each an uppercase alphanumeric string up to
// kCallsignChars long (matches Flight::callsign's 12-byte buffer budget). Pure logic (no Arduino/NVS) so
// it's unit-tested on the host: pio test -e native. Persisted as one comma-separated string, e.g.
// "UAL1217,DAL456" - mirrors ../../../mlb-board's TeamList, minus the league prefix this app has no
// equivalent of.
namespace CallsignList {

constexpr int kMax = 8;
constexpr int kCallsignChars = 11;  // + NUL = Flight::callsign's 12-byte budget
constexpr size_t kEncodedMax = kMax * (kCallsignChars + 1) + 1;

using Buf = char[kCallsignChars + 1];

// True for a single callsign: 2..kCallsignChars uppercase letters/digits.
bool valid(const char *callsign);

// nullptr if the list is usable (zero entries is valid - it means "no specific flights, show whatever's
// nearby"), else "bad_callsign" / "too_many_callsigns" / "duplicate_callsign".
const char *validate(const Buf *callsigns, int count);

// Writes "UAL1217,DAL456" into out. Returns the length (0 for an empty list, which is not an error - see
// validate()), or 0 with out untouched if `cap` is too small for a non-empty list.
size_t encode(const Buf *callsigns, int count, char *out, size_t cap);

// Parses the encoded form. Malformed, invalid-charset and duplicate entries are skipped; stops at `max`.
// Returns the count.
int decode(const char *s, Buf *out, int max);

}  // namespace CallsignList
