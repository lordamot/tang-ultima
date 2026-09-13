# Progress

## State, 12 September 2026 (before the board)

Version 0.1.0 alpha.  Built in one session from the three siblings.

What has been verified, and how:

- **The three cores build out of their trees with the MultiBoot address
  and RECONFIG_N as a GPIO**, each passing its sibling's timing gate
  (0 setup, 0 hold violations on all three), `reconfig_n` placed on pin
  9 bank 3 as an output in each pin report.  Resources unchanged within
  noise (UKNC 52%, PK8000 33%, Korvet 44% logic).
- **The MultiBoot address is in the bitstream**, not only in the header
  comment: the PK8000 core built with 0 and with 0x200000 differs in one
  byte, 0x39, the operand of the `D2` preamble command.  `mkimage.py`
  checks that field.  Gowin's option takes bare hex digits; `0x...` is
  echoed as `0x0x...` in the header but parsed right.
- **The packed `.fs` is Gowin's `.bin`**, byte for byte (UKNC Nano's
  committed pair), so `bin/ultima.bin` is what three `-o` writes would
  leave.
- **The firmware builds** (457 952 bytes) and **`make menu-test` walks
  all three cores' forms with 0 errors**, 28 screens, the Core form on
  each with the running core marked and the switch recorded.
- **Each sibling lints** with the reconfig support in.

## The first flash, 13 September 2026

`make flash-image` wrote the image and the board powers up as the UKNC.
**The core switch does not work.**  What was seen:

- F12, Core, Korvet: the OSD draws "Switching to Korvet ...", the machine
  restarts, and it is the UKNC again.  The Core form then marks UKNC as
  the running core - which is what it must do, since the mark comes from
  CMD 0 and the UKNC is what answers; it says nothing about the card.
- **Landing on the UKNC rules the card out as the cause.**  One hop from
  the UKNC is the PK8000, not the Korvet: `ultima_switch` sends CMD 9
  once, before the MCU resets and before any `.ini` is read, and
  `ultima_boot` would only finish the walk afterwards.  A lost
  `/sd/ultima.ini` would therefore leave the board on the PK8000.  It is
  on the UKNC, so the *first* hop had no effect at all.
- The flash holds the right image.  Read back over JTAG
  (`openFPGALoader -b tangnano20k --dump-flash --file-size 3004570 -o 0`)
  and compared with `bin/ultima.bin`: **byte-identical**, all three
  slots, so the three jump addresses the FPGA reads are the ring's.

So slot 0 is right, its header's jump address is right, and CMD 9 is
reached.  Two possibilities remain, and they look the same from outside:

1. **RECONFIG_N driven low from user logic does not reload this FPGA** -
   the on-board BL616 sitting on pin 9, or the pad losing its path to
   the configuration controller when reused as a GPIO.
2. **It reloads, but from address 0** - the jump address not applied on a
   RECONFIG_N pulse on this device.

The discriminator was whether the SPI link dies for about three seconds
after CMD 9 (a reload at 2.5 MHz) or answers at once.  Settled below by
an LED instead of a console: **it is 1.**

### The LED witness, 13 September 2026 - it is case A

To tell the two apart without a console or a scope, `../tang-uknc`'s
`top.v` got a temporary latch: `recfg_seen` set by `sys_reconfig`, LED 2
blinking from it at ~3 Hz.  The register lives exactly as long as the
configuration does, so a reload would wipe it.

**The LED blinks and keeps blinking.**  So:

- CMD 9 reaches `sysctrl.v`, the A5h magic matches, `reconfig` pulses and
  `reconfig_cnt` is loaded - the core's side of the switch works.
- **The FPGA does not reload.**  Case B is out: the jump address is not
  the problem, because no configuration is attempted at all.

What that leaves, and neither has been measured:

- **A1** - pin 9 never reaches a low level, something on the board
  holding it high.  Pin 9 sits in the corner with TMS 5, TCK 6, TDI 7,
  TDO 8 and the 27 MHz oscillator on 4 - board infrastructure, not the
  user headers - so the on-board BL616 on that net is the likely holder.
