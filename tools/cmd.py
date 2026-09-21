#!/usr/bin/env python3
"""Send one JSON-lines command to the board and print the reply (see src/diag/SerialCommands.h).

  python3 tools/cmd.py '{"cmd":"diag"}'
  python3 tools/cmd.py '{"cmd":"debug","on":true}' [port]

Waits up to 45 s for a reply line whose "cmd" matches: the main loop only services serial between blocking
fetch/enrich cycles, which can take 10+ s (removing that latency is Phase 2's network task). Debug log lines (starting '[') are ignored. Exit code 1 on
timeout or {"ok":false}. Needs pyserial (bundled with PlatformIO's Python).
"""
import glob
import json
import sys
import time

import serial


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    obj = json.loads(sys.argv[1])
    port = sys.argv[2] if len(sys.argv) > 2 else glob.glob("/dev/cu.usbserial*")[0]
    s = serial.Serial(port, 115200, timeout=0.5)
    time.sleep(0.3)
    s.reset_input_buffer()
    s.write((json.dumps(obj) + "\n").encode())
    end = time.time() + 45
    while time.time() < end:
        line = s.readline().decode(errors="replace").strip()
        if not line.startswith("{"):
            continue
        try:
            reply = json.loads(line)
        except ValueError:
            continue
        if reply.get("cmd") == obj.get("cmd") or reply.get("ok") is False:
            print(json.dumps(reply, indent=2))
            return 0 if reply.get("ok") else 1
    print("no reply", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
