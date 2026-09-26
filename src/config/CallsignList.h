#pragma once

#include <stddef.h>

// The tracked-flights list: up to kMax entries, each a callsign plus an optional origin/destination pin
// (Phase 8 - see FlightSelector::enrich()'s "Route pinning" comment for why: a live route provider can be
// wrong or stale, so a pin lets the user assert the correct one). A callsign is an uppercase alphanumeric
// string up to kCallsignChars long (matches Flight::callsign's 12-byte buffer budget); a pin is "" (unset)
// or a 2..kCodeChars uppercase alphanumeric airport code - format only, matching Flight::origin/
// destination's real-world IATA(3)/ICAO(4) codes. Pure logic (no Arduino/NVS) so it's unit-tested on the
// host: pio test -e native. Existence-checking a pin against the real airport table (AirportTable) is
// ConfigStore's job, not this module's - same split as ConfigStore already validates lat/lon ranges itself.
//
// Persisted as one comma-separated string of colon-joined fields, e.g. "UAL1217:ORD:DEN,DAL456,N441MV::"
// (a callsign alone, with no colons, means no pin - this is also exactly Phase 7's original encoding, so
// existing NVS data decodes unchanged). Mirrors ../../../mlb-board's TeamList in spirit, not shape.
namespace CallsignList {

constexpr int kMax = 8;
constexpr int kCallsignChars = 11;  // + NUL = Flight::callsign's 12-byte budget
constexpr int kCodeChars = 4;       // IATA(3) or ICAO(4) - Flight::origin/destination's real code length
constexpr size_t kEncodedMax = kMax * (kCallsignChars + 2 * kCodeChars + 3) + 1;

using Buf = char[kCallsignChars + 1];
using Code = char[kCodeChars + 1];

struct Entry {
    Buf callsign = "";
    Code origin = "";       // "" = no pin
    Code destination = "";  // "" = no pin
};

// True for a single callsign: 2..kCallsignChars uppercase letters/digits.
bool valid(const char *callsign);

// nullptr if the list is usable (zero entries is valid - it means "no specific flights, show whatever's
// nearby"), else "bad_callsign" / "bad_origin" / "bad_destination" / "too_many_callsigns" /
// "duplicate_callsign". A pin field is checked for shape only (empty, or 2..kCodeChars uppercase
// letters/digits) - whether the code actually exists is checked elsewhere (ConfigStore).
const char *validate(const Entry *entries, int count);

// Writes "UAL1217:ORD:DEN,DAL456::" into out. Returns the length (0 for an empty list, which is not an
// error - see validate()), or 0 with out untouched if `cap` is too small for a non-empty list.
size_t encode(const Entry *entries, int count, char *out, size_t cap);

// Parses the encoded form. An entry with a malformed/duplicate callsign is skipped entirely; a malformed
// pin field within an otherwise-valid entry is dropped (decodes as unpinned) rather than dropping the
// whole entry. Stops at `max`. Returns the count.
int decode(const char *s, Entry *out, int max);

}  // namespace CallsignList
