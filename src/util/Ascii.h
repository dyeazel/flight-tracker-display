#pragma once

// Sanitizing free-text fields pulled from external APIs (airport/airline
// names) down to the printable ASCII (32-126) the display's 8x8 bitmap
// font actually has glyphs for - see DisplayManager::drawText8x8(). Not
// needed for anything sourced from OpenSky (callsigns are ASCII by spec)
// or the fixed IATA/ICAO code fields, only for human-written name text.
namespace Ascii {

// In-place, null-terminated: recognized UTF-8 sequences (the common
// "smart" punctuation - en/em dash, curly quotes - and the Latin-1
// Supplement accented letters, e.g. from "Minneapolis-Saint Paul..."'s
// en dashes or names like "Malaga"/"Dusseldorf") are transliterated to a
// plain ASCII equivalent; anything else non-ASCII is dropped whole (never
// leaves a stray continuation byte behind, which would itself render as
// a garbage glyph). Output is always <= input length, so this is safe to
// run in place on the same buffer it's given.
void sanitize(char *s);

}  // namespace Ascii
