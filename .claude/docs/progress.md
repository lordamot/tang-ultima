# Progress

> Picking this up cold?  Read `handover.md` first - it is the snapshot of
> what is on the board, what is committed, and the one decision left open.
> This file is the running record of how it got there.

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

## The on-board BL616, 13 September 2026 - targets prepared, nothing flashed

Asked for: make targets to put the factory firmware back on the board's
own BL616 and to put upstream's FT2232-emulating firmware on it, as the
first step towards a firmware there that loads the FPGA's SRAM from the
card (route D).  Done on the host, none of it run on the board:

- `bin/onboard/` fetched from MiSTle-Dev/FPGA-Companion (commit
  `49ebb11`, release v1.4.29), sha256 pinned in the Makefile: the factory
  `friend_20k` plain and encrypted, Sipeed's `bl616_fpga_partner_20kNano`
  (the FT2232 emulation that starts a second stage at 0x40000), and
  upstream's `fpga_companion_nano20k` as a known-good second stage.
- `make onboard-status` (seen: `0403:6010 SIPEED 20K's FRIEND
  2023030621`), `onboard-backup`, `onboard-efuse`,
  `flash-mcu-onboard-{orig,orig-encrypted,ftdi,stage2,restore}`;
  `tools/onboard.sh` refuses a write unless exactly one BL616 is in boot
  mode and it cannot be the dock; `tools/efuse_bl616.py` decodes
  `ef_sf_aes_mode` from efuse word 0 (the SDK's `ef_data_reg.h`).
- The finding that shapes the plan: Sipeed's Partner is encrypted and
  runs only on a chip with the flash-encryption efuse set (boards from
  ~2024).  This board's serial string reads as March 2023.  If the fuse
  is clear, the Partner cannot run and a custom firmware on that chip
  and the PC-side programmer are mutually exclusive - the FRIEND goes
  back for every `make flash-image`.  `make onboard-efuse` settles it
  before anything is written.
- Recovery: the ISP is in mask ROM, UPDATE-at-power-up always reaches
  it, nothing here writes efuses, and the FPGA boots from its own flash
  whatever the BL616 holds.  The table is in `onboard.md`.

Then run, the same evening, all seen on the board (`onboard.md`, "What
was seen"): the backup reads (1 MB, 2.6 s, a BFNP image of ~82 KB
matching no upstream file exactly); the efuse reads and says **fused**
(`EF_CFG_0 = 0x431`, AES128) - the 2023 serial string was no guide; the
**FPGA Partner was written, verified by the chip's own SHA256, and
runs**: `SIPEED "USB Debugger"`, and openFPGALoader still detects the
FPGA through it.  So on this board the programmer and a second-stage
firmware at 0x40000 can coexist.

## Route D works, 13 September 2026, 18:52

A stage 2 of our own (`onboard/`: bit-banged JTAG on GPIO 10/12/14/16,
openFPGALoader's Gowin SRAM sequence routine for routine, a core staged
in the chip's own 4 MB flash behind a descriptor sector, every step
logged to a flash sector for reading back) was written to 0x40000 and
the UKNC staged at 0x100000.  On a power bank, with the Korvet in the
FPGA's flash: the screen went black and came up as the **UKNC**.  The
log: idcode `0x081b`, status before `DONE_FINAL` (the Korvet had
configured), erase ok, load ok, `DONE_FINAL` after, **1 145 ms** for
907 418 bytes.  The FPGA's flash was not touched.  So: **a core can be
switched into the FPGA's SRAM by the board's own BL616 in about a
second, with no wires**, which is the instant switch coreswitch.md
priced at 3-5 days and called sound; it took an evening.

What remains for it to be the switch the OSD offers: the choice has to
reach the on-board chip and the bitstreams have to reach its flash.

## The OSD-driven switch, 13 September 2026, evening - built, first test failed

Designed and built the same evening (`onboard.md`, "The switch through
stage 2"): CMD 11 + `mister/coreload.v` in all three cores (lint, build,
timing all pass), stage 2 v2 as a UART command server, `mnano/coreload.c`
and the Core form's "Save to flash".  All flashed: stage 2, the dock, the
new Korvet at flash address 0 (JTAG readback identical), the card.  On
the power bank, Core -> UKNC sat on "Connecting" and nothing switched.
Not diagnosed - the host was about to hang.

## The switch works, 13 September 2026, later that evening

**Seen on the board, by the user: Korvet -> PK8000 -> UKNC, each chosen
from the OSD, each running seconds later; no flash written, no power
cycle.**  Then, with the diagnostic firmware below, again, "from the
very first attempt".  That is route D as the OSD's switch, and it is the
mechanism this repository is for.

How the evening went, because the record is the point:

- The desk-check of the handover's suspects first, all on paper: the
  BL616's UART pins agree with upstream from two directions
  (MiSTeryNano's `nano20k/atarist.cst`: `spi_irqn` on 69, an output;
  FPGA-Companion's `mcu_hw.c`: `SPI_PIN_IRQ = GPIO 13 "in UART RX,
  crossed"`, `PIN_UART_TX = GPIO 11`); the baud is exact on the BL616
  (XCLK 40 MHz / 20) and 0.012% off on the Korvet; CMD 11 is CMD 10's
  lines.  Nothing to find by reading.
- `make onboard-log` (20:00): START 20260913, FLASH, FLASHSIZE, nothing
  more - **stage 2 v2 had run and waited**.  Suspect 1 closed.
- Stage 2 given a link log - `LOG_PINS` (GPIO 13 and 11 read as inputs
  at start), `LOG_RX`/`LOG_TX` (the first 32 bytes each way, with the
  ms), later `LOG_READY` and a log that survives its loads - and the
  dock's `cl_ping()` split into `CL_ERR_CMD11` (the FPGA's FIFO never
  offers room; the status byte shown) and `CL_ERR_LINK` (room, but no
  answer).  `make fw` builds, `menu-test` passes.
- Then the user's test, on the stage 2 with the link log and the dock
  firmware from the evening before: **the first attempt after power-up
  failed on "Connecting", every one after it switched** - Korvet ->
  PK8000 -> UKNC.
- Then my error: `make flash-mcu` wrote `bin/bl616.bin` - the committed
  15:26 firmware, the flash-writing design - not `build/fw/bl616.bin`;
  the flash tool's "Read SHA256/461712" was the old file's size and I
  did not read it.  The board "regressed" to erasing its flash; the user
  cut its power mid-install.  Stage 2's log for that power-up: pins
  `3`, listening from 652 ms, **no byte ever arrived** - consistent, the
  switch was never asked.  The board came up normally afterwards, so the
  erase had not begun or the install had finished.  `flash-mcu` now
  refuses when `build/fw/` is newer than `bin/`, and CLAUDE.md has the
  trap.
- The right firmware on the dock (465 392 bytes, SHA `f36077c7...`,
  21:13), stage 2 v3 at 0x40000 (SHA `4203ed7f...`): **"it now works
  like it should", and "from the very first attempt"**.

Not explained: the one failed first attempt, seen once on the earlier
dock firmware and not since.  The likeliest account is a ping before
the Partner had handed over to stage 2, but that is a guess; the log
now keeps the first attempt's bytes and times if it comes back.

Not tested under this firmware: **"Save to flash"** (`ultima_install()`,
the same `flash_install()` that installed the Korvet the evening before,
byte-verified; but the same function is an argument, not a test), the
UKNC's serial port after a switch (its `active` mux hands pin 69 back on
`cl_release()`), and what a switch does to `/sd/ultima.ini` (nothing -
so the `.ini` names what the flash holds, not what is running).

