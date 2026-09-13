#!/usr/bin/env python3
"""A packed core -> the staged form the on-board BL616's stage 2 loads.

    mkstage.py bin/uknc.bin build/onboard/stage-uknc.bin

One 4 KB descriptor sector, then the bitstream unchanged: onboard/stage.h
is the layout and the two must agree by hand.  Checks the bitstream the
way mkimage.py and flash_install() do - a5 c3 at 0x16, IDCODE 0x0000081b
at 0x1c - so nothing but a GW2AR-18 bitstream can be staged.

    mkstage.py --log build/onboard/log.bin

decodes a LOG sector read back from the chip.
"""

import struct
import sys
import zlib

STAGE_DATA_OFF = 0x1000
STAGE_MAGIC = 0x314C5554          # "TUL1"
STAGE_MAX_LEN = 1024 * 1024
GW2AR18_IDCODE = 0x0000081B
MAGIC_OFF = 0x16
IDCODE_OFF = 0x1C

LOG_TAGS = {
    0x01: "start (build)", 0x02: "flash jedec id", 0x03: "flash size",
    0x04: "descriptor (0 ok, 1 magic, 2 length, 3 idcode)",
    0x05: "bitstream length", 0x06: "crc (0 ok)", 0x07: "FPGA idcode",
    0x08: "status before", 0x09: "erase (0 ok, else step)",
    0x0A: "status after erase", 0x0B: "status after load",
    0x0C: "DONE_FINAL", 0x0D: "load ms", 0x0E: "end (0 loaded, else stopped at)",
    0x10: "link pins at start {GPIO 13 (RX), GPIO 11 (TX)}",
    0x11: "link RX byte", 0x12: "link TX byte", 0x13: "listening from (ms)",
}
STATUS_BITS = {
    0: "CRC_ERROR", 1: "BAD_COMMAND", 2: "ID_VERIFY_FAILED", 3: "TIMEOUT",
    5: "MEMORY_ERASE", 6: "PREAMBLE", 7: "SYSTEM_EDIT_MODE",
    8: "PRG_SPIFLASH_DIRECT", 10: "NON_JTAG_CNF_ACTIVE", 11: "BYPASS",
    12: "GOWIN_VLD", 13: "DONE_FINAL", 14: "SECURITY_FINAL", 15: "READY",
    16: "POR", 17: "FLASH_LOCK",
}


def status_str(v):
    return " ".join(n for b, n in STATUS_BITS.items() if v & (1 << b)) or "-"


def decode_log(path):
    d = open(path, "rb").read()
    n = 0
    for off in range(0, len(d) - 7, 8):
        tag, val = struct.unpack("<II", d[off:off + 8])
        if tag == 0xFFFFFFFF and val == 0xFFFFFFFF:
            break
        name = LOG_TAGS.get(tag, "?")
        extra = ""
        if tag in (0x08, 0x0A, 0x0B):
            extra = "  " + status_str(val)
        if tag in (0x11, 0x12):
            extra = f"  {val & 0xff:02x} at {val >> 8} ms"
        print(f"  {tag:02x}  {val:08x}  {name}{extra}")
        n += 1
    if n == 0:
        print("  (empty - stage 2 never ran, or never got to write)")
    return 0


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--log":
        return decode_log(sys.argv[2])
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    if data[MAGIC_OFF:MAGIC_OFF + 2] != b"\xa5\xc3":
        sys.exit(f"{src}: no a5 c3 at 0x{MAGIC_OFF:x} - not a Gowin bitstream")
    idcode = struct.unpack(">I", data[IDCODE_OFF:IDCODE_OFF + 4])[0]
    if idcode != GW2AR18_IDCODE:
        sys.exit(f"{src}: IDCODE {idcode:08x} at 0x{IDCODE_OFF:x}, want {GW2AR18_IDCODE:08x}")
    if not 0 < len(data) <= STAGE_MAX_LEN:
        sys.exit(f"{src}: {len(data)} bytes, not a bitstream for this device")
    name = src.rsplit("/", 1)[-1].rsplit(".", 1)[0].encode()[:15]
    desc = struct.pack("<IIII16s", STAGE_MAGIC, len(data), zlib.crc32(data) & 0xFFFFFFFF,
                       GW2AR18_IDCODE, name)
    out = desc + b"\xff" * (STAGE_DATA_OFF - len(desc)) + data
    open(dst, "wb").write(out)
    print(f"{dst}: {name.decode()} {len(data)} bytes, crc32 {zlib.crc32(data) & 0xFFFFFFFF:08x}, "
          f"{len(out)} bytes staged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
