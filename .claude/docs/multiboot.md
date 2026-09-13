# MultiBoot: the first design, and why it is not used

> **SUPERSEDED, 13 September 2026.**  Everything below is correct about
> what Gowin documents and about what this repository built - and it does
> not work on this board, because **RECONFIG_N cannot be pulsed from
> inside this FPGA**.  The pulse is generated and no configuration is
> attempted; pin 9 reaches nothing but test pad TP1, so nothing external
> holds it up, which leaves the pad losing its path to the configuration
> controller when it is reused as a GPIO.  `progress.md` has the evidence
> and `coreswitch.md` has the design that replaced this one: one
> bitstream at flash address 0, the three on the card, written from the
> OSD and power-cycled into.
>
> Kept because the account is worth having, and because **one wire from
> header pin 48 to TP1 would make all of it work** - the cores still
> carry CMD 9 and `reconfig_n`.

## The facts (UG290 2.7.6, §7.5.4, §4.2, §7.5.3)

- The GW2AR-18 configures itself from the SPI flash on its MSPI pins at
  power-up, from address 0, always.
- Every bitstream's header carries a **SPI Flash Address** - the address
  of the next bitstream.  Set with the IDE's Bitstream option or gw_sh's
  `set_option -multi_boot 1 -multiboot_spi_flash_address <hex>`; only
  bits [23:12] are honoured, so 4 KB aligned.  Default 0.
- A low pulse of 25 ns or more on **RECONFIG_N** makes the FPGA reload
  from that address.  "There is no limit placed on the number of
  RECONFIG_N events."  The address register itself is reset only by a
  power cycle; on each reload the FPGA takes the address from the header
  of the image it has just loaded.
- RECONFIG_N (pin 9 on the QFN88, bank 3) can be a GPIO, output only
  (`-use_reconfign_as_gpio 1`), and "you can also write logic to control
  the pin to trigger the device to reconfigure as required".  The pin
  must read high from configuration on - an initial value of 1.
- If a load fails (bad CRC, wrong ID), the GW2AR-18 makes one more
  attempt, from the failed image's jump address if it could read it.
  With a slot erased the FPGA "continues reading flash addresses until
  the golden image is reached".  In the worst case it sits unconfigured
  until a power cycle, which loads slot 0 again.
- The Tang Nano 20K's flash is 64 Mbit = 8 MB (Sipeed wiki).  A core
  here is 907 418 bytes uncompressed.

## The layout

```
0x000000  slot 0  UKNC    (../tang-uknc,   test003.gprj)  header -> 0x100000
0x100000  slot 1  PK8000  (../tang-pk8000, pk8000.gprj)   header -> 0x200000
0x200000  slot 2  Korvet  (../tang-korvet, korvet.gprj)   header -> 0x000000
0x300000  ...     free (five more slots)
```

`Makefile`: `CORES`, `ADDR_*`, `NEXT_*`, `SLOT_SIZE`.  `mnano/ultima.c`:
`ultima_cores[]` in the same order.  `bin/ultima.bin` is the three at
their offsets with 0xFF between, 3 004 570 bytes.

## What the core does

- `mister/sysctrl.v` (each sibling): **CMD 9**, and the byte after it
  must be **A5h**; then `reconfig` pulses for one clock.  The magic byte
  is there so that no stray byte on the link reloads the FPGA.
- `top.v` (each sibling): `reconfig_cnt`, an 8-bit counter initialised
  to 0, loaded with 255 by the pulse, counting down; `reconfig_n = (cnt
  == 0)`.  So the pin is high from configuration and low for 256 clocks
  once - 6 µs on the Korvet's 40.5 MHz, 10 µs on the UKNC's 25 MHz -
  against the 25 ns asked for.  It is released after that on purpose:
  should the FPGA for any reason not reconfigure, the pin does not stay
  low (RECONFIG_N low blocks configuration).