## The keyboard that goes away, 13 September 2026, night

Reported: the USB keyboard is lost "from time to time" - F12 does
nothing - and only a power cycle brings it back; the user's guess was
the keyboard's power saving.  Read, not seen: `usb_host.c` polled
`/dev/inputN` every 100 ms and deleted the reader thread when the name
went; CherryUSB kills a detached device's URB without calling its
callback (`usb_hc_ehci.c`, `usbh_kill_urb`), so a thread blocked on a
URB with no timeout stays blocked; a device that detaches and
re-attaches within one poll gets the same name and the poll sees
nothing.  A keyboard that sleeps and re-attaches on waking is exactly
that device.  Changed to the stack's own hooks (`usbh_hid_run`/`stop`,
weak in this SDK's `usbh_hid.c`), a stop flag, a 1 s URB timeout and a
CLEAR_FEATURE on a stalled endpoint - upstream FPGA-Companion's shape
since Feb 2026.  Builds here and in all three siblings (the same patch
applies to each `mnano/usb_host.c`); their CHANGELOGs say so.  **Not
seen fixed**: the test is the keyboard sleeping and waking under the
new firmware, and the dock's second LED (lit while a keyboard is
enumerated) tells this failure from a device that really left.

## The switch on PC power, 13 September 2026, night - left as is

