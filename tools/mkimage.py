#!/usr/bin/env python3
"""Pack one core's bitstream into the binary form the flash holds.

A Gowin .fs is ASCII: a `//` header and then lines of '0' and '1'.  The
flash wants those bits as bytes, MSB first, which is byte for byte what
Gowin's own .bin holds and what openFPGALoader writes for a .fs (checked
against UKNC Nano's committed test003.fs/.bin pair).

The result is used twice, and both matter:

  * `make flash-image` writes it to flash address 0 with openFPGALoader -
    the first flash of a board, and the only one needed;
  * it is copied onto the SD card as /cores/<name>.bin, from where the
    OSD installs it into address 0 itself (mnano/flashwr.c).

Address 0 is the only thing this board can boot, so putting the wrong
bytes there is a board that needs openFPGALoader to come back.  The
checks below are therefore the same two mnano/flashwr.c makes before it
erases anything - the Gowin preamble and this device's IDCODE - so that a
file this script accepts is a file the firmware will accept.

There used to be more to this script: three cores packed into one 3 MB
image at 1 MB slots, with each header's MultiBoot jump address checked to
close a ring.  That design is gone, because RECONFIG_N cannot be pulsed
from inside this FPGA and so the jump can never be triggered - see
.claude/docs/progress.md and .claude/docs/coreswitch.md.

  mkimage.py --fs2bin IN.fs OUT.bin
"""
import re
import sys

DEVICE       = "GW2AR-18"
IDCODE       = 0x0000081B    # what --detect reads off this FPGA
IDCODE_OFF   = 0x1C          # 32-bit, big-endian
MAGIC_OFF    = 0x16          # a5 c3, the Gowin preamble
FLASH_SLOT   = 0x100000      # the most one core may occupy at address 0


def fs_load(path):
    """-> (header dict, packed bytes)"""
    header, bits = {}, []
    with open(path, "rb") as f:
        for line in f:
            if line.startswith(b"//"):
                m = re.match(rb"//([^:]+):\s*(.*)", line.strip())
                if m:
                    header[m.group(1).decode()] = m.group(2).decode()
                continue
            s = line.strip()
            if s:
                bits.append(s)
    data = b"".join(bits)
    if set(data) - {ord("0"), ord("1")}:
        sys.exit(f"{path}: not a Gowin .fs (bits expected)")
    if len(data) % 8:
        sys.exit(f"{path}: bit count not a multiple of 8")
    return header, int(data, 2).to_bytes(len(data) // 8, "big")


def check(path, header, packed):
    dev = header.get("Device", "?")
    if dev != DEVICE:
        sys.exit(f"{path}: device {dev}, want {DEVICE}")
    if len(packed) < 0x10000 or len(packed) > FLASH_SLOT:
        sys.exit(f"{path}: {len(packed)} bytes will not do - the firmware "
                 f"takes 0x10000..0x{FLASH_SLOT:x}")
    if packed[MAGIC_OFF:MAGIC_OFF + 2] != b"\xa5\xc3":
        sys.exit(f"{path}: no Gowin preamble at 0x{MAGIC_OFF:02x} "
                 f"({packed[MAGIC_OFF:MAGIC_OFF + 2].hex()}, want a5c3)")
    idcode = int.from_bytes(packed[IDCODE_OFF:IDCODE_OFF + 4], "big")
    if idcode != IDCODE:
        sys.exit(f"{path}: IDCODE 0x{idcode:08x}, want 0x{IDCODE:08x}")
    return idcode


def main():
    if len(sys.argv) != 4 or sys.argv[1] != "--fs2bin":
        sys.exit(__doc__)
    src, out = sys.argv[2], sys.argv[3]
    header, packed = fs_load(src)
    idcode = check(src, header, packed)
    with open(out, "wb") as f:
        f.write(packed)
    pages = -(-len(packed) // 256)
    blocks = -(-len(packed) // 65536)
    print(f"{out}: {len(packed)} bytes, {header.get('Device', '?')} "
          f"IDCODE 0x{idcode:08x}")
    print(f"  an install is {blocks} block erases and {pages} page writes")


if __name__ == "__main__":
    main()
