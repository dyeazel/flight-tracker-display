#!/usr/bin/env python3
"""Host-side probe of the live route/track APIs this project depends on (uses curl; no deps).
Prints the raw response and optionally saves it as a test fixture.

adsbdb (api.adsbdb.com) and OpenSky's states/all are public/unauthenticated and safe to call freely.
AeroDataBox (RapidAPI) is NOT: it's billed against this project's tightly-tracked monthly unit budget
(see config.h's ROUTE_PROVIDER_MONTHLY_BUDGET comment on a real past incident from spending it on
development testing) - this tool deliberately has no AeroDataBox mode. Its native test fixtures
(test/test_aerodataboxparse) are instead constructed by hand to match the field names AeroDataBoxClient.h
documents as confirmed against real captured responses.

  tools/fetch_sample.py adsbdb UAL1217 [--save name]
  tools/fetch_sample.py states --lamin 41.5 --lamax 42.3 --lomin -88.3 --lomax -87.3 [--save name]

Saved fixtures land in tools/fixtures/<name>.json - paste the relevant slice into a test's raw string
literal by hand (see test_adsbdbparse/test_openskyparse for the pattern) rather than loading the file at
test time, matching how ../mlb-board's EspnParse tests embed fixtures.
"""
import argparse
import subprocess
import sys


def get(url):
    return subprocess.run(["curl", "-fsS", "-m", "15", url], check=True, capture_output=True).stdout


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode", required=True)

    p_adsbdb = sub.add_parser("adsbdb", help="GET api.adsbdb.com/v0/callsign/<callsign>")
    p_adsbdb.add_argument("callsign")

    p_states = sub.add_parser("states", help="GET opensky-network.org/api/states/all (unauthenticated)")
    p_states.add_argument("--lamin", type=float, required=True)
    p_states.add_argument("--lamax", type=float, required=True)
    p_states.add_argument("--lomin", type=float, required=True)
    p_states.add_argument("--lomax", type=float, required=True)

    ap.add_argument("--save", help="save the raw response to tools/fixtures/<name>.json")
    a = ap.parse_args()

    if a.mode == "adsbdb":
        url = f"https://api.adsbdb.com/v0/callsign/{a.callsign}"
    else:
        url = (f"https://opensky-network.org/api/states/all?lamin={a.lamin}&lamax={a.lamax}"
               f"&lomin={a.lomin}&lomax={a.lomax}")

    raw = get(url)
    print(f"{a.mode}: {len(raw)} bytes")
    print(raw.decode("utf-8", errors="replace"))
    if a.save:
        import os
        os.makedirs("tools/fixtures", exist_ok=True)
        open(f"tools/fixtures/{a.save}.json", "wb").write(raw)
        print("saved", a.save)


if __name__ == "__main__":
    sys.exit(main())