With the Tang on the PC's USB the switch does nothing: the Partner sees
a host and stays the programmer, stage 2 never runs, and the OSD says
"Not on PC power?".  That is the Partner's rule, in Sipeed's signed
image, not ours.  The way round it exists and was priced: upstream's
`bl616_bootloader_0x20000_nano20k_signed.bin` (FPGA-Companion #170, May
2026) at address 0 starts whatever is at 0x20000 unconditionally, PC or
not - at the cost of the Tang's USB-C ceasing to be a programmer until
`make flash-mcu-onboard-ftdi` puts the Partner back.  **Decided: leave
it** - the board runs off a charger or a power bank, and the PC-side
programmer is kept.  If that changes: fetch the bootloader sha-pinned,
put stage 2 at 0x20000 as well as 0x40000, add the two targets.

## The fourth core, 14 September 2026 - the ZS-256, built, walked, and seen on the board

**Seen on the board, 14 September 2026, evening: the user reports the
ZS-256 works under this firmware ("it works").**  What that covers
beyond the machine coming up - the switch to it, its ROMs from
`/sd/zs256/`, the keyboard - was not itemised; the list below of what
was checked here stands as the account of the build, and the "NOT
verified" list is now what the report did not itemise, not what is
unseen.

`../tang-zs256` (ZS-256 Nano: Sergey Zonov's Scorpion ZS-256 Turbo+,
started 13 Sep 2026 on Korvet Nano's method, and written to be a core
of this repository from the start) is the fourth core.  Nothing in the
switch changed for it; what changed is every place the number three
was written down.

What was verified, and how:

- **The ZS-256 has all the reconfig support already**: its
  `mister/coreload.v` and `mister/flashwr.v` are byte-identical to the
  other three siblings' (cmp), its `sysctrl.v` decodes CMD 10 and 11
  and answers CMD 0 with id 9, `gowin_tcl.py --abs` and the PnR-dir
  argument of `timing_check.py` are there, `"MSPI": true` is in its
  process config.  **Nothing in the sibling's tree was edited**; its
  `git status` is what it was.
- **`make core-zs256` builds out of the sibling's tree**: about 70 s,
  logic 9334/20736 (46%), registers 21%, timing gate clean (0 setup, 0
  hold violated over clk27, clk42, the two PLLs and spi_clk).  The pin
  report has `mspi_*` on 59/60/61/62 and `uart_tx`/`uart_rx` on 69/70.
  `bin/zs256.bin` is 907 418 bytes like the other three, byte-identical
  to Gowin's own `impl/pnr/zs256.bin`, `a5 c3` at 0x16, IDCODE
  `0x0000081b` at 0x1c.
- **`make lint-zs256`**: ok.
- **`make fw` builds**: 469 296 bytes (was 465 952), copied to
  `bin/bl616.bin`.  What went in: `CORE_ID_ZS256` 9, `sys_poke24()`
  (CMD 6 with three address bytes - the ZS-256's meaning of the
  command; the firmware only sends it to core 9), `zs256.h` verbatim,
  `romload.c/h` with the default ROM paths moved from the card's root
  to `/sd/zs256/`, the ZS-256's forms with `S,Core,2`, About,
  variables, `rom_boot()` at `menu_init`, `rom_select()` from the
  file selector's slot 5, its own Debug page byte map, 'm' resetting,
  `drivename()` "A B C D HDD", `ultima_cores[]` fourth entry,
  `ULTIMA_CORES` 4, the Core form's fourth `C` entry.  The settings
  file saves `drive5=` on the ZS-256 only - the ROM file it names is
  loaded at every start, whereas the UKNC's and PK8000's slot 5 is a
  one-shot browser.
