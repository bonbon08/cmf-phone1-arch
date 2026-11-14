#!/usr/bin/env python3
"""Checks the speaker amplifier's ACF file against mainline's aw88261 loader.

The CMF Phone 1 has an AW88261 (chip id 0x2113 at I2C6 0x34), which mainline drives with
sound/soc/codecs/aw88261.c. The driver takes its configuration from the file named by the
device-tree property "firmware-name". This script repeats aw88395_dev_load_acf_check()
for header version 1 (file id, sizes, CRC8 of every block) and the block selection of
aw_dev_parse_scene_count_v1(): a device block (type 0) for bus/address/chip id, else a
default block (type 2) for channel/chip id.

Usage: check_aw88261_acf.py <aw882xx_acf.bin> [bus] [addr] [channel]
       (defaults: bus 6, address 0x34, channel 0)
"""
import struct
import sys

ACF_FILE_ID = 0x0A15F908
HDR_VER_V1 = 0x01000000
AW88261_CHIP_ID = 0x2113
DEV_TYPE, DEFAULT_TYPE = 0, 2
# struct aw_cfg_hdr: id, project[24], custom[16], version[4], author_id, ddt_size, ddt_num,
# hdr_offset, hdr_version, reserved[3]
HDR = struct.Struct("<I24s16s4s5I3I")
# struct aw_cfg_dde_v1: type, dev_name[16], dev_index, dev_bus, dev_addr, dev_profile,
# data_type, data_size, data_offset, data_crc, dev_profile_str[32], chip_id, reserved[4]
DDE = struct.Struct("<I16s4H4I32sI4I")


def crc8_table(poly=0x8C):
    """crc8_populate_lsb() of lib/crc8.c"""
    table = []
    for i in range(256):
        t = i
        for _ in range(8):
            t = (t >> 1) ^ (poly if t & 1 else 0)
        table.append(t)
    return table


def crc8(table, data):
    crc = 0
    for b in data:
        crc = table[(crc ^ b) & 0xFF]
    return crc


def text(b):
    return b.split(b"\0", 1)[0].decode(errors="replace")


def check(data, bus=6, addr=0x34, channel=0):
    """Returns (errors, header, blocks)."""
    if len(data) < HDR.size:
        return [f"file too short: {len(data)} bytes"], None, []
    h = HDR.unpack_from(data)
    hdr = dict(id=h[0], project=text(h[1]), custom=text(h[2]), version=h[3].hex(),
               ddt_size=h[5], ddt_num=h[6], hdr_offset=h[7], hdr_version=h[8])
    errors = []
    if hdr["id"] != ACF_FILE_ID:
        errors.append(f"not an ACF file (id 0x{hdr['id']:08x})")
    if hdr["hdr_version"] != HDR_VER_V1:
        errors.append(f"header version 0x{hdr['hdr_version']:08x}, this checks only v1")
        return errors, hdr, []
    if hdr["hdr_offset"] + hdr["ddt_size"] > len(data):
        errors.append("header plus block table overflow the file")
        return errors, hdr, []

    table = crc8_table()
    blocks = []
    total = hdr["hdr_offset"] + hdr["ddt_size"]
    for i in range(hdr["ddt_num"]):
        off = hdr["hdr_offset"] + i * DDE.size
        d = DDE.unpack_from(data, off)
        blk = dict(type=d[0], dev_name=text(d[1]), dev_index=d[2], dev_bus=d[3],
                   dev_addr=d[4], profile=d[5], data_type=d[6], data_size=d[7],
                   data_offset=d[8], data_crc=d[9], profile_str=text(d[10]), chip_id=d[11])
        total += blk["data_size"]
        end = blk["data_offset"] + blk["data_size"]
        if end > len(data):
            errors.append(f"block {i}: data overflows the file")
        elif crc8(table, data[blk["data_offset"]:end]) != blk["data_crc"]:
            errors.append(f"block {i}: CRC8 mismatch")
        blocks.append(blk)
    if total != len(data):
        errors.append(f"sizes add up to {total}, file has {len(data)} bytes")

    dev = [b for b in blocks if b["type"] == DEV_TYPE and b["dev_bus"] == bus and
           b["dev_addr"] == addr and b["chip_id"] == AW88261_CHIP_ID]
    default = [b for b in blocks if b["type"] == DEFAULT_TYPE and b["dev_index"] == channel and
               b["chip_id"] == AW88261_CHIP_ID]
    if not dev and not default:
        errors.append(f"no block for the AW88261 (chip id 0x{AW88261_CHIP_ID:04x}) at bus {bus} "
                      f"0x{addr:02x} or channel {channel}")
    return errors, hdr, blocks


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    args = [int(a, 0) for a in sys.argv[2:5]]
    errors, hdr, blocks = check(data, *args)
    if hdr:
        print(f"project {hdr['project']!r}, custom {hdr['custom']!r}, header 0x{hdr['hdr_version']:08x}, "
              f"{hdr['ddt_num']} blocks")
    for i, b in enumerate(blocks):
        print(f"  block {i}: type {b['type']} {b['dev_name']!r} index {b['dev_index']} bus {b['dev_bus']} "
              f"addr 0x{b['dev_addr']:02x} chip 0x{b['chip_id']:04x} profile {b['profile_str']!r} "
              f"data type {b['data_type']} {b['data_size']} bytes")
    for e in errors:
        print("error:", e)
    print("OK for mainline aw88261" if not errors else "would be rejected")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
