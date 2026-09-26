#!/usr/bin/env python3
"""Exercises the device's serial setup protocol (src/diag/SerialCommands.cpp's set/scan/forget, Phase 7;
origin/destination pins, Phase 8) on real hardware. Restores the original WiFi/location/radius/callsigns
settings afterwards (WiFi creds are read from include/secrets.h). Mirrors ../../mlb-board's
tools/test_setup_protocol.py.

  /Users/dean/.platformio/penv/bin/python tools/test_setup_protocol.py [/dev/cu.usbserial-XX]
"""
import glob, json, re, sys, time
from pathlib import Path
import serial

ROOT = Path(__file__).resolve().parent.parent

def secrets():
    txt = (ROOT / "include" / "secrets.h").read_text()
    g = lambda k: re.search(r'#define %s "(.*)"' % k, txt).group(1)
    return g("WIFI_SSID"), g("WIFI_PASSWORD")

class Dev:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(4)  # opening the port resets the board; let it boot
        self.s.reset_input_buffer()
    def call(self, obj, wait=5.0, raw=None):
        self.s.write((raw if raw is not None else json.dumps(obj)).encode() + b"\n")
        end = time.time() + wait
        while time.time() < end:
            line = self.s.readline().decode(errors="replace").strip()
            if line.startswith("{"):
                return json.loads(line)
        raise TimeoutError(f"no reply to {obj or raw!r}")