- **ZS-256 Nano's key-report fix is in `usb_host.c` for every core**:
  the six slots of a USB keyboard report are compared as a set, not
  slot by slot (a keyboard packs its slots, so a release of the first
  of two held keys moved the second down a slot and the old compare
  sent a release and a press for a key that never moved - the
  ZS-256's chords counted their shift twice); and a release is sent
  with the OSD open too, so a key held while F12 opened it does not
  stay down in the core's matrix.  The UKNC's `kbd_tx_uknc` already
  dropped the releases it never forwarded, so that path is unchanged
  in effect.  **Not seen on a board on any core.**
- **`make menu-test`: 40 screens, 0 errors** over four cores (was 28
  over three): every ZS-256 form walked, the Core form six entries on
  every core with the running one marked and "Save to flash" last, and
  a new check that picking a file in the ZS-256's ROM slot calls
  `rom_select()` with `/sd/zs256/GAME.rom`, mounts nothing, remembers
  the name in slot 5 and closes the OSD.

What was NOT verified here, and what the board report did not itemise:

- "Save to flash" with the ZS-256, its Debug page, and how much of the
  machine was exercised (its own repository's `progress.md` is where
  the machine's own state is kept).
- `/sd/zs256/zs256.rom` and `gs105a.rom` must be put on the card by
  hand (`make card` says so); the machine executes zeros without them.
- The key-report change on the other three cores.

## The fifth core, 24 September 2026 - the BK, built and walked, NOT on a board

`../tang-bk-epta` (BK Nano: the БК-0011М with MAXIOL's AZBK controller,
started 19 Sep 2026 on ZS-256 Nano's method and written to be a core
of this repository from the start; its own `progress.md` has it running
Dangerous Dave on a board under its own firmware, 23 Sep) is the fifth
core.  Nothing in the switch changed for it; what changed is every
place the number four was written down, and two generic things the
sibling found on its board.

What was verified, and how:

- **The BK has all the reconfig support already**: its
  `mister/coreload.v` and `mister/flashwr.v` are byte-identical to
  ZS-256 Nano's (cmp), its `sysctrl.v` decodes CMD 9, 10 and 11 and
  answers CMD 0 with id 10, `gowin_tcl.py --abs` and the PnR-dir
  argument of `timing_check.py` are there, `"MSPI": true` and
  `"RECONFIG_N": false` are in its process config, and its `.cst` has
  `mspi_*` on 59/60/61/62, `uart_tx`/`uart_rx` on 69/70, `reconfig_n`
  on 48 - and the PnR pin report of the build here says the same (all
  bank 3 but the UART's bank 1).  **Nothing in the sibling's tree was edited**; its `git
  status` is what it was (one untracked file of its own).
- **`make core-bk` builds out of the sibling's tree**: logic
  11576/20736 (56%), registers 35%, timing gate clean (0 setup, 0 hold
  violated over clk27, clk64, the two PLLs' outputs and spi_clk).
  `bin/bk.bin` is 907 418 bytes like the other four, byte-identical to
  Gowin's own `impl/pnr/bk.bin`, `a5 c3` at 0x16, IDCODE `0x0000081b`
  at 0x1c; an install would be the same 14 block erases and 3545 page
  writes.
- **`make lint-bk`**: ok.
- **`make fw` builds**: 483 808 bytes (was 469 296), copied to
  `bin/bl616.bin`.  What went in: `CORE_ID_BK` 10, `azbk.c/h` and
  `bk.c/h` verbatim from the sibling (`AZ_ROOT` is `/sd/bk`, which is
  `ultima_cores[]`'s `"bk"` - by hand, as the sibling reads the same
  folder), `sys_peek24()` (CMD 8 with a ready byte - the BK's meaning
  of the command; the Korvet's ExtROM is CMD 8 too, and each is only
  sent to its own core), `SPI_SYS_PEEK`, irq 4 dispatched to
  `az_handle_event()` on core 10 and `extrom_handle_event()` otherwise,
  `kbd_tx_bk()` on the modifier, release and press paths of
  `usb_host.c` with `keymap[10]`/`modifier[10]` NULL and checked, the
  BK's forms with `S,Core,2`, About, variables, `menu_bk_mount()` for
  the four AZ unit slots (never `sdc_image_open`), `menu_bk_boot()`
  between `R=3` and `R=0`, its own Debug page byte map with `azbk.c`'s
  lines after it, `drivename()` "AZ0..AZ3 -", `ultima_cores[]` fifth
  entry "BK-0011M", `ULTIMA_CORES` 5, the Core form's fifth `C` entry.
  The only warnings in the build are the two that were there before
  (an unused `hexdump`, `M0S_DOCK` redefined).
- **Two of the sibling's changes are now in every core's build**: the
  SPI task's stack is 2048 words instead of 512 (`spi.c` - `azbk.c`
  serves the AZ's block reads from that task through FatFs, and the
  sibling's fourth board hung in its first read with the OSD dead on
  512), and `sdc_read_sector()`'s two waits are bounded, a failed wait
  counted by `sdc_timeouts()` and returned to FatFs as `RES_ERROR`
  (before: `// todo: add timeout`, and a card that stopped answering
  spun for ever with the SPI mutex held).  **Not seen on a board on any
  core under this firmware.**
- **`make menu-test`: 50 screens, 0 errors** over five cores (was 40
  over four): every BK form walked, the Core form seven entries on
  every core with the running one marked and "Save to flash" last, and
  new checks that on the BK `az_boot()` is called once with `R=3` sent
  and `R=0` not yet (the machine held while the ROMs arrive), that no
  `sdc_image_open` happens at start, and that picking a file in an AZ
  unit's selector calls `az_set_unit()` with `/sd/bk/GAME.img`, opens
  nothing in `sd_card.v`, remembers the name in the slot, closes the
  OSD, reopens on the file, and "No Disk" unmounts it the same way.

What was NOT verified here:

- **The BK on a board under this firmware, at all.**  Its own
  repository has it running on a board under its own firmware; this is
  the same core built here and the same `azbk.c`, but the merge is by
  hand and that is an argument.  The switch to it, its keyboard through
  the merged `usb_host.c`, its disks through the AZ, "Save to flash"
  with it: none seen.
- `/sd/bk/` must be filled by hand with MAXIOL's card package
  (`../tang-bk-epta/soft/azbk/`: `AZ.INI`, `ROM/`, `DISKS/`; the
  sibling's `make card` stages it); `make card` here says so.  Without
  `AZ.INI` the machine restarts for ever on empty memory.
- The 2048-word SPI task and the bounded card waits on the other four
  cores.
- The FreeRTOS heap after the larger task: the firmware links and the
  SDK's static allocation sizes it; whether anything else was near the
  limit is not measured.

## The BK brought to 0.1.28, 25 September 2026 - built and walked, NOT on a board

BK Nano's commit `5163f79` (25 Sep, its 0.1.21 -> 0.1.28): Dangerous
Dave's buzz was the legacy 8-bit Covox on 177714 playing the AY's
register writes; a new Hardware switch "Covox 177714" (`'c'`, Off by
default, "AZ setup" the old behaviour) gates it in `azsound.v`
through `sysctrl.v`'s `system_covox`, and the OSD's Debug page is gone
at the operator's word (the core's dbg bus stays, for the testbench).
The sibling has it working on its board under its own firmware.

What changed here: `mnano/menu.c` only, by hand from the sibling's -
the Hardware form's `L,Covox 177714:,Off|AZ setup,c;`, `{ 'c', { 0 }}`
in `variables_bk[]`, `T,Debug,;` out of the BK's main form and the BK
branch out of `menu_debug_open()` (nothing reaches it).  `azbk.c/h`
and `bk.c/h` did not change (cmp).  The About text still has no
version in it, as before.  And the Makefile: `make core-<x>` now packs
`bin/<x>.bin` too - it stopped at the `.fs`, and a `make core-bk`
left the card's `bin/bk.bin` at yesterday's core (caught by cmp
against Gowin's).

What was verified: `make core-bk` from the sibling's tree - logic
11917/20736 (58%), registers 35%, timing gate 0 setup / 0 hold
violated; `bin/bk.bin` 907 418 bytes, byte-identical to Gowin's
`impl/pnr/bk.bin`, IDCODE checked by `mkimage.py`.  `make lint-bk`
ok.  `make menu-test` 49 screens, 0 errors (one fewer: the BK's
Debug page); the Hardware form shows the Covox line.  `make fw`
builds, 482 000 bytes, copied to `bin/bl616.bin`.  The sibling's tree
untouched.

NOT verified: any of it on a board under this firmware - the BK
itself still has not been switched to here (see the fifth core).
With the Debug page gone, a board session learns whether `AZ.INI` was
read only from the machine starting, or from the dock's serial log.

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
