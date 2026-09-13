#!/usr/bin/env python3
"""Decode a BL616 efuse dump: is this chip fused for encrypted firmware?

Reads what `BLFlashCommand --efuse --read` saved and prints efuse word 0
(EF_CFG_0) field by field.  The bit positions are the SDK's own,
tools/bouffalo_sdk/drivers/soc/bl616/std/include/hardware/ef_data_reg.h:

    ef_sf_aes_mode   [1:0]   0 none, 1 AES128, 2 AES256, 3 AES192
    ef_sboot_en      [5:4]   secure boot (signature) enable
    ef_dbg_jtag_0_dis [27:26]
    ef_dbg_mode      [31:28]

What it means for a Tang Nano 20K (.claude/docs/onboard.md): with
ef_sf_aes_mode != 0 the boot ROM runs only ENCRYPTED images at flash
address 0 - Sipeed's FPGA Partner and friend_20k_encrypted - and with 0
only PLAIN ones - friend_20k, or a firmware built here.  That is the
"fused / not fused" distinction upstream says cannot be told beforehand;
it can, this is it.  The dump may be raw bytes or hex text.
"""

import re
import sys


def load(path):
    raw = open(path, "rb").read()
    txt = raw.decode("ascii", "replace")
    if re.fullmatch(r"[0-9a-fA-F\s,x]+", txt) and len(txt.strip()) >= 8:
        hexs = re.sub(r"0x|[\s,]", "", txt)
        return bytes.fromhex(hexs)
    return raw


def main():
    if len(sys.argv) != 2:
        print("usage: efuse_bl616.py <efuse dump>", file=sys.stderr)
        return 2
    d = load(sys.argv[1])
    if len(d) < 4:
        print(f"{sys.argv[1]}: {len(d)} bytes, not an efuse dump", file=sys.stderr)
        return 1
    w0 = int.from_bytes(d[0:4], "little")
    aes = (w0 >> 0) & 3
    sboot = (w0 >> 4) & 3
    jtag_dis = (w0 >> 26) & 3
    dbg_mode = (w0 >> 28) & 15
    aes_name = {0: "none", 1: "AES128", 2: "AES256", 3: "AES192"}[aes]

    print(f"efuse dump: {len(d)} bytes; EF_CFG_0 = 0x{w0:08x}")
    print(f"  ef_sf_aes_mode    = {aes}  ({aes_name})")
    print(f"  ef_sboot_en       = {sboot}")
    print(f"  ef_dbg_jtag_0_dis = {jtag_dis}")
    print(f"  ef_dbg_mode       = {dbg_mode}")
    print()
    if aes:
        print("FUSED: flash encryption is on.  Only ENCRYPTED images run at")
        print("address 0: the FPGA Partner (flash-mcu-onboard-ftdi) will run,")
        print("friend_20k_encrypted (flash-mcu-onboard-orig-encrypted) restores")
        print("the factory state, and a firmware built here can only ever be a")
        print("second stage at 0x40000 behind the Partner.")
    else:
        print("NOT FUSED: no flash encryption.  Only PLAIN images run at")
        print("address 0: friend_20k (flash-mcu-onboard-orig) is the factory")
        print("state, a firmware built here can be the primary, and the")
        print("encrypted FPGA Partner will NOT run - the chip would stay in")
        print("'Bouffalo CDC DEMO' until a plain image is flashed back.")
        print("The programmer and a custom firmware are then mutually exclusive.")
    if all(b == 0xFF for b in d[:4]) or all(b == 0 for b in d[:64]):
        print()
        print("WARNING: word 0 looks blank - is this really the efuse, and did")
        print("the read succeed?  Check the tool's log before trusting this.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
