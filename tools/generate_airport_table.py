# Regenerates src/data/AirportData.cpp from OurAirports' free public dataset.
# Airport identities/locations change rarely enough that the output is
# checked in as a plain hardcoded table (see AirportTable.h) rather than
# fetched at runtime - re-run this only if that table needs updating.
#
# Usage: python3 tools/generate_airport_table.py
# (run from the project root; writes src/data/AirportData.cpp)
import csv
import os
import unicodedata
import urllib.request

AIRPORTS_CSV_URL = "https://davidmegginson.github.io/ourairports-data/airports.csv"
SRC_CSV = os.path.join(os.path.dirname(__file__), "airports.csv")
OUT_CPP = os.path.join(os.path.dirname(__file__), "..", "src", "data", "AirportData.cpp")

if not os.path.exists(SRC_CSV):
    print(f"Downloading {AIRPORTS_CSV_URL} ...")
    urllib.request.urlretrieve(AIRPORTS_CSV_URL, SRC_CSV)


def to_ascii(s):
    s = s.replace("–", "-").replace("—", "-")
    s = s.replace("‘", "'").replace("’", "'")
    s = s.replace("“", '"').replace("”", '"')
    s = unicodedata.normalize("NFKD", s)
    s = "".join(c for c in s if unicodedata.category(c) != "Mn")
    s = "".join(c for c in s if 32 <= ord(c) <= 126)
    s = " ".join(s.split())
    return s


def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


with open(SRC_CSV, newline="", encoding="utf-8") as f:
    rows = list(csv.DictReader(f))

final = [
    r
    for r in rows
    if r["type"] in ("large_airport", "medium_airport") and r["scheduled_service"] == "yes"
]
final.sort(key=lambda r: (r["iata_code"] or r["icao_code"] or r["ident"]))

lines = []
lines.append("// GENERATED FILE - do not hand-edit.")
lines.append("//")
lines.append("// Static global airport table backing DepartureVerifier's and")
lines.append("// DestinationVerifier's airport lookups (see AirportTable.h). Source:")
lines.append("// OurAirports' free, public")
lines.append(
    f"// airports.csv (https://davidmegginson.github.io/ourairports-data/airports.csv),"
)
lines.append(
    "// filtered to type in {large_airport, medium_airport} with scheduled_service=yes -"
)
lines.append(
    "// i.e. every airport worldwide that actually sees scheduled commercial service,"
)
lines.append(
    "// which is the only kind of airport a route-verification correction should ever"
)
lines.append(
    "// resolve to. Also carries field elevation (0 for the ~1% of entries missing it -"
)
lines.append(
    "// acceptable slop for DestinationVerifier's AGL estimate), for turning a raw"
)
lines.append(
    "// barometric altitude into height above THIS airport specifically. Names are"
)
lines.append(
    "// transliterated to printable ASCII (32-126) at generation"
)
lines.append(
    "// time (NFKD-normalized, combining marks stripped, then non-ASCII dropped) since"
)
lines.append("// the display font has no other glyphs - see util/Ascii.cpp's runtime")
lines.append("// equivalent for live API text, which this static data doesn't need.")
lines.append("//")
lines.append(f"// Regenerate with the generator script if OurAirports data is refreshed -")
lines.append("// airport identities/locations change rarely enough that this is checked")
lines.append("// in as a plain hardcoded table rather than fetched at runtime.")
lines.append("")
lines.append('#include "AirportTable.h"')
lines.append("")
lines.append("namespace AirportTable {")
lines.append("")
lines.append("namespace {")
lines.append("")
lines.append("const AirportEntry kAirports[] = {")

for r in final:
    icao = r["icao_code"] or r["gps_code"] or ""
    iata = r["iata_code"] or ""
    # IATA preferred, else ICAO - matches the exact convention AdsbdbClient/
    # AeroDataBoxClient already use for f.origin/f.destination, so this is
    # the only form a lookup against those fields will ever need to match.
    code = iata or icao
    name = c_escape(to_ascii(r["name"]))
    lat = float(r["latitude_deg"])
    lon = float(r["longitude_deg"])
    elev = float(r["elevation_ft"]) if r["elevation_ft"] else 0.0
    lines.append(f'    {{"{code}", "{name}", {lat}f, {lon}f, {elev}f}},')

lines.append("};")
lines.append("")
lines.append("}  // namespace")
lines.append("")
lines.append("const AirportEntry *kAirportTable = kAirports;")
lines.append("const int kAirportCount = sizeof(kAirports) / sizeof(kAirports[0]);")
lines.append("")
lines.append("}  // namespace AirportTable")
lines.append("")

with open(OUT_CPP, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print(f"wrote {len(final)} entries to {OUT_CPP}")
