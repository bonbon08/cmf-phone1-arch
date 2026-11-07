#!/usr/bin/env python3
"""Checks haptic_config.bin against the rules rt6010_apply_config() enforces.

The driver reads twelve little-endian u32 fields, followed by the waveform. This script
decodes the same layout and repeats the driver's checks, so a firmware file that the
driver would reject is caught on the PC.

Usage: check_haptic_config.py <haptic_config.bin>
"""
import struct
import sys

RT6010_MAX_RAM_SIZE = 1536
PLAYLIST_LEN = 6  # rt6010_playlist[] in rt6010-haptic.c
HDR = struct.Struct("<12I")
FIELDS = ("chip_id", "reg_size", "ram_size", "f0", "list_base_addr", "wave_base_addr",
          "fifo_ae", "fifo_af", "boost_mode", "gain", "boost_vol", "waveform_size")


def check(data: bytes):
    """Returns (errors, cfg). cfg is None if the header cannot be read."""
    errors = []
    if len(data) < HDR.size:
        return [f"file too short: {len(data)} bytes"], None
    cfg = dict(zip(FIELDS, HDR.unpack_from(data)))
    wave_len = cfg["waveform_size"]
    if len(data) < HDR.size + wave_len:
        errors.append("waveform_size exceeds file size")
    if cfg["wave_base_addr"] > RT6010_MAX_RAM_SIZE:
        errors.append("wave_base_addr above RAM size")
    if cfg["list_base_addr"] >= cfg["wave_base_addr"]:
        errors.append("list_base_addr must be below wave_base_addr")
    if wave_len > RT6010_MAX_RAM_SIZE - cfg["wave_base_addr"]:
        errors.append("waveform does not fit into RAM above wave_base_addr")
    if PLAYLIST_LEN > cfg["wave_base_addr"] - cfg["list_base_addr"]:
        errors.append("playlist does not fit into the list section")
    return errors, cfg


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    data = open(sys.argv[1], "rb").read()
    errors, cfg = check(data)
    if cfg:
        for name in FIELDS:
            print(f"{name:16s} 0x{cfg[name]:x} ({cfg[name]})")
    print(f"file size        {len(data)} bytes, header {HDR.size} bytes")
    if errors:
        print("FAIL:")
        for e in errors:
            print("  -", e)
        return 1
    print("OK: all checks of rt6010_apply_config() pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