- `.cst`: `IO_LOC "reconfig_n" 9; IO_PORT ... LVCMOS33 PULL_MODE=UP`.
- `tang/impl/<name>_process_config.json`: `"RECONFIG_N": true`, which
  `tools/gowin_tcl.py` turns into `set_option -use_reconfign_as_gpio 1`
  (it already read the other dual-purpose pins from there).
- `tools/gowin_tcl.py --multiboot-addr ADDR` adds `set_option -multi_boot
  1` and `-multiboot_spi_flash_address <8 bare hex digits>`; `--abs`
  makes the source paths absolute so the Tcl runs from `build/cores/`.

The `.fs` header then reads `//MultiBootSPIAddr: 0x00100000`.  **That
line is text the tool echoes** - given `0x100000` it printed
`0x0x00100000`.  The value the FPGA reads is the operand of the `D2`
command in the bitstream's preamble: bytes `d2 00 ff ff` at 0x34, then
the 32-bit address big-endian at 0x38.  Found by building the PK8000
core with two addresses and diffing the packed bitstreams: exactly one
byte differed, 0x39, 0x00 against 0x20.  `tools/mkimage.py` checks that
field against the ring, and refuses to pack otherwise.

The packed `.fs` (the bit lines, `//` lines dropped, MSB first) is
byte-for-byte Gowin's own `.bin` - checked on UKNC Nano's committed
`test003.fs`/`test003.bin` - so `bin/ultima.bin` is what three
`openFPGALoader -f -o <slot> core.fs` runs would leave in the flash, in
one run.

## What the firmware does (`mnano/ultima.c`)

The firmware cannot pick a slot.  It can say "next" (CMD 9) and see who
answers (CMD 0's core id: 5 UKNC, 7 PK8000, 8 Korvet).  So:

- `/sd/ultima.ini` holds the wish, `core=uknc|pk8000|korvet`.
- **At start** (`ultima_boot`, from the OSD task once the card is
  readable, before `menu_init`): if the wish differs from the running
  core, `sys_irq_hold` goes up, and the ring is walked - CMD 9, 300 ms of
  believing nothing, then up to ten seconds for a core to answer CMD 0,
  `R=3` to hold it, compare - at most three times.  The same core
  answering twice in a row means the reconfiguration did not happen (a
  bitstream without CMD 9, or a lone image pointing at itself) and the
  walk stops.  Then the new core's coldboot notice is acknowledged
  (`sys_irq_ctrl(spi, 1)`), the card is waited for and remounted
  (`sdc_reattach`), the hold is lifted, and the core's directory is
  created if missing.  `menu_init` then sets the machine up as its own
  firmware would.
- **From the OSD** (`ultima_switch`, the Core form's 'C' entry): the wish
  is written, every image is closed (a floating MISO must never be read
  as a write request against an open image), the hold goes up, `R=3`,
  CMD 9, 100 ms, and the MCU resets itself through the watchdog
  (`sys_reset_mcu`, the same reset MiSTeryNano applies to a coldboot
  notice).  The MCU comes back up while the FPGA is still loading -
  `main()` waits ten seconds for it now, not five - and `ultima_boot`
  finishes the walk.  One code path for the power-up and the switch.
- While `sys_irq_hold` is up, `spi.c`'s task does not read the interrupt
  status (it looks again 50 ms later), and `sys_handle_event` does not
  reset the MCU on a coldboot notice.

## Timing

Gowin's default `LoadingRate` is 2.5 MHz and the header says so: 7.26
Mbit at 2.5 MHz is about 2.9 s a load, so a switch is 3-6 s of black
screen plus the MCU's restart and the USB keyboard's enumeration.
`make cores LOADING_RATE=<MHz>` passes `-loading_rate`; not tried on a
board, and a rate the flash or the traces cannot take is a board that
does not configure - which a power cycle does not cure, since it loads
at the same rate.  Leave it until the default has been seen working.

## What is not known

Whether this board's RECONFIG_N reloads when driven from inside.  The
guide says it does and the pin is placed there (`reconfig_n - 9/3 Y
out` in each core's pin report), but the on-board BL616 is also on that
net on some board revisions and its idle state is not documented.
`progress.md` is where the first flash's result goes.
