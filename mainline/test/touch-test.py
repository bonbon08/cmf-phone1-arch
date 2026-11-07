#!/usr/bin/env python3
"""Shows the touches the FT3519 port reports, for a few seconds (run on the phone as root).

    sudo python3 touch-test.py [seconds]

Touch the screen while it runs. Prints every multitouch position and a summary.
"""
import os
import select
import struct
import sys
import time

NAME = "fts_ts"
EV_SYN, EV_ABS = 0x00, 0x03
ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y = 0x2F, 0x39, 0x35, 0x36
EVENT = struct.Struct("@llHHi")  # struct input_event on a 64-bit kernel


def find_device():
    for entry in sorted(os.listdir("/sys/class/input")):
        if not entry.startswith("event"):
            continue
        try:
            with open(f"/sys/class/input/{entry}/device/name") as f:
                if f.read().strip() == NAME:
                    return f"/dev/input/{entry}"
        except OSError:
            pass
    return None


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 15
    path = find_device()
    if not path:
        print(f"no input device named {NAME!r}", file=sys.stderr)
        return 1
    print("device:", path, "- touch the screen now")
    fd = os.open(path, os.O_RDONLY)
    slot, x, y = 0, None, None
    touches = 0
    coords = {}
    end = time.time() + seconds
    try:
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.2)
            if not r:
                continue
            data = os.read(fd, EVENT.size * 64)
            for off in range(0, len(data) - EVENT.size + 1, EVENT.size):
                _, _, etype, code, value = EVENT.unpack_from(data, off)
                if etype == EV_ABS:
                    if code == ABS_MT_SLOT:
                        slot = value
                    elif code == ABS_MT_POSITION_X:
                        x = value
                    elif code == ABS_MT_POSITION_Y:
                        y = value
                    elif code == ABS_MT_TRACKING_ID and value >= 0:
                        touches += 1
                elif etype == EV_SYN and code == 0 and x is not None and y is not None:
                    print(f"slot {slot}: x={x} y={y}")
                    coords[slot] = (x, y)
                    x = y = None
    finally:
        os.close(fd)
    print(f"done: {touches} touch starts, slots used {sorted(coords)}")
    return 0 if touches else 2


if __name__ == "__main__":
    sys.exit(main())
