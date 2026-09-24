# Building and flashing

What ships prebuilt, so a user needs no toolchain:

```
bin/<core>.fs   uknc pk8000 korvet zs256 bk  the five cores, Gowin's ASCII form
bin/<core>.bin                               the same packed - what the flash
                                             holds and what goes on the card
bin/bl616.bin                                the firmware
```

There are no slots: the flash holds one bitstream, at address 0, and the
card holds all five.  `.claude/docs/coreswitch.md` says why.

Everything builds here; `tools/` holds the toolchain (`make toolchain`,
~8 GB; on this host hard-linked from `../tang-korvet/tools/`, same
inodes) and the five sibling repositories must sit beside this one.

```
make cores         all five -> bin/<c>.fs and bin/<c>.bin
make core-<c>      one of uknc, pk8000, korvet, zs256, bk
make card          say which files to copy onto the SD card
make fw            the firmware -> build/fw/bl616.bin  (copy to bin/ by hand)
make menu-test     the OSD on the host, every form of every core -> build/menu/*.png
make lint          each sibling's Verilator lint
make flash-image   openFPGALoader bin/<DEFAULT_CORE>.bin -> flash address 0
make flash-core-<c>  any single core -> address 0 (the same write, another file)
make flash-mcu     the BL616 over UART (COMX=/dev/ttyACM0)
```

The board's own BL616 - the USB programmer, a different chip - has its
own set: `onboard-status`, `onboard-fetch`, `onboard-backup`,
`onboard-efuse`, `flash-mcu-onboard-{orig,orig-encrypted,ftdi,stage2,restore}`.
`.claude/docs/onboard.md` before any of them.

## A core

`make core-korvet` runs `../tang-korvet/tools/gowin_tcl.py --abs` into
`build/cores/korvet/build.tcl`, then
gw_sh there.  gw_sh writes `impl/` under its cwd, so the PnR output is
`build/cores/korvet/impl/pnr/korvet.fs` and the sibling's tree is not
touched.  `../tang-korvet/tools/timing_check.py build/cores/korvet/impl/pnr`
gates it - the sibling's own rules, its own clocks - and the `.fs` is
copied to `bin/korvet.fs`.  About 35 s (PK8000) to 60 s (UKNC, Korvet);
the ZS-256 about 70 s (14 Sep 2026: logic 46%, registers 21%, timing
clean).

The build reads the sibling's `.gprj` for the file list and its
`tang/impl/<name>_process_config.json` for the dual-purpose pins.  Both
carry `"MSPI" : true` since Sep 2026, which is what puts the flash on
`flashwr.v`'s MSPI pins after configuration, and `"RECONFIG_N" : false` -
pin 9 is left a configuration input, and `reconfig_n` sits on pin 48
instead, dormant unless a wire is run from there to TP1.  A sibling built
in its own tree has all of it.

The gw_sh quirks (its libraries against a current Linux, its option
names, `-use_sspi_as_gpio`) are the siblings' business and their
`build.md` has them; the Makefile here sets the same three environment
variables.

## The packed bitstream

`tools/mkimage.py --fs2bin bin/korvet.fs bin/korvet.bin` turns the `.fs`'s
bit lines into bytes, MSB first - byte-identical to Gowin's own `.bin`,
checked against `impl/pnr/*.bin` and against UKNC Nano's committed pair.

It refuses anything that is not a bitstream for this device: the `//`
header's `Device`, the Gowin preamble `a5 c3` at 0x16, IDCODE
`0x0000081b` at 0x1c, and a size between 64 KB and 1 MB.  Those are the
same checks `mnano/flashwr.c` makes before it erases flash address 0, and
they are in both places on purpose - a file this script accepts is a file
the firmware will accept.

## Flashing the FPGA

Replug the USB cable, flash, power-cycle - in that order.  UKNC Nano
learned it: openFPGALoader `-f` writes the flash and reports success but
does not reliably reconfigure the chip, and once anything has opened the
board's `/dev/ttyUSB*` or a previous openFPGALoader run has reset the
bridge, the next run dies with `ftdi_usb_reset failed` until the cable is
replugged.

```
make flash-image     openFPGALoader -b tangnano20k -f --file-type bin -o 0 bin/uknc.bin
```

`--file-type bin` makes openFPGALoader (v1.1.1 in `tools/`) take the file
raw rather than parse it as a bitstream.  **Verified on the board**, 13
September 2026: written and then read back with `--dump-flash
--file-size` and compared byte for byte.

This is needed once.  After it, the OSD installs cores from the card
itself and the host tool is only for recovery - which is what it is for if
an install is interrupted between the erase and the end of the write.

Reading the flash back is the same tool and is worth knowing:

```
openFPGALoader -b tangnano20k --dump-flash --file-size 907418 -o 0 flash.bin
cmp flash.bin bin/uknc.bin
```

It loads a pass-through bitstream into the FPGA's SRAM to do it, so the
board needs a power cycle afterwards.

## Flashing the BL616

Hold BOOT, tap RESET, release BOOT; the chip enumerates as a serial port;
`make flash-mcu COMX=/dev/ttyACM0` (needs `dialout`; `sg dialout -c '...'`
works without a relogin).  `BFLB IMG LOAD HANDSHAKE FAIL` means the port
opened and nothing answered.  Press RST afterwards.  The flasher is a
self-contained bundle inside the SDK; the Makefile writes it an `.ini`
with an absolute path.

## The console

The M0S Dock's UART (io21/22, 2 Mbit/s) prints what `ultima.c` does:

```
Tang Ultima: running UKNC (05)
flash: JEDEC ID ef4017 (W25Q64, 8 MB), status 00
flash: address 0 holds a GW2AR-18C bitstream
Tang Ultima: installing Korvet from /sd/cores/korvet.bin
flashwr: JEDEC ID ef4017
flashwr: /sd/cores/korvet.bin, 907418 bytes
flashwr: erasing 14 blocks
flashwr: writing 3545 pages
flashwr: verifying
flashwr: 907418 bytes installed at address 0 and verified
Tang Ultima: Korvet is in the flash - power-cycle the board
```

The two `flash:` lines come from `flash_probe()` at every boot and are
read-only; they are the cheapest possible check that the switch can work.
The refusals all name themselves: "refusing - no Winbond flash answered"
is the MSPI pins not reaching the chip, "no Gowin preamble" or "IDCODE
..., want 0000081b" is the wrong file on the card, and "mismatch at
<addr>" is a verify failure - after which address 0 is **not** a
bitstream, so retry rather than power off.