fails = 0
def check(name, cond, extra=""):
    global fails
    print(("PASS " if cond else "FAIL ") + name, extra if not cond else "")
    fails += (not cond)

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else glob.glob("/dev/cu.usbserial*")[0]
    ssid, pw = secrets()
    d = Dev(port)

    r = d.call({"cmd": "hello"}); check("hello", r.get("ok") and r.get("board") == "cyd", r)
    check("hello reports proto 2 and maxCallsigns 8", r.get("proto") == 2 and r.get("maxCallsigns") == 8, r)
    g = d.call({"cmd": "get"})
    check("get returns ssid/lat/lon/callsigns, no password", g["ok"] and "pass" not in g and "hasPass" in g
          and "lat" in g and "lon" in g and isinstance(g.get("callsigns"), list), g)
    orig_ssid, orig_lat, orig_lon, orig_radius, orig_callsigns = g["ssid"], g["lat"], g["lon"], g["radius_mi"], g["callsigns"]
    check("get ssid matches secrets.h", orig_ssid == ssid, g)

    dg = d.call({"cmd": "diag"})
    check("diag returns health data", dg.get("ok") and "boot_reason" in dg and "heap_max_alloc" in dg and "restarts" in dg, dg)
    # "missing" is only present once NetTask has published at least once (see SerialCommands.cpp's diag
    # handler) - right after boot (as here) that may not have happened yet, so absence is fine too.
    check("diag's missing field, if present, is a list", "missing" not in dg or isinstance(dg["missing"], list), dg)

    r = d.call({"cmd": "scan"}, wait=20); check("scan lists networks", r.get("ok") and isinstance(r.get("networks"), list) and len(r["networks"]) > 0, r)
    if r.get("ok"):
        check("scan includes own network", any(n["ssid"] == ssid for n in r["networks"]), [n["ssid"] for n in r["networks"]])

    base = {"cmd": "set", "ssid": ssid, "lat": orig_lat, "lon": orig_lon}
    check("set rejects empty ssid", d.call({**base, "ssid": ""}).get("error") == "bad_ssid")
    check("set rejects 33-char ssid", d.call({**base, "ssid": "x" * 33}).get("error") == "bad_ssid")
    check("set rejects short password", d.call({**base, "pass": "short"}).get("error") == "bad_pass")
    check("set rejects missing lat/lon", d.call({"cmd": "set", "ssid": ssid}).get("error") == "bad_json")
    check("set rejects out-of-range lat", d.call({**base, "lat": 95.0}).get("error") == "bad_lat")
    check("set rejects out-of-range lon", d.call({**base, "lon": -200.0}).get("error") == "bad_lon")
    check("set rejects out-of-range radius", d.call({**base, "radius_mi": 0.1}).get("error") == "bad_radius")
    # callsigns entries are [callsign, origin, destination] triples (Phase 8); origin/destination default to "".
    cs = lambda callsign, origin="", dest="": [callsign, origin, dest]
    C = lambda cs: d.call({**base, "callsigns": cs}).get("error")
    check("set rejects lowercase callsign", C([cs("ual1217")]) == "bad_callsign")
    check("set rejects too-long callsign", C([cs("ABCDEFGHIJKL")]) == "bad_callsign")
    check("set rejects duplicate callsigns", C([cs("UAL1217"), cs("UAL1217")]) == "duplicate_callsign")
    check("set rejects 9 callsigns", C([cs(f"UAL{1000+i}") for i in range(9)]) == "too_many_callsigns")
    check("set rejects non-string callsign entry", C([[1217, "", ""]]) == "bad_json")
    check("set rejects lowercase origin", C([cs("UAL1217", "ord")]) == "bad_origin")
    check("set rejects unrecognized origin code", C([cs("UAL1217", "ZZZZ")]) == "bad_origin")
    check("set rejects lowercase destination", C([cs("UAL1217", "", "den")]) == "bad_destination")
    check("set rejects unrecognized destination code", C([cs("UAL1217", "", "ZZZZ")]) == "bad_destination")
    check("bad json", d.call(None, raw="{nope").get("error") == "bad_json")
    check("unknown cmd", d.call({"cmd": "xyz"}).get("error") == "unknown_cmd")
    check("too long line", d.call(None, raw='{"cmd":"set","ssid":"' + "a" * 600 + '"}').get("error") == "too_long")
    g = d.call({"cmd": "get"})
    check("rejected sets changed nothing", g["ssid"] == ssid and g["lat"] == orig_lat and g["callsigns"] == orig_callsigns, g)

    r = d.call({**base, "radius_mi": 75.0, "callsigns": [cs("UAL1217"), cs("DAL456")]})
    check("set radius + 2 callsigns", r.get("ok"), r)
    g = d.call({"cmd": "get"})
    check("get returns saved radius/callsigns", g["radius_mi"] == 75.0
          and g["callsigns"] == [cs("UAL1217"), cs("DAL456")], g)

    r = d.call({**base, "callsigns": [cs("UAL1217", "ORD", "DEN")]})
    check("set a real origin/destination pin", r.get("ok"), r)
    g = d.call({"cmd": "get"})
    check("get returns the saved pin", g["callsigns"] == [cs("UAL1217", "ORD", "DEN")], g)

    r = d.call({**base, "callsigns": []})  # omitting radius_mi -> back to auto
    check("set clears callsigns, radius back to auto", r.get("ok"), r)
    g = d.call({"cmd": "get"})
    check("get reflects auto radius + no callsigns", g["callsigns"] == [] and g["radius_mi"] > 0, g)

    r = d.call({**base, "pass": "wrongpass1"}); check("set wrong password accepted", r.get("ok"), r)
    end, seen = time.time() + 60, ""
    while time.time() < end:
        g = d.call({"cmd": "get"})
        if g["wifi_state"] == "failed":
            seen = g["failure"]
            break
        time.sleep(2)
    check("wrong password reported as failure", seen in ("wrong_password", "timeout"), seen or g)

    r = d.call({"cmd": "forget"}); check("forget", r.get("ok"), r)
    g = d.call({"cmd": "get"}); check("unconfigured after forget", g["wifi_state"] == "unconfigured" and g["ssid"] == "", g)

    r = d.call({"cmd": "set", "ssid": ssid, "pass": pw, "lat": orig_lat, "lon": orig_lon,
                "radius_mi": orig_radius if orig_radius > 0 else None, "callsigns": orig_callsigns})
    check("restore original settings", r.get("ok"), r)
    end, ok = time.time() + 40, False
    while time.time() < end:
        g = d.call({"cmd": "get"})
        if g["wifi_state"] == "connected": ok = True; break
        time.sleep(2)
    check("reconnects with restored credentials", ok and g["callsigns"] == orig_callsigns, g)
    print("\nALL PASSED" if not fails else f"\n{fails} FAILED")
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
