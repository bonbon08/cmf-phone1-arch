#!/usr/bin/env python3
"""Vibrates the CMF Phone 1 motor through the input force-feedback interface.

Runs on the phone (python3, root). Finds the input device named "rt6010-haptic", uploads
one FF_RUMBLE effect and plays it a few times, then removes it again.

    sudo python3 haptic-ff-test.py [pulses] [ms]
"""
import ctypes as C
import fcntl
import os
import sys
import time

NAME = "rt6010-haptic"
EV_FF = 0x15
FF_RUMBLE = 0x50


class Trigger(C.Structure):
    _fields_ = [("button", C.c_uint16), ("interval", C.c_uint16)]


class Replay(C.Structure):
    _fields_ = [("length", C.c_uint16), ("delay", C.c_uint16)]


class Envelope(C.Structure):
    _fields_ = [("attack_length", C.c_uint16), ("attack_level", C.c_uint16),
                ("fade_length", C.c_uint16), ("fade_level", C.c_uint16)]


class Periodic(C.Structure):
    _fields_ = [("waveform", C.c_uint16), ("period", C.c_uint16), ("magnitude", C.c_int16),
                ("offset", C.c_int16), ("phase", C.c_uint16), ("envelope", Envelope),
                ("custom_len", C.c_uint32), ("custom_data", C.c_void_p)]


class Condition(C.Structure):
    _fields_ = [("right_saturation", C.c_uint16), ("left_saturation", C.c_uint16),
                ("right_coeff", C.c_int16), ("left_coeff", C.c_int16),
                ("deadband", C.c_uint16), ("center", C.c_int16)]


class Rumble(C.Structure):
    _fields_ = [("strong_magnitude", C.c_uint16), ("weak_magnitude", C.c_uint16)]


class Union(C.Union):
    _fields_ = [("rumble", Rumble), ("periodic", Periodic),
                ("condition", Condition * 2)]


class Effect(C.Structure):
    _fields_ = [("type", C.c_uint16), ("id", C.c_int16), ("direction", C.c_uint16),
                ("trigger", Trigger), ("replay", Replay), ("u", Union)]


def _ioc(direction, nr, size):
    return (direction << 30) | (size << 16) | (ord("E") << 8) | nr


EVIOCSFF = _ioc(1, 0x80, C.sizeof(Effect))   # _IOW('E', 0x80, struct ff_effect)
EVIOCRMFF = _ioc(1, 0x81, C.sizeof(C.c_int))  # _IOW('E', 0x81, int)


def find_device():
    for entry in sorted(os.listdir("/sys/class/input")):
        if not entry.startswith("event"):
            continue
        try:
            with open(f"/sys/class/input/{entry}/device/name") as f:
                if f.read().strip() == NAME:
                    return f"/dev/input/{entry}"
        except OSError:
            continue
    return None


def play(fd, effect_id, on):
    # struct input_event on a 64-bit kernel: timeval (2 x long), type, code, value
    ev = (0).to_bytes(16, "little") + (EV_FF).to_bytes(2, "little") + \
        effect_id.to_bytes(2, "little") + (1 if on else 0).to_bytes(4, "little", signed=True)
    os.write(fd, ev)


def main():
    pulses = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    length = int(sys.argv[2]) if len(sys.argv) > 2 else 300

    path = find_device()
    if not path:
        print(f"no input device named {NAME!r}; is rt6010-haptic loaded?", file=sys.stderr)
        return 1
    print("device:", path)

    effect = Effect()
    effect.type = FF_RUMBLE
    effect.id = -1
    effect.replay.length = length
    effect.u.rumble.strong_magnitude = 0xFFFF
    effect.u.rumble.weak_magnitude = 0

    fd = os.open(path, os.O_RDWR)
    try:
        buf = bytearray(bytes(effect))
        fcntl.ioctl(fd, EVIOCSFF, buf, True)
        effect_id = int.from_bytes(buf[2:4], "little", signed=True)
        print(f"effect uploaded, id {effect_id}")
        for n in range(pulses):
            play(fd, effect_id, True)
            print(f"pulse {n + 1}/{pulses} ({length} ms)")
            time.sleep(length / 1000 + 0.3)
        fcntl.ioctl(fd, EVIOCRMFF, effect_id)
        print("effect removed")
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
