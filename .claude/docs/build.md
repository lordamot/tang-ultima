# Building and flashing

What ships prebuilt, so a user needs no toolchain:

```
bin/uknc.fs bin/pk8000.fs bin/korvet.fs   the three cores, one a slot
bin/ultima.bin                            the three at their slots, one flash image
bin/bl616.bin                             the firmware
```

Everything builds here; `tools/` holds the toolchain (`make toolchain`,
~8 GB; on this host hard-linked from `../tang-korvet/tools/`, same
inodes) and the three sibling repositories must sit beside this one.

```
make cores         the three bitstreams -> bin/, then bin/ultima.bin
make core-<c>      one of uknc, pk8000, korvet
make image         bin/ultima.bin from bin/*.fs (mkimage.py checks the ring)
make fw            the firmware -> build/fw/bl616.bin  (copy to bin/ by hand)
make menu-test     the OSD on the host, every form of every core -> build/menu/*.png
make lint          each sibling's Verilator lint
make flash-image   openFPGALoader bin/ultima.bin -> the flash, one run
make flash-core-<c>  one slot (-o <addr> core.fs); three of these are three replugs
make flash-mcu     the BL616 over UART (COMX=/dev/ttyACM0)
```

## A core

`make core-korvet` runs `../tang-korvet/tools/gowin_tcl.py --abs
--multiboot-addr 0x000000` into `build/cores/korvet/build.tcl`, then
gw_sh there.  gw_sh writes `impl/` under its cwd, so the PnR output is
`build/cores/korvet/impl/pnr/korvet.fs` and the sibling's tree is not
touched.  `../tang-korvet/tools/timing_check.py build/cores/korvet/impl/pnr`
gates it - the sibling's own rules, its own clocks - and the `.fs` is
copied to `bin/korvet.fs`.  About 35 s (PK8000) to 60 s (UKNC, Korvet).

The build reads the sibling's `.gprj` and its
`tang/impl/<name>_process_config.json` for the dual-purpose pins; both
carry `RECONFIG_N: true` since Sep 2026.  A sibling built in its own
tree (`make bitstream` there) has the same reconfig support with the
address left at 0 - it reloads itself on CMD 9.

The gw_sh quirks (its libraries against a current Linux, its option
names, `-use_sspi_as_gpio`) are the siblings' business and their
`build.md` has them; the Makefile here sets the same three environment
variables.

## The image

`tools/mkimage.py bin/ultima.bin 0x100000 ADDR:NEXT:core.fs ...` packs
each `.fs` (bit lines, MSB first - byte-identical to Gowin's `.bin`),
checks that each header's MultiBoot address is the next slot both in the
`//` comment and in the `D2` command's operand at 0x38, that each fits
its slot, and lays them out with 0xFF between.  Change the layout in the
Makefile and this refuses until the cores are rebuilt to match.

## Flashing the FPGA

Replug the USB cable, flash, power-cycle - in that order.  UKNC Nano
learned it: openFPGALoader `-f` writes the flash and reports success but
does not reliably reconfigure the chip, and once anything has opened the
board's `/dev/ttyUSB*` or a previous openFPGALoader run has reset the
bridge, the next run dies with `ftdi_usb_reset failed` until the cable is
replugged.  That is why the three slots go in one run:

```
make flash-image     openFPGALoader -b tangnano20k -f --file-type bin -o 0 bin/ultima.bin
```

`--file-type bin` makes openFPGALoader (v1.1.1 in tools/) take the file
raw rather than parse it as a bitstream.  The write covers 3 MB and
erases only the sectors it writes.  **Not yet run on a board**; if it
refuses the raw file, the fallback is three `make flash-core-<c>` runs
with a replug before each.

A single core can also be flashed the sibling's way (`make
flash-fpga-flash` there, or `-o 0 bin/uknc.fs` here): slot 0 alone, its
header pointing at an empty slot 1.  A switch from it will then fail to
load - the FPGA tries slot 1, finds nothing, and sits until a power
cycle - which is the documented shape of the failure, not a hang of the
firmware: `main()` reports "FPGA not ready after 10 seconds".

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
Tang Ultima: running UKNC (05), card asks for 08
core 05 running, Korvet (08) wanted: reconfiguring
SYS reconfigure
Core ID: 07
core 07 running, Korvet (08) wanted: reconfiguring
SYS reconfigure
Core ID: 08
created /sd/korvet
```

"the core did not change - no MultiBoot image to go to" is a bitstream
without CMD 9 or a lone image; "no core answered after the
reconfiguration" is an empty or bad slot (power-cycle: slot 0 loads).
