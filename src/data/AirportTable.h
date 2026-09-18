#pragma once

// Static global table of every airport with scheduled commercial service
// (large or medium, per OurAirports' classification), for
// DepartureVerifier's "which real airport is actually near this
// coordinate" reverse lookup - see tools/generate_airport_table.py for how
// AirportData.cpp (the actual 3,269-entry table) is generated, and
// AirportTable.cpp for findNearest()'s search.
//
// Baked into flash as a hardcoded table rather than fetched from a live
// API: airport identities/locations change rarely enough that a
// once-in-a-while manual regeneration is fine, and every live provider
// capable of this same "nearest airport to a coordinate" lookup
// (AeroDataBox's airports/search/location) bills it against the same
// already-tight RapidAPI unit budget the route lookups use - see
// config.h's ROUTE_PROVIDER_MONTHLY_BUDGET comment.
namespace AirportTable {

struct AirportEntry {
    // IATA if this airport has one, else ICAO ("" for the handful worldwide
    // with neither) - the same "IATA-preferred" convention
    // AdsbdbClient/AeroDataBoxClient already use for Flight::origin/
    // destination, so this is the only code form a lookup against those
    // fields ever needs to match. Storing both would only waste flash.
    const char *code;
    const char *name;  // ASCII-transliterated at generation time
    float lat;
    float lon;
    float elevationFt;  // field elevation, 0 for the rare entry missing it
};

extern const AirportEntry *kAirportTable;
extern const int kAirportCount;

// Nearest table entry to (lat, lon), or nullptr if the closest one found is
// farther than maxDistanceMi away (e.g. a coordinate over open ocean, or an
// airport too small to be in this table). Linear scan - fine at this table
// size (a few thousand entries) called only for the rare
// already-flagged-suspect case DepartureVerifier uses it for, and once per
// commercial flight per poll for DestinationVerifier - not a hot loop.
const AirportEntry *findNearest(float lat, float lon, float maxDistanceMi);

// Exact match against AirportEntry::code (case-sensitive - callers already
// store codes as adsbdb/AeroDataBox give them, which are consistently
// upper-case), or nullptr if code isn't in the table. Linear scan, same
// rationale as findNearest() above.
const AirportEntry *findByCode(const char *code);

}  // namespace AirportTable
