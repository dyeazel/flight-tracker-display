#include "Iso8601.h"

#include <stdio.h>

namespace Iso8601 {

namespace {

// Howard Hinnant's days_from_civil: days since 1970-01-01 for a given
// proleptic-Gregorian y/m/d, with no dependency on libc timezone globals.
// http://howardhinnant.github.io/date_algorithms.html
long daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

}  // namespace

bool parseUtc(const char *s, time_t *out) {
    if (!s || !*s) return false;
    int year, month, day, hour, minute, second = 0;
    // Try with seconds first, then without - AeroDataBox has been
    // observed to omit seconds in some fields.
    int n = sscanf(s, "%d-%d-%d%*[T ]%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    if (n < 5) {
        n = sscanf(s, "%d-%d-%d%*[T ]%d:%d", &year, &month, &day, &hour, &minute);
        if (n < 5) return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 60) {
        return false;
    }
    long days = daysFromCivil(year, (unsigned)month, (unsigned)day);
    *out = (time_t)(days * 86400L + hour * 3600 + minute * 60 + second);
    return true;
}

}  // namespace Iso8601
