#!/usr/bin/env python3
"""Double-tap to wake with the FT3519 port (run on the phone as root, Nothing shell running).

    sudo python3 gesture-test.py [seconds]

Turns the screen off through nothing-wm, then waits for KEY_WAKEUP from the port's
"fts_ts gestures" device: double-tap the dark screen while it runs. nothing-wm turns the
screen on for that key. Exit code 0 when the key came and the screen is on again.
"""
import glob
import json
import os
import select
import socket
import struct
import sys
import time

NAME = "fts_ts gestures"
EV_KEY, KEY_WAKEUP = 0x01, 143
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


def wm(cmd):
    socks = glob.glob("/run/user/*/nothing-wm.*.sock")
    if not socks:
        return None
    s = socket.socket(socket.AF_UNIX)
    s.connect(socks[0])
    s.sendall((cmd + "\n").encode())
    reply = s.recv(1 << 20).decode() if cmd == "state" else ""
    s.close()
    return reply


def screen_off():
    state = wm("state")
    return state is not None and json.loads(state).get("screen_off")


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20
    path = find_device()
    if not path:
        print(f"no input device named {NAME!r} (is the port bound?)", file=sys.stderr)
        return 1
    if wm("screen off") is None:
        print("nothing-wm is not running", file=sys.stderr)
        return 1
    time.sleep(1)
    print("device:", path, "- screen is off, double-tap it now")
    fd = os.open(path, os.O_RDONLY)
    got = False
    end = time.time() + seconds
    try:
        while time.time() < end and not got:
            r, _, _ = select.select([fd], [], [], 0.2)
            if not r:
                continue
            data = os.read(fd, EVENT.size * 16)
            for off in range(0, len(data) - EVENT.size + 1, EVENT.size):
                _, _, etype, code, value = EVENT.unpack_from(data, off)
                if etype == EV_KEY and code == KEY_WAKEUP and value == 1:
                    got = True
    finally:
        os.close(fd)
    time.sleep(1)
    on = not screen_off()
    if not got:
        wm("screen on")
    print(f"KEY_WAKEUP: {'yes' if got else 'no'}, screen on afterwards: {'yes' if on else 'no'}")
    return 0 if got and on else 2


if __name__ == "__main__":
    sys.exit(main())
