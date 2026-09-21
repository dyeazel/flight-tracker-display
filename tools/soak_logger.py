#!/usr/bin/env python3
"""Soak logger: holds the serial port for hours, samples the board's `diag` once a minute and records every
restart, network failure and WiFi event with a wall-clock timestamp, so heap trend, error rate and recovery
behaviour can be judged afterwards (adapted from ../mlb-board/tools/soak_logger.py).

  python3 tools/soak_logger.py [--hours 12] [--interval 60]
  python3 tools/soak_summary.py            # read the results

Writes (in soak/, gitignored):
  soak-raw.log      every serial line with a wall-clock timestamp
  soak-samples.csv  one row per diag sample (heap, counters, per-source timing, last failure)
  soak-events.log   restarts, [net-t] failures/slow calls, WiFi events, unresponsive periods, port loss/return

Only one program can hold the serial port. To flash or run another serial tool:
  touch soak/PAUSE      (the logger closes the port within ~2 s and waits)
  rm soak/PAUSE         (it reopens)

Replies to `diag` can be slow: the main loop only services serial between blocking fetch/enrich cycles, so
"unresponsive" is only declared after UNRESPONSIVE_S (a blocking cycle is normally well under a minute).
"""
import argparse
import csv
import glob
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parent.parent
D = ROOT / "soak"
SOURCES = ["token", "states", "track", "adsbdb", "aerodatabox"]
FIELDS = (["ts", "elapsed_s", "uptime_s", "boot_reason", "boots", "heap_free", "heap_max_alloc", "heap_min_free",
           "poll_ok_boot", "poll_fail_boot", "poll_ok_total", "poll_fail_total", "consec_fails", "last_ok_ago_s",
           "wifi", "wifi_reconnects", "rssi"]
          + [f"{s}_{k}" for s in SOURCES for k in ("fail", "max_ms")]
          + ["last_failure", "restarts_json"])
EVENT_PREFIXES = ("[diag] boot #", "[diag] restarting", "[net-t]", "[main] fetch failed", "[wifi]", "[opensky]")
UNRESPONSIVE_S = 90


def now():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def sample_row(r, t, t_start):
    row = {"ts": now(), "elapsed_s": int(t - t_start), "uptime_s": r["uptime_s"], "boot_reason": r["boot_reason"],
           "boots": r["boots"], "heap_free": r["heap_free"], "heap_max_alloc": r["heap_max_alloc"],
           "heap_min_free": r["heap_min_free"], "poll_ok_boot": r["poll_ok_this_boot"],
           "poll_fail_boot": r["poll_fail_this_boot"], "poll_ok_total": r["poll_ok"], "poll_fail_total": r["poll_fail"],
           "consec_fails": r["poll_consecutive_fails"], "last_ok_ago_s": r["last_ok_ago_s"],
           "wifi": int(r["wifi_connected"]), "wifi_reconnects": r.get("wifi_reconnects", ""), "rssi": r["rssi"],
           "last_failure": r.get("last_failure", ""), "restarts_json": json.dumps(r["restarts"], separators=(",", ":"))}
    for s in SOURCES:
        src = r.get("sources", {}).get(s, {})
        row[f"{s}_fail"] = src.get("fail", "")
        row[f"{s}_max_ms"] = src.get("max_ms", "")
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hours", type=float, default=12)
    ap.add_argument("--interval", type=float, default=60)
    a = ap.parse_args()
    D.mkdir(exist_ok=True)
    raw = open(D / "soak-raw.log", "a", buffering=1)
    events = open(D / "soak-events.log", "a", buffering=1)
    new_csv = not (D / "soak-samples.csv").exists()
    csvf = open(D / "soak-samples.csv", "a", newline="", buffering=1)
    w = csv.DictWriter(csvf, FIELDS)
    if new_csv:
        w.writeheader()

    def ev(msg):
        line = f"{now()} {msg}"
        print(line, flush=True)
        events.write(line + "\n")

    # keep the Mac awake for the duration (the display may sleep; the system must not)
    caff = subprocess.Popen(["caffeinate", "-i", "-t", str(int(a.hours * 3600) + 600)])
    t_start = time.time()
    end = t_start + a.hours * 3600
    ev(f"soak start: {a.hours} h, diag every {a.interval:.0f} s (pid {os.getpid()})")
    port = None
    last_diag = 0
    pending = None
    last_uptime = None
    unresponsive_since = None
    try:
        while time.time() < end:
            if (D / "PAUSE").exists():
                if port:
                    port.close()
                    port = None
                    ev("paused (soak/PAUSE present): serial port released")
                time.sleep(2)
                continue
            if port is None:
                try:
                    port = serial.Serial(glob.glob("/dev/cu.usbserial*")[0], 115200, timeout=0.5)
                    ev("serial port opened")
                    last_uptime = None
                except (IndexError, serial.SerialException) as e:
                    ev(f"serial port unavailable ({e.__class__.__name__}); retrying")
                    time.sleep(5)
                    continue
            try:
                line = port.readline().decode(errors="replace").strip()
            except (serial.SerialException, OSError) as e:
                ev(f"serial error: {e}; closing port")
                port = None
                time.sleep(3)
                continue
            t = time.time()
            if line:
                raw.write(f"{now()} {line}\n")
                if line.startswith(EVENT_PREFIXES):
                    ev(line)
                if line.startswith("{"):
                    try:
                        r = json.loads(line)
                    except ValueError:
                        r = None
                    if r and r.get("cmd") == "diag" and r.get("ok"):
                        pending = None
                        unresponsive_since = None
                        if last_uptime is not None and r["uptime_s"] < last_uptime:
                            ev(f"uptime went backwards ({last_uptime} -> {r['uptime_s']}): restart between samples")
                        last_uptime = r["uptime_s"]
                        w.writerow(sample_row(r, t, t_start))
            if t - last_diag >= a.interval and pending is None and port is not None:
                try:
                    port.write(b'{"cmd":"diag"}\n')
                    pending = t
                    last_diag = t
                except (serial.SerialException, OSError):
                    port = None
            if pending and t - pending > UNRESPONSIVE_S and unresponsive_since is None:
                unresponsive_since = pending
                ev(f"board UNRESPONSIVE to diag for > {UNRESPONSIVE_S} s")
                pending = None
    except KeyboardInterrupt:
        ev("interrupted")
    finally:
        ev(f"soak end after {(time.time() - t_start) / 3600:.2f} h")
        caff.terminate()


if __name__ == "__main__":
    sys.exit(main())
