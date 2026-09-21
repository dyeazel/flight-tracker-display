#!/usr/bin/env python3
"""Force-tests the watchdog / diagnostics recovery paths on real hardware (adapted from ../mlb-board's tool).

Needs the env:esp32dev-test firmware (adds _hang/_blocknet/_slownet) built with shortened thresholds:

  stall | slownet | blocknet   PLATFORMIO_BUILD_FLAGS="-DSTALL_RESET_MS=45000UL"     pio run -e esp32dev-test -t upload
  scheduled                    PLATFORMIO_BUILD_FLAGS="-DSELF_REBOOT_INTERVAL_MS=100000UL" pio run -e esp32dev-test -t upload

  python3 tools/test_watchdog.py stall|slownet|blocknet|scheduled

Keeps ONE serial session open the whole time. Compares diag values before/after instead of resetting counters,
so the board's lifetime history (boots, fetch ok/fail) is left intact. Remember to flash the normal
env:esp32dev firmware afterwards. Timings note: the main loop only services serial between blocking
fetch/enrich cycles, so commands can take a while to be answered.
"""
import glob
import json
import re
import sys
import time

import serial


class Sess:
    def __init__(self):
        self.s = serial.Serial(glob.glob("/dev/cu.usbserial*")[0], 115200, timeout=0.3)
        self.log = []

    def read(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            line = self.s.readline().decode(errors="replace").strip()
            if line:
                self.log.append(line)
                yield line

    def wait(self, pattern, timeout, show=True):
        t0 = time.time()
        for line in self.read(timeout):
            if show and line.startswith(("[diag]", "[net-t]", "[wifi]", "[main]")):
                print(f"  {time.time() - t0:6.1f}s {line[:140]}")
            if re.search(pattern, line):
                return line
        return None

    def call(self, obj, timeout=120):
        self.s.write((json.dumps(obj) + "\n").encode())
        for line in self.read(timeout):
            if line.startswith("{"):
                try:
                    r = json.loads(line)
                except ValueError:
                    continue
                if r.get("cmd") == obj["cmd"]:
                    return r
        raise TimeoutError(obj)


results = []


def check(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name, ("" if cond else str(extra)))
    results.append(bool(cond))


def diag(s):
    return s.call({"cmd": "diag"})


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else ""
    s = Sess()
    print(f"== {which}")

    if which == "stall":  # firmware: -DSTALL_RESET_MS=45000UL
        before = diag(s)
        print("  boot reason:", before["boot_reason"], "| stall restarts so far:", before["restarts"]["stall watchdog"])
        s.call({"cmd": "_hang"})
        line = s.wait(r"\[diag\] boot #", 120)
        check("board restarted by the stall ISR", line is not None)
        check("reason recognised as 'stall watchdog'", line and "stall watchdog" in line, line)
        after = diag(s)
        check("stall counter incremented", after["restarts"]["stall watchdog"] == before["restarts"]["stall watchdog"] + 1, after["restarts"])
        check("boot counter incremented", after["boots"] == before["boots"] + 1, (before["boots"], after["boots"]))

    elif which == "slownet":  # firmware: -DSTALL_RESET_MS=45000UL; each network call stalls 20 s, no progress marking inside it
        before = diag(s)
        s.call({"cmd": "_slownet", "ms": 20000})
        print("  every network call now stalls 20 s; watching 240 s for an unwanted restart...")
        line = s.wait(r"\[diag\] boot #", 240)
        check("no stall restart while a slow provider is merely slow (enrich marks progress per flight/attempt)", line is None, line)
        s.call({"cmd": "_slownet", "ms": 0}, timeout=300)
        after = diag(s)
        check("boot counter unchanged", after["boots"] == before["boots"], (before["boots"], after["boots"]))
        slow = max(after["sources"][k]["max_ms"] for k in ("states", "adsbdb", "aerodatabox", "track", "token"))
        check("per-source timing recorded the stalls (max_ms >= 20000)", slow >= 20000, slow)

    elif which == "blocknet":  # any firmware built with TEST_HOOKS
        before = diag(s)
        s.call({"cmd": "_blocknet", "on": True})
        print("  network now 'unreachable'; waiting for consecutive failures...")
        d = None
        for _ in range(12):
            time.sleep(15)
            d = diag(s)
            if d["poll_consecutive_fails"] >= 2:
                break
        check("consecutive failures counted", d and d["poll_consecutive_fails"] >= 2, d and d["poll_consecutive_fails"])
        check("last failure names the cause", d and "blocked" in d.get("last_failure", ""), d and d.get("last_failure"))
        check("lifetime fail counter grew", d and d["poll_fail"] > before["poll_fail"], (before["poll_fail"], d and d["poll_fail"]))
        s.call({"cmd": "_blocknet", "on": False})
        ok0 = d["poll_ok_this_boot"]
        recovered = None
        for _ in range(12):
            time.sleep(15)
            recovered = diag(s)
            if recovered["poll_consecutive_fails"] == 0 and recovered["poll_ok_this_boot"] > ok0:
                break
        check("recovers on its own once the network is back", recovered and recovered["poll_consecutive_fails"] == 0 and recovered["poll_ok_this_boot"] > ok0, recovered)
        check("no restart during the outage", recovered and recovered["boots"] == before["boots"], recovered and recovered["boots"])

    elif which == "scheduled":  # firmware: -DSELF_REBOOT_INTERVAL_MS=100000UL
        print("  waiting for the first scheduled reboot (100 s of uptime)...")
        line = s.wait(r"restarting: scheduled", 240)
        check("scheduled reboot happened (from loop context)", line is not None)
        boot = s.wait(r"\[diag\] boot #", 90)
        check("came back with reason 'scheduled'", boot is not None and "scheduled" in boot, boot)

    else:
        print(__doc__)
        return 2

    print("\nALL PASSED" if all(results) else "\nFAILURES")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
