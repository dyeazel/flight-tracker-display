#!/usr/bin/env python3
"""Captures the board's screen (needs a firmware built with SCREEN_CAPTURE: env:esp32dev-shot).

  python3 tools/screenshot.py out.png [port]                # the current screen -> out.png
  python3 tools/screenshot.py out.png --sim [scene ...]      # sim build: every scene (or the listed indices)
                                                             #   -> out_<NN>_<scene-name>.png

Sends {"cmd":"_shot"}; the board reads its panel back and dumps it as "#S x y w h <hex RGB565 LE>" lines
(~27 s at 115200 baud), which this turns into a PNG (view it with any image viewer). The dump blocks the
board's loop for its duration, and the main loop only services serial between blocking fetch cycles, so
the first "#S" line can take a while. Wire format and the capture()/write_png() decoding are
../../cyd-common's shared board_serial (both apps run the same 320x240 panel) - only the CLI and scene
selection (--sim) are specific to this app's own "_sim" command.
"""
import glob
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "cyd-common", "tools"))
from board_serial import H, call, capture, write_png  # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if a != "--sim"]
    sim = "--sim" in sys.argv[1:]
    out = args[0] if args else "screen.png"
    rest = args[1:]
    port = glob.glob("/dev/cu.usbserial*")[0]
    if rest and not rest[0].isdigit():
        port, rest = rest[0], rest[1:]
    import serial
    s = serial.Serial(port, 115200, timeout=1)
    time.sleep(0.5)  # opening the port does not reset this board
    ok = True
    if not sim:
        rgb, rows = capture(s)
        write_png(out, rgb)
        print(f"wrote {out} ({rows}/{H} rows)", flush=True)
        return 0 if rows == H else 1
    info = call(s, {"cmd": "_sim", "n": -1})
    if not info:
        print("no reply to _sim (is this the esp32dev-sim firmware?)", file=sys.stderr)
        return 1
    scenes = [int(a) for a in rest] or list(range(info["count"]))
    stem = out[:-4] if out.endswith(".png") else out
    for k in scenes:
        r = call(s, {"cmd": "_sim", "n": k})
        time.sleep(1.0)  # let the loop draw the scene
        rgb, rows = capture(s)
        path = f"{stem}_{k:02d}_{r['name']}.png"
        write_png(path, rgb)
        print(f"wrote {path} ({rows}/{H} rows)", flush=True)
        ok = ok and rows == H
    call(s, {"cmd": "_sim", "n": -1})  # resume cycling
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
