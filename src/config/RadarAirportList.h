#pragma once

#include <stddef.h>

// The radar screen's "always show these airports" list (Phase 11) - up to kMax airport codes, each drawn as
// a white "+" and code label on the radar at that airport's real location (see RadarScreen.cpp). Unlike
// CallsignList, entries are bare codes with no pin fields - format-only here (2..kCodeChars uppercase
// alphanumeric, matching AirportTable::AirportEntry::code's IATA/ICAO convention); whether a code actually
// exists in AirportTable is ConfigStore's job, same split CallsignList's pin fields already use.
//
// Persisted as one comma-separated string of codes, e.g. "ORD,MKE,MSP". Mirrors CallsignList.h's encoding
// in spirit, simplified for the no-pin-fields case.
namespace RadarAirportList {

constexpr int kMax = 6;
constexpr int kCodeChars = 4;  // IATA(3) or ICAO(4) - AirportTable::AirportEntry::code's real code length
constexpr size_t kEncodedMax = kMax * (kCodeChars + 1) + 1;

using Code = char[kCodeChars + 1];

// True for a single code: 2..kCodeChars uppercase letters/digits.
bool valid(const char *code);

// nullptr if the list is usable (zero entries is valid - it means "no extra airports"), else "bad_airport" /
// "too_many_airports" / "duplicate_airport". Only checks shape - whether each code actually exists in
// AirportTable is checked elsewhere (ConfigStore).
const char *validate(const Code *codes, int count);

// Writes "ORD,MKE,MSP" into out. Returns the length (0 for an empty list, which is not an error), or 0 with
// out untouched if `cap` is too small for a non-empty list.
size_t encode(const Code *codes, int count, char *out, size_t cap);

// Parses the encoded form. A malformed or duplicate code is skipped entirely. Stops at `max`. Returns the
// count.
int decode(const char *s, Code *out, int max);

}  // namespace RadarAirportList
