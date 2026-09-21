#!/usr/bin/env python3
"""Summarises soak/soak-samples.csv and soak/soak-events.log: is heap flat, how many polls failed and *why*,
what restarted. Reads what tools/soak_logger.py wrote.

  python3 tools/soak_summary.py
"""
import csv
import re
import statistics as st
from collections import Counter
from pathlib import Path

D = Path(__file__).resolve().parent.parent / "soak"
rows = list(csv.DictReader(open(D / "soak-samples.csv")))
if not rows:
    raise SystemExit("no samples yet")


def col(k):
    out = []
    for r in rows:
        try:
            out.append(float(r[k]))
        except (ValueError, TypeError, KeyError):
            pass
    return out


def slope(xs, ys):
    n = len(xs)
    if n < 2:
        return 0.0
    mx, my = sum(xs) / n, sum(ys) / n
    den = sum((x - mx) ** 2 for x in xs)
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den if den else 0.0


hours = float(rows[-1]["elapsed_s"]) / 3600
print(f"samples: {len(rows)} over {hours:.2f} h  ({rows[0]['ts']} .. {rows[-1]['ts']})")

# Heap only means something within one boot: split into segments at every uptime reset.
segs, cur = [], [rows[0]]
for r in rows[1:]:
    if float(r["uptime_s"]) < float(cur[-1]["uptime_s"]):
        segs.append(cur)
        cur = []
    cur.append(r)
segs.append(cur)
print(f"boots seen in the log: {len(segs)}")
for k, label in [("heap_free", "free heap"), ("heap_max_alloc", "largest free block"), ("heap_min_free", "lowest free heap ever")]:
    v = col(k)
    print(f"{label:24} min {min(v):8.0f}  median {st.median(v):8.0f}  max {max(v):8.0f}")
for i, sg in enumerate(segs):
    if len(sg) < 5:
        continue
    xs = [float(r["uptime_s"]) / 3600 for r in sg]
    ys = [float(r["heap_free"]) for r in sg]
    ma = [float(r["heap_max_alloc"]) for r in sg]
    span = xs[-1] - xs[0]
    trend = f"free-heap trend {slope(xs, ys):+.1f} B/h" if span >= 1.0 else "free-heap trend n/a (< 1 h)"
    print(f"  boot segment {i + 1}: {len(sg)} samples, {span:.2f} h, {trend}, "
          f"largest block {min(ma):.0f}..{max(ma):.0f}")

last = rows[-1]
ok, fail = float(last["poll_ok_total"]), float(last["poll_fail_total"])
first = rows[0]
d_ok, d_fail = ok - float(first["poll_ok_total"]), fail - float(first["poll_fail_total"])
print(f"\npolls (lifetime): ok {ok:.0f}, failed {fail:.0f}")
print(f"polls (during this log): ok {d_ok:.0f}, failed {d_fail:.0f} -> failure rate "
      f"{100 * d_fail / max(1, d_ok + d_fail):.2f}%")
print("WiFi connected in", f"{100 * sum(int(r['wifi']) for r in rows) / len(rows):.1f}% of samples; RSSI median",
      st.median(col("rssi")))
print("restart counters (latest):", last["restarts_json"])
print("\nslowest single call seen per source (ms, this boot's max at the last sample):")
for s in ["token", "states", "track", "adsbdb", "aerodatabox"]:
    print(f"  {s:12} {last.get(f'{s}_max_ms', '')}  (failures this boot: {last.get(f'{s}_fail', '')})")

events = (D / "soak-events.log").read_text().splitlines() if (D / "soak-events.log").exists() else []
fails = [e for e in events if "[net-t]" in e and " FAIL " in e]
if fails:
    print(f"\nnetwork failures logged: {len(fails)}")
    by = Counter()
    by_hour = Counter()
    for e in fails:
        m = re.search(r"\[net-t\] (\w+) FAIL http=(-?\d+) reason=(\S+)", e)
        if m:
            by[(m.group(1), m.group(3), m.group(2))] += 1
        by_hour[e[11:13]] += 1
    for (src, reason, code), n in by.most_common():
        print(f"  {n:4}x {src} reason={reason} http={code}")
    print("  by hour of day:", ", ".join(f"{h}:00={n}" for h, n in sorted(by_hour.items())))

print("\nevents (restarts, WiFi, unresponsive; [net-t] lines are summarised above):")
for e in events:
    if "[net-t]" in e or "[opensky]" in e:
        continue
    print("  " + e)
