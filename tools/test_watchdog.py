#!/usr/bin/env python3
"""Force-tests the watchdog / health / diagnostics recovery paths on real hardware (adapted from
../mlb-board's tool; rewritten 2026-09-22 for the NetTask/Health split - see docs in src/watchdog/).

Needs the env:esp32dev-test firmware (adds _hang/_blocknet/_slownet/_wifidown) built with shortened
thresholds - each test's line below says which:

  python3 tools/test_watchdog.py stall|slownet|netstuck|nodata|wifidown|scheduled|blocknet

Keeps ONE serial session open the whole time. Compares diag values before/after instead of resetting
counters, so the board's lifetime history (boots, fetch ok/fail) is left intact. Remember to flash the
normal env:esp32dev firmware when done. The UI task services serial every ~2s regardless of what NetTask
is doing (that independence is the whole point of the NetTask/Health split - see the "stall" and
"netstuck" tests in particular, which specifically check diag keeps answering promptly throughout).
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
            if show and line.startswith(("[diag]", "[net-t]", "[wifi]", "[main]", "[health]")):
                print(f"  {time.time() - t0:6.1f}s {line[:140]}")
            if re.search(pattern, line):
                return line
        return None

    def call(self, obj, timeout=10):
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


def timed_diag(s):
    """diag(), returning (reply, seconds_taken) - used to prove the UI task never blocked. Returns
    (None, timeout) if the board doesn't answer at all, which is expected if it happens to be mid-reboot
    at that exact moment - callers should treat that as inconclusive for that one sample, not a failure."""
    t0 = time.time()
    try:
        d = diag(s)
    except TimeoutError:
        return None, time.time() - t0
    return d, time.time() - t0


def watch_for_restart(s, deadline_s, poll_every=10):
    """Polls diag every poll_every seconds until deadline_s elapses or a fresh boot line appears - whichever
    comes first. Returns (boot_line_or_None, list_of_(reply,elapsed)_samples, saw_net_task_growing).

    Scans s.log (every line seen by ANY read so far, including timed_diag()'s own internal read loop while
    it waits for a JSON reply) rather than doing a second, separately-windowed read for the boot marker -
    a boot line that arrives while timed_diag() is mid-call gets consumed and appended to s.log there, and
    a later, fresh s.wait() call would never see it again (found by hitting exactly this while writing the
    wifidown test - a real reboot happened but went undetected)."""
    t0 = time.time()
    samples = []
    saw_growing = False
    scanned = len(s.log)

    def scan_new_log():
        nonlocal scanned
        for line in s.log[scanned:]:
            if re.search(r"\[diag\] boot #", line):
                scanned = len(s.log)
                return line
        scanned = len(s.log)
        return None

    while time.time() - t0 < deadline_s:
        d, dt = timed_diag(s)
        samples.append((d, dt))
        if d is not None:
            print(f"  t={time.time() - t0:5.1f}s diag replied in {dt:.1f}s"
                  f" net_task_ms_since_loop={d.get('net_task_ms_since_loop')}")
            if d.get("net_task_ms_since_loop", 0) > 20000:
                saw_growing = True
        found = scan_new_log()
        if found:
            return found, samples, saw_growing
        for line in s.read(poll_every):
            if line.startswith(("[diag]", "[health]", "[net-t]", "[wifi]")):
                print(f"  t={time.time() - t0:5.1f}s {line[:140]}")
        found = scan_new_log()
        if found:
            return found, samples, saw_growing
    return None, samples, saw_growing


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
        # The whole point of the NetTask split: the UI task hanging must not stop NetTask from working.
        # STALL_RESET_MS (45s) comfortably exceeds one poll interval (REFRESH_SECONDS=30s), so at least one
        # more successful fetch should have landed - and been persisted to NVS - before the reset fired.
        check("NetTask kept fetching while the UI task was hung (lifetime poll_ok grew)",
              after["poll_ok"] > before["poll_ok"], (before["poll_ok"], after["poll_ok"]))

    elif which == "slownet":  # any TEST_HOOKS firmware - tests that a MODERATE stall is fully tolerated
        before = diag(s)
        s.call({"cmd": "_slownet", "ms": 5000})
        print("  every network call now stalls 5 s (well under HEALTH_NET_STUCK_MS); watching 90 s...")
        d, dt = None, 0
        for _ in range(6):
            time.sleep(15)
            d, dt = timed_diag(s)
            check(f"  diag answered promptly ({dt:.1f}s)", dt < 3, dt)
        s.call({"cmd": "_slownet", "ms": 0})
        after = diag(s)
        check("no restart of any kind (boots unchanged)", after["boots"] == before["boots"], (before["boots"], after["boots"]))
        slow = max(after["sources"][k]["max_ms"] for k in ("states", "adsbdb", "aerodatabox", "track", "token"))
        check("per-source timing recorded the stalls (max_ms >= 5000)", slow >= 5000, slow)

    elif which == "netstuck":  # firmware: -DHEALTH_NET_STUCK_MS=45000UL
        before = diag(s)
        s.call({"cmd": "_slownet", "ms": 90000})  # well past HEALTH_NET_STUCK_MS - NetTask's outer loop won't return in time
        print("  every network call now stalls 90 s; watching for 'net stuck' while checking diag stays responsive...")
        line, samples, saw_growing = watch_for_restart(s, 150)
        answered = [dt for d, dt in samples if d is not None]
        check("diag kept answering promptly throughout (UI task never blocked)", answered and max(answered) < 3, answered)
        check("net_task_ms_since_loop was seen growing past 20s while stuck", saw_growing)
        check("board restarted (net stuck)", line is not None)
        check("reason recognised as 'net stuck'", line and "net stuck" in line, line)
        after = diag(s)
        check("net stuck counter incremented", after["restarts"]["net stuck"] == before["restarts"]["net stuck"] + 1, after["restarts"])
        s.call({"cmd": "_slownet", "ms": 0})  # RAM-only flag - the fresh boot already starts with it off

    elif which == "nodata":  # firmware: -DHEALTH_NO_DATA_MS=60000UL
        before = diag(s)
        s.call({"cmd": "_blocknet", "on": True})
        print("  network 'unreachable'; watching for 'no data' while checking diag stays responsive...")
        line, samples, _ = watch_for_restart(s, 150)
        answered = [dt for d, dt in samples if d is not None]
        check("diag kept answering promptly throughout (UI task never blocked)", answered and max(answered) < 3, answered)
        check("board restarted (no data)", line is not None)
        check("reason recognised as 'no data'", line and "no data" in line, line)
        after = diag(s)
        check("no data counter incremented", after["restarts"]["no data"] == before["restarts"]["no data"] + 1, after["restarts"])
        # _blocknet is a RAM-only flag - the fresh boot starts with it off, so recovery should be automatic.
        recovered = s.wait(r"\[poll\] |\[soak\]", 60, show=False)
        if not recovered:
            d, _ = timed_diag(s)
            recovered = bool(d and d["poll_ok_this_boot"] > 0)
        check("recovers on its own after the restart", bool(recovered))

    elif which == "wifidown":  # firmware: -DHEALTH_WIFI_DOWN_MS=60000UL
        before = diag(s)
        s.call({"cmd": "_wifidown", "on": True})
        print("  WifiManager reporting down (real link untouched); watching for 'wifi down'...")
        line, samples, _ = watch_for_restart(s, 150)
        answered = [dt for d, dt in samples if d is not None]
        check("diag kept answering promptly throughout (UI task never blocked)", answered and max(answered) < 3, answered)
        connected_flags = [d["wifi_connected"] for d, _ in samples if d is not None]
        check("diag's wifi_connected correctly showed false throughout while forced down",
              connected_flags and all(c is False for c in connected_flags), connected_flags)
        check("board restarted (wifi down)", line is not None)
        check("reason recognised as 'wifi down'", line and "wifi down" in line, line)
        after = diag(s)
        check("wifi down counter incremented", after["restarts"]["wifi down"] == before["restarts"]["wifi down"] + 1, after["restarts"])
        check("the real link was never actually touched - wifi_connected is true again on the fresh boot",
              after["wifi_connected"] is True, after["wifi_connected"])

    elif which == "scheduled":  # firmware: -DSELF_REBOOT_INTERVAL_MS=100000UL
        print("  waiting for the first scheduled reboot (100 s of uptime)...")
        before = diag(s)
        print("  flights_shown just before the interval elapses may still be nonzero (deferred) - see below")
        line = s.wait(r"restarting: scheduled", 240)
        check("scheduled reboot happened (from loop/task context, not the ISR)", line is not None)
        boot = s.wait(r"\[diag\] boot #", 90)
        check("came back with reason 'scheduled'", boot is not None and "scheduled" in boot, boot)
        after = diag(s)
        check("consecutive_health_reboots NOT incremented (scheduled is not a health reboot)",
              after["consecutive_health_reboots"] == before["consecutive_health_reboots"],
              (before["consecutive_health_reboots"], after["consecutive_health_reboots"]))

    elif which == "blocknet":  # any TEST_HOOKS firmware - short outage tolerance + clean recovery
        before = diag(s)
        s.call({"cmd": "_blocknet", "on": True})
        print("  network now 'unreachable'; watching for consecutive failures...")
        d = None
        for _ in range(6):
            time.sleep(15)
            d, dt = timed_diag(s)
            check(f"  diag answered promptly ({dt:.1f}s)", d is not None and dt < 3, dt)
            if d and d["poll_consecutive_fails"] >= 2:
                break
        check("consecutive failures counted", d and d["poll_consecutive_fails"] >= 2, d and d["poll_consecutive_fails"])
        check("last failure names the cause", d and "blocked" in d.get("last_failure", ""), d and d.get("last_failure"))
        check("lifetime fail counter grew", d and d["poll_fail"] > before["poll_fail"], (before["poll_fail"], d and d["poll_fail"]))
        s.call({"cmd": "_blocknet", "on": False})
        ok0 = d["poll_ok_this_boot"] if d else before["poll_ok"]
        recovered = None
        for _ in range(6):
            time.sleep(15)
            recovered, _ = timed_diag(s)
            if recovered and recovered["poll_consecutive_fails"] == 0 and recovered["poll_ok_this_boot"] > ok0:
                break
        check("recovers on its own once the network is back", recovered and recovered["poll_consecutive_fails"] == 0 and recovered["poll_ok_this_boot"] > ok0, recovered)
        check("no restart during a short outage (below the health thresholds)", recovered and recovered["boots"] == before["boots"], recovered and recovered["boots"])

    else:
        print(__doc__)
        return 2

    print("\nALL PASSED" if all(results) else "\nFAILURES")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
