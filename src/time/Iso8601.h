#pragma once

#include <time.h>

// Parses an ISO-8601 UTC timestamp string into a UTC time_t. Standalone
// (not part of TimeSync, which is scoped strictly to NTP sync/local-day
// bucketing - see its own header comment) since this is a generic string-
// to-epoch conversion any secondary route provider's response could need,
// not something tied to this device's own clock.
//
// Deliberately does NOT use mktime()/timegm(): TimeSync::begin() sets the
// global TZ to HOME_TZ_POSIX via configTzTime(), so mktime() on this core
// interprets a struct tm using THAT zone, not UTC - applying it to a
// parsed UTC timestamp would silently shift it by the local UTC offset.
// timegm() is the standard fix but its presence on this ESP32 Arduino/
// newlib toolchain wasn't confirmed, so this instead computes days-since-
// epoch directly (Howard Hinnant's days_from_civil algorithm) - correct
// regardless of the global TZ setting or toolchain libc quirks.
namespace Iso8601 {

// Accepts "YYYY-MM-DD[T ]HH:MM[:SS][Z]" (AeroDataBox has been observed to
// vary the date/time separator and whether seconds/trailing Z are
// present - see nearby_flights.py's parse_aerodatabox_time() for the same
// variance handled on the desktop port). Returns false and leaves *out
// unchanged if the string doesn't parse.
bool parseUtc(const char *s, time_t *out);

}  // namespace Iso8601
