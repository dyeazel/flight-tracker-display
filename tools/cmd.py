#!/usr/bin/env python3
"""Send one JSON-lines command to the board and print the reply (see src/diag/SerialCommands.h).

  python3 tools/cmd.py '{"cmd":"diag"}'
  python3 tools/cmd.py '{"cmd":"debug","on":true}' [port]

Waits up to 45 s for a reply line whose "cmd" matches: the main loop only services serial between blocking
fetch/enrich cycles, which can take 10+ s (removing that latency is Phase 2's network task). Debug log lines (starting '[') are ignored. Exit code 1 on
timeout or {"ok":false}. Needs pyserial (bundled with PlatformIO's Python).
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "cyd-common", "tools"))
from board_serial import call, connect, find_port  # noqa: E402


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    obj = json.loads(sys.argv[1])
    port = sys.argv[2] if len(sys.argv) > 2 else find_port()
    s = connect(port, timeout=0.5)
    time.sleep(0.3)
    s.reset_input_buffer()  # drop anything buffered before we asked for it, so we don't match a stale reply
    reply = call(s, obj, timeout=45)
    if reply is None:
        print("no reply", file=sys.stderr)
        return 1
    print(json.dumps(reply, indent=2))
    return 0 if reply.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
