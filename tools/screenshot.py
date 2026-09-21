#!/usr/bin/env python3
"""Captures the board's screen (needs a firmware built with SCREEN_CAPTURE: env:esp32dev-shot).

  python3 tools/screenshot.py out.png [port]                # the current screen -> out.png
  python3 tools/screenshot.py out.png --sim [scene ...]      # sim build: every scene (or the listed indices)
                                                             #   -> out_<NN>_<scene-name>.png

Sends {"cmd":"_shot"}; the board reads its panel back and dumps it as "#S x y w h <hex RGB565 LE>" lines
(~27 s at 115200 baud), which this turns into a PNG (view it with any image viewer). Adapted from
../mlb-board/tools/screenshot.py (same wire format). The dump blocks the board's loop for its duration, and
the main loop only services serial between blocking fetch cycles, so the first "#S" line can take a while.
"""
import glob, struct, sys, time, zlib
import serial

W, H = 320, 240

def write_png(path, rgb):
    raw = b"".join(b"\x00" + bytes(rgb[y * W * 3:(y + 1) * W * 3]) for y in range(H))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                          + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))

def capture(s):
    """Reads one full dump from the open port into an RGB buffer; returns (rgb, rows_seen)."""
    s.reset_input_buffer()
    s.write(b'{"cmd":"_shot"}\n')
    rgb = bytearray(W * H * 3)
    seen = set()
    end = time.time() + 240
    while time.time() < end and len(seen) < H:
        line = s.readline().decode(errors="replace").strip()
        if not line.startswith("#S "):
            continue
        parts = line.split(" ", 5)
        try:
            x, y, w, h = map(int, parts[1:5]); data = bytes.fromhex(parts[5])
        except (ValueError, IndexError):
            continue  # a log line interleaved with the dump: skip this strip
        if len(data) != w * h * 2: continue
        for row in range(h):
            for col in range(w):
                v = data[(row * w + col) * 2] | (data[(row * w + col) * 2 + 1] << 8)
                o = ((y + row) * W + x + col) * 3
                rgb[o] = ((v >> 11) & 31) * 255 // 31
                rgb[o + 1] = ((v >> 5) & 63) * 255 // 63
                rgb[o + 2] = (v & 31) * 255 // 31
            seen.add(y + row)
    return rgb, len(seen)

def call(s, obj, timeout=10):
    """Sends one JSON command and returns its reply (skipping log lines), or None."""
    import json
    s.write((json.dumps(obj) + "\n").encode())
    end = time.time() + timeout
    while time.time() < end:
        line = s.readline().decode(errors="replace").strip()
        if line.startswith("{"):
            try:
                r = json.loads(line)
            except ValueError:
                continue
            if r.get("cmd") == obj["cmd"]:
                return r
    return None

def main():
    args = [a for a in sys.argv[1:] if a != "--sim"]
    sim = "--sim" in sys.argv[1:]
    out = args[0] if args else "screen.png"
    rest = args[1:]
    port = glob.glob("/dev/cu.usbserial*")[0]
    if rest and not rest[0].isdigit():
        port, rest = rest[0], rest[1:]
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
