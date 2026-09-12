#!/usr/bin/env python3
"""Pack the cores' bitstreams into one SPI flash image, checking the ring.

Each core is a Gowin .fs (ASCII bits, a `//` header) built by ../tang-<core>
with `--multiboot-addr` naming the NEXT slot's address; the FPGA starts at
0 and moves to that address whenever RECONFIG_N is pulsed (UG290 7.5.4).
This script packs each .fs to its binary form - the same bytes Gowin's
own .bin holds and openFPGALoader writes for a .fs - places it at its slot
and writes the whole thing as one raw file for a single flash operation.
Before that it refuses anything that would leave the board without a
way back:

  * an image larger than its slot;
  * a header whose //MultiBootSPIAddr is not the next slot in the ring
    (a wrong link makes the switch land on the wrong core, or on nothing);
  * a header for a different device.

  mkimage.py OUT.bin SLOT_SIZE  ADDR:NEXT:core.fs  ADDR:NEXT:core.fs ...

Also `mkimage.py --fs2bin IN.fs OUT.bin` packs one bitstream alone.
"""
import re
import sys


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


def spi_addr_field(packed):
    """The 32-bit operand of the D2 (SPI flash address) preamble command,
    or None.  The preamble is short; look only there."""
    i = packed.find(b"\xd2\x00\xff\xff", 0, 256)
    if i < 0:
        return None
    return int.from_bytes(packed[i + 4:i + 8], "big")


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--fs2bin":
        _, packed = fs_load(sys.argv[2])
        open(sys.argv[3], "wb").write(packed)
        print(f"{sys.argv[3]}: {len(packed)} bytes")
        return

    if len(sys.argv) < 4:
        sys.exit(__doc__)
    out, slot_size = sys.argv[1], int(sys.argv[2], 0)
    image = bytearray()
    device = None
    for spec in sys.argv[3:]:
        addr, nxt, path = spec.split(":", 2)
        addr, nxt = int(addr, 0), int(nxt, 0)
        header, packed = fs_load(path)
        dev = header.get("Device", "?")
        if device is None:
            device = dev
        elif dev != device:
            sys.exit(f"{path}: device {dev}, the others are {device}")
        got = int(header.get("MultiBootSPIAddr", "0x0"), 16)
        if got != nxt:
            sys.exit(f"{path}: header names next image 0x{got:06x}, "
                     f"the ring wants 0x{nxt:06x} - rebuild with --multiboot-addr")
        # The header line is only text; the address the FPGA reads is the
        # operand of the D2 command in the bitstream's preamble (seen by
        # building the same core with two addresses: one 32-bit field,
        # big-endian, at 0x38 on the GW2AR-18).  Check that too.
        enc = spi_addr_field(packed)
        if enc is None:
            sys.exit(f"{path}: no MultiBoot address command (D2) in the preamble")
        if enc != nxt:
            sys.exit(f"{path}: bitstream encodes next image 0x{enc:06x}, "
                     f"the header says 0x{got:06x}")
        if len(packed) > slot_size:
            sys.exit(f"{path}: {len(packed)} bytes do not fit a "
                     f"0x{slot_size:x}-byte slot")
        if addr % 0x1000:
            sys.exit(f"{path}: slot 0x{addr:x} is not 4 KB aligned")
        if len(image) > addr:
            sys.exit(f"{path}: slot 0x{addr:x} overlaps the previous image")
        image += b"\xff" * (addr - len(image))
        image += packed
        print(f"  0x{addr:06x}  {len(packed):8d} bytes  -> next 0x{nxt:06x}  {path}")
    open(out, "wb").write(image)
    print(f"{out}: {len(image)} bytes, {device}")


if __name__ == "__main__":
    main()