- **A2** - the pin does go low and the configuration controller ignores
  it, its input path cut by `-use_reconfign_as_gpio`.  UG290 4.2 says
  otherwise ("you can also write logic to control the pin to trigger the
  device to reconfigure"), and gw_sh has an error for a configuration
  that cannot do it ("configuration that does not support RECONFIG_N")
  which it did not raise.

Telling A1 from A2 needed pin 9 to be reachable.  It is - TP1 - and the
schematic settles it without a measurement; see below.

### Pin 9 IS reachable, and nothing else is on its net

From the Tang Nano 20K schematic v1.3 (the Sipeed wiki links it; a copy
is mirrored at iot-kmutnb.github.io).  Pin 9's net is `PIN09_SYS_~{RECFG}`
and it appears **exactly twice** in the whole schematic: at the FPGA's
`IOR31B/RECONFIG_N` pin, and at **TP1**.  The six test points - the
"reserved jtag test point" group the wiki mentions - are:

```
TP1  PIN09_SYS_~{RECFG}   RECONFIG_N      TP4  PIN08_SYS_TDO
TP2  GND                                  TP5  PIN07_SYS_TDI
TP3  PIN06_SYS_TCK                        TP6  PIN05_SYS_TMS
```

So **the on-board BL616 is NOT on RECONFIG_N** - its nets are
JTAG_TMS/TCK/TDI/TDO, BL616_UART_RX/TX and the SPI group, none of them
RECFG.  There is no external pull-up either.  The net is the FPGA pad, a
bare test pad, and the pad's own ~100 uA internal pull-up.

**That rules out A1 by inspection.**  An 8 mA LVCMOS33 push-pull output
against 100 uA of pull-up and nothing else cannot fail to reach 0 V.  The
pin goes low, and the configuration controller ignores it: **A2**.
`-use_reconfign_as_gpio` cuts the pad from the controller, whatever
UG290 4.2 says about writing logic to control the pin.

### The fix that follows: drive TP1 from another pin

Leave pin 9 a **real RECONFIG_N input** - drop `-use_reconfign_as_gpio`,
`RECONFIG_N: false` in the process config, no `IO_LOC` for it - and move
`reconfig_n` to a free header pin, **open-drain** so it only ever pulls
low and the pad's internal pull-up provides the high.  One wire from that
header pin to TP1.  The controller then sees the documented external low
pulse, and nothing else about the design changes: the three slots, the
ring, `mkimage.py` and `ultima.c`'s walk all stay.

Pins free in all three cores and carrying no configuration function:

```
bank 5   25 26 27 28 29 30 31 32     (LCD_HS, LCD_VS, LCD_B7..B3, LCD_G7)
bank 3   48 49                       (LCD_DE, LCD_BL)
```

The `LCD_*` nets do reach the pin headers: this board's M0S Dock is wired
to pins 41 and 42, whose schematic nets are `PIN41_LCD_R4` and
`PIN42_LCD_R3`.  Pin 48 is the first choice - bank 3 is already active
(87/88 are the buttons), and unlike 49 it drives no backlight circuit.

Not tried on the board.  If the switch then works, A2 is confirmed and no
measurement is needed; if it does not, the multimeter reading on TP1 is
the next step.

### Two MultiBoot options we had not set - checked, not the fault

`strings libGWTE.so` also carries `-multiboot_mode` and
`-multiboot_address_width`, neither of which `gowin_tcl.py` sets.  They
are not a trigger switch: the accepted values sit beside
`single|fast|dual|quad` (the SPI read mode for the jump) and `24|32`
(the address width).  The defaults are right for a 24-bit address in a
64 Mbit flash, so nothing to change.

What was audited on the host after the flash, and is sound:

- `set_option -multi_boot 1`, `-multiboot_spi_flash_address`,
  `-use_reconfign_as_gpio 1` all present in the generated `build.tcl`.
- The jump address in the binary at 0x38 - `0x00100000`, `0x00200000`,
  `0x00000000` - and **Gowin's own `impl/pnr/*.bin` is byte-identical to
  the packed one**, so the field is the tool's, not `mkimage.py`'s.
- A stock build and a MultiBoot build of the UKNC differ in exactly one
  byte of the first kilobyte, 0x39.  Nothing else was disturbed.
- `reconfig_n` at `9/3 out RECONFIG_N LVCMOS33 UP` in the pin report, no
  gw_sh warning.
- CMD 9 + A5h -> `reconfig` -> `reconfig_cnt` -> `reconfig_n`, and the
  `.cst` line, identical in all three siblings; `system_reset` reaches
  only `pp_rst` and cannot hold the counter.
- `menu.c`'s `'C'` entry -> `ultima_switch` -> `sys_reconfig` -> CMD 9.
  CMD 4 from the same state machine works on the board (the OSD's
  settings do), so the protocol and the decode are not in question.

## What was NOT verified - the rest of the list

On the board, still unverified:

1. That RECONFIG_N driven low from user logic reloads this board's FPGA
   from the header's address.  UG290 says so; the on-board BL616 is on
   the same net on some revisions.
2. ~~`openFPGALoader --file-type bin -o 0`~~ - **verified 13 Sep 2026**:
   the 3 MB raw image is written correctly, read back and compared
   byte for byte.  No fallback to three `flash-core-*` runs needed.
3. The timing of a switch: ~3 s a load at the default 2.5 MHz rate, plus
   the MCU's watchdog restart and USB enumeration.
4. That the card is readable again after a reload without any further
   care (`sdc_reattach` waits for sd_card.v's status and remounts).
5. Every core's own behaviour under this firmware rather than its own:
   the merge is by hand.  The host test says the forms and letters
   agree; it cannot say the keyboards do (the keymaps are copied
   verbatim).

### Second flash, 13 September 2026 - reconfig_n on pin 48, open drain

Built and flashed, **not yet tried on the board** (it needs the wire).

- All three siblings: `IO_LOC "reconfig_n" 48` with `OPEN_DRAIN=ON`,
  `"RECONFIG_N" : false` in each process config, so `gowin_tcl.py` emits
  no `-use_reconfign_as_gpio` (checked in the generated Tcl).  `top.v`
  and the `.cst` comments say why.  The blink diagnostic is reverted.
- All three lint clean; all three pass their timing gate with 0 setup and
  0 hold violations; the ring closes; `bin/ultima.bin` repacked.
- Each pin report: `reconfig_n  48/3  IOR49[B]  out  LVCMOS33  drive 8
  pull UP  Open Drain ON`, and `9/3  -  in  IOR31[B]  RECONFIG_N` - pin 9
  carries no user signal and is a configuration input again.
- Flashed with `make flash-image` and read back: byte-identical to
  `bin/ultima.bin`.

**What the board needs:** one wire, header **pin 48** to **TP1**.  Nothing
else is on either point, and the output is open drain, so it can only ever
pull TP1 down - a wire to the wrong pad cannot damage anything.

Whether pin 48 is on the pin headers is an inference, not a reading: the
`LCD_*` nets do reach them, since this board's M0S Dock is on pins 41 and
42 (`PIN41_LCD_R4`, `PIN42_LCD_R3`), and 48 is `PIN48_LCD_DE`.  If the
silkscreen does not show 48, any of 25-32 or 49 will do - one line in each
`.cst` and a rebuild.

## The redesign, 13 September 2026 - the flash writer

MultiBoot cannot be triggered on this board, so the switch no longer tries
to. **Power-up always loads flash address 0**; the flash now holds one
bitstream, that address, and the three machines live on the SD card as
packed bitstreams.  The OSD writes the wanted one into address 0 through
the FPGA's own MSPI pins and asks for a power cycle.
`.claude/docs/coreswitch.md` is the design; `multiboot.md` is kept as the
record of the first one.

Chosen over the alternatives on purpose: all three machines in one
bitstream does not fit and is not close - logic 130%, **BSRAM 87 blocks of
46**, and six rPLLs wanted of two, at frequencies that cannot be shared.
Two machines do not fit either (58 BSRAM of 46).

What is built:

- **`mister/flashwr.v`** in all three siblings: a 512-byte buffer and one
  transaction - "shift TX bytes out, read RX bytes back, CS held" - on
  MCLK 59, MCS_N 60, MO 61, MI 62, at MCLK = clk/4.  It is not a flash
  controller; the whole W25Q64 command set is in the firmware.  The buffer
  is there so that nothing has to shift eight bits to the flash inside one
  20 MHz m0s byte time.
- **`"MSPI" : true`** in each process config, so `gowin_tcl.py` emits
  `-use_mspi_as_gpio 1`.  UG290 4.1.2 table 4-2 says those pins are
  configuration pins until DONE and GPIOs after, so a bitstream that takes
  them over still boots from the flash it takes over.  openFPGALoader does
  the same thing from the JTAG side - every `-f` and `--dump-flash` here
  has gone through the FPGA driving these pins.
- **sysctrl CMD 10** in all three, passing the payload through byte for
  byte and tracking flashwr's answers.
- **`mnano/flashwr.c`**: the W25Q64, and `flash_install()` - which refuses
  before it erases (a Winbond JEDEC ID, a file of 64 KB..1 MB, `a5 c3` at
  0x16, IDCODE `0x0000081b` at 0x1c), then erases 14 blocks, writes 3545
  pages, and reads it all back to compare.
- **`mnano/ultima.c`** rewritten: no ring, no walk, no `sys_irq_hold`
  across the switch.  `ultima_switch()` installs and returns; the machine
  changes on the next power-up.  `ultima_boot()` reports and warns.
- **`mnano/menu.c`**: the Core form draws Checking/Erasing/Writing/
  Verifying with a percentage and "Do not switch off!", and afterwards
  either "power-cycle the board" or the failure with "Do NOT switch off -
  retry".
- **`tools/mkimage.py`** is now `--fs2bin` only, with the same two
  bitstream checks the firmware makes; the ring packer is gone.
- The Makefile: no slots, no `--multiboot-addr`; `make cores` also packs
  `bin/<core>.bin`, `make card` says what to copy, `make flash-image`
  writes `bin/$(DEFAULT_CORE).bin` to address 0.  `bin/ultima.bin` is
  gone.

Verified here, and only here:

- **All three lint** (Verilator, in their own trees).
- **All three build and pass their own timing gate**: 0 setup, 0 hold
  violations each.  Cost of the flash writer: UKNC 10788 -> 10928 LUTs,
  PK8000 6640 -> 6872, Korvet 9058 -> 9200, and one BSRAM block each
  (UKNC 30 -> 31 of 46).
- **The MSPI pins place** on 59/60/61/62 with their configuration
  functions in every pin report; pin 9 carries no user signal and is a
  RECONFIG_N input again.
- **The firmware builds**, 461 088 bytes, no warnings in the new files.
- **`make menu-test`**: 28 screens, 0 errors, the Core form on all three.
- **`bin/<core>.bin` is 907 418 bytes**, IDCODE `0x0000081b`, and an
  install is 14 block erases and 3545 page writes.

**NOT verified - nothing has written the flash from user logic yet:**

1. That flashwr.v talks to the W25Q64 at all.  The first thing to look for
   is `flashwr: JEDEC ID ef4017` on the console - that alone proves the
   MSPI pins, the transaction engine and CMD 10 all work, and it reads the
   flash without writing a byte.
2. That an install completes, and how long it takes (the arithmetic says
   ten-odd seconds; nothing has measured it).
3. That a core installed this way boots - i.e. that the bytes written by
   flashwr.v are the bytes openFPGALoader would have written.  Reading the
   flash back over JTAG and comparing with `bin/<core>.bin` settles this
   without a power cycle.
4. That the three machines behave as their own repositories make them with
   the MSPI pins taken over.  Nothing should change - the flash is idle
   after configuration - but it has not been seen.

## The flash writer works, 13 September 2026

**First core installed on a board.**  The order it was done in, because it
matters:

1. `make flash-mcu` - the new firmware, 461 712 bytes, SHA verified on the
   device by the flasher.
2. `make flash-image` - the new UKNC core (the one carrying `flashwr.v`) to
   flash address 0, then read back over JTAG and compared with
   `bin/uknc.bin`: identical.
3. The card, written from this host: `/cores/uknc.bin`, `/cores/pk8000.bin`,
   `/cores/korvet.bin`, each compared with `bin/` after copying, and
   `ultima.ini` corrected to `core=uknc` - it still said `korvet` from the
   MultiBoot attempt, which was never true.
4. Power cycle; the UKNC came up.  **So `-use_mspi_as_gpio` does not stop
   the FPGA booting from the flash it then takes over** - UG290 4.1.2 says
   so and now the board does.
5. F12 -> Core -> Korvet from the OSD.  It reported **"Korvet installed"**.
6. **Before power-cycling**, the flash was read back over JTAG -
   `--dump-flash --file-size 907418 -o 0` - and compared with
   `bin/korvet.bin`: **byte-identical**.

That last step is the one that cannot be taken afterwards, and it is what
makes this a measurement rather than a hope.  The firmware verifies its own
write, so the firmware agreeing with itself proves little; JTAG is a second
witness that never went through `flashwr.v`.

**What is now proven on the board:**

- `flashwr.v`, SYS CMD 10 and `mnano/flashwr.c` work: the MSPI pins reach
  the W25Q64, the transaction engine shifts correctly in both directions,
  and erase, page program and read-back all do what they should.
- 907 418 bytes of a bitstream can be moved card -> FPGA -> MCU -> FPGA ->
  flash with not one byte wrong.
- A bitstream built with `-use_mspi_as_gpio` still configures the FPGA from
  that same flash.
- The OSD's Core form drives all of it and reports the result.

7. **And it booted it** - without a power cycle, which is itself a finding.
   openFPGALoader resets the FPGA at the end of a flash operation (hence
   its `--skip-reset`), and for Gowin that reset is a JTAG RELOAD, which
   makes the device reconfigure **from flash address 0**.  By then that was
   the Korvet, so the readback in step 6 reconfigured the board into it:
   the screen came up as Korvet Nano and the OSD switched to the Korvet's
   menus and caption, which means the MCU saw the coldboot, restarted, and
   read core id 8.  The user confirmed all of it.

**The mechanism is complete and works end to end.**  A machine is chosen
from the OSD, written to the flash from the card, and the board comes up as
that machine.

Two things fall out of step 7 that are worth keeping:

- **A JTAG RELOAD reconfigures this FPGA from flash address 0.**  So it is
  only the RECONFIG_N *pin driven from user logic* that cannot trigger a
  reload; the configuration controller itself is perfectly willing.  That
  is more evidence for A2 - the pad losing its path to the controller when
  it is reused as a GPIO - and it means anything with the FPGA's JTAG can
  reload the board.  Practically: `openFPGALoader -b tangnano20k -r` should
  do it, so a reload can be had from a host without touching the power.
- **The on-board BL616 owns that JTAG**, and TP3..TP6 are TCK, TDO, TDI and
  TMS as test pads.  Four wires from the companion BL616 to those pads
  would let the firmware issue a RELOAD itself and the power cycle would
  go away - but that is four wires against one for RECONFIG_N (pin 48 to
  TP1), so the single wire remains the better option if one is ever wanted.

**An install takes about 20 seconds**, measured on the board - erase, write
and verify of 907 418 bytes, every byte of it crossing the m0s link twice.
Close enough to the arithmetic's ten-odd seconds, and cheap enough that the
flash-to-flash copy engine is **not worth building**: 20 s to change
machines, once, is not a cost anyone will notice.

## What is next

The mechanism is done.  What is left is polish and the things a second
session should confirm:

1. **Install the other two and boot them**, so all three are known to work
   and not just the Korvet: PK8000 from the Korvet, then back to the UKNC.
2. **A console would still help.**  `mnano/main.c` puts it on the companion
   BL616's GPIO 21/22 at 2 000 000 baud, which needs a 2 Mbit-capable
   USB-UART; dropping `CONSOLE_BAUDRATE` to 921600 is a one-line change.
   Nothing needs it - the OSD carries the install's progress and every
   failure string - but `flash_probe()`'s output only goes there.
3. Every core's own behaviour under this firmware rather than its own: the
   merge is by hand, the keymaps are copied verbatim, and the host test
   says the forms agree, not the keyboards.
4. `LOADING_RATE`, still untried, and still the whole of the configuration
   wait.

Not worth doing: the flash-to-flash copy engine.  At 20 s an install there
is nothing to buy with it.

## Defects

1. **RECONFIG_N driven from user logic does not reload this FPGA.**  Not in
   the way any more - the design does not depend on it - but it is the
   reason a switch costs a power cycle.  A JTAG RELOAD *does* reload the
   device (seen 13 Sep 2026), so the controller is willing and it really is
   the pad that loses its path when reused as a GPIO.  One wire from header
   pin 48 to TP1 would remove the power cycle.  Was:  Seen on the board 13 September 2026 and
   narrowed by the LED witness to case A - the pulse is generated and no
   configuration is attempted.  Whether pin 9 is held high from outside
   (A1) or the controller ignores it (A2) is not measured.  Everything
   either side of the pulse - CMD 9, the magic byte, the counter, the
   flash contents, the three jump addresses, the walk - is verified.
