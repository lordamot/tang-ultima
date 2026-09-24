# Handover, 24 September 2026 - the fifth core, not yet on a board

**The BK (`../tang-bk-epta`, BK Nano - the БК-0011М with MAXIOL's AZBK
controller) is integrated as the fifth core** - built out of its tree,
timing clean, linted, its menu walked on the host with the other four,
the firmware built and copied to `bin/bl616.bin` (483 808 bytes).
**Not on a board under this firmware.**  `progress.md` ("The fifth
core") has what was checked and how, and what was not.  Nothing
committed yet; the sibling's tree is untouched.

What a board session would do, in order: `make card` (five `.bin`s
into `/cores/`, and `/bk/` filled with the sibling's `soft/azbk/` -
`AZ.INI`, `ROM/`, `DISKS/` - by hand; the machine does not start
without `AZ.INI`); `make flash-mcu` (the dock alone on the PC;
`bin/bl616.bin` is current); switch to "BK-0011M" from the Core form
off PC power; F12 and the Debug page (the "ROM:" line from `azbk.c`
says whether `AZ.INI` was read and how many files went in, "verify"
how many words came back wrong).  A BK that does not come up is first
a question for its own repository, whose `progress.md` has it running
on a board under its own firmware since 23 Sep; what is new here is the
merge - `menu.c`, `sysctrl.c`, `usb_host.c` - and the two generic
changes below.

Two things that changed for every core, both from the sibling's board:
the SPI task's stack is 2048 words (`azbk.c` runs FatFs from it), and
`sdc_read_sector()`'s waits are bounded (`sdc_timeouts()`).  Neither
seen on a board under this firmware.

---

# Handover, 14 September 2026 - the fourth core

**The ZS-256 (`../tang-zs256`) is integrated as the fourth core** -
built out of its tree, timing clean, linted, its menu walked on the
host with the other three, the firmware built and copied to
`bin/bl616.bin` (469 296 bytes) - **and seen working on the board the
same evening** (the user: "it works").  So the dock now carries this
firmware and the card `/cores/zs256.bin` and `/zs256/` with its ROMs;
the night handover below is otherwise the physical state of the board.
Not itemised in the report: "Save to flash" with the ZS-256, and the
key-report change on the other three machines.

What a board session did, in order: `make card` (four `.bin`s into
`/cores/`, and `/zs256/zs256.rom` + `gs105a.rom` from
`../tang-zs256/soft/rom/` by hand - the machine executes zeros without
them); `make flash-mcu` (the dock alone on the PC; `bin/bl616.bin` is
current); switch to the ZS-256 from the Core form off PC power; F12 and
the Debug page (`ROM 64 KB, GS ROM 32 KB` on its last line says the
ROMs arrived).  A ZS-256 that does not come up is first a question for
its own repository, whose `progress.md` says nothing of it has been on
a board yet - it came up.

`progress.md` ("The fourth core") has what was checked and how.  Two
things that changed for every core: `usb_host.c` compares the key
report as a set (ZS-256 Nano's fix, 14 Sep) and sends releases with the
OSD open; and the README describes the switch as it is (it still said
MultiBoot).  Nothing committed yet; the sibling's tree is untouched.

---

# Handover, 13 September 2026, night - READ THIS FIRST

**The switch works, on the board, from the OSD: Korvet -> PK8000 ->
UKNC in seconds, no flash written, no power cycle, from the very first
attempt.**  Seen by the user the same evening; `progress.md` ("The
switch works") has the whole evening, including the one wrong flash.
CLAUDE.md's mechanism paragraph is current.

## The physical state, right now

```
FPGA flash address 0   a core with CMD 11 in it (the Korvet, unless the
                       interrupted install of the evening finished - it booted,
                       so it is one of the three); the old MultiBoot image is
                       still behind it from 0x100000, stale, harmless
companion BL616 (dock) bin/bl616.bin = build/fw/bl616.bin, 465 952 bytes, SHA
                       59d9b9da..., 13 Sep 21:41: the stage 2 switch, the
                       ping's two failure texts, "Save to flash" as the Core
                       form's 4th entry, the USB keyboard fix.  VERSION still
                       0.1.1 alpha
SD card                /cores/{uknc,pk8000,korvet}.bin, all three with CMD 11
                       /ultima.ini - names what the flash holds, NOT what is
                       running; a switch does not touch it, "Save to flash" does
on-board BL616         Sipeed's FPGA Partner at 0 (openFPGALoader works on a
                       PC); OUR STAGE 2 v3 at 0x40000, SHA 4203ed7f..., 13 Sep
                       20:18: the UART command server with the link log; the
                       last switched core staged at 0x100000.  Fused, 4 MB.
                       Factory image in bin/onboard/backup/
```

## What is NOT tested

1. **"Save to flash"** under this firmware.  It is `flash_install()`,
   which installed the Korvet the evening before with a byte-exact JTAG
   readback; the function is unchanged, but that is an argument.  The
   user has not tried the entry.
2. **The one failed first attempt.**  On the dock firmware from the
   evening before, the first switch after power-up sat on "Connecting"
   and every later one worked; on the current firmware it worked first
   time.  Not explained - a ping before the Partner had handed over to
   stage 2 is the guess.  If it comes back: the OSD's second line now
   says `no answer from stage 2 (st xx)` or `CMD 11: FIFO never has room
   (st xx)`, and `make onboard-log` shows whether the `P` arrived, when,
   and what went back (`LOG_RX`, `LOG_TX`, `LOG_READY`).
3. The UKNC's own serial port after `cl_release()` hands pin 69 back;
   each machine's behaviour under this firmware rather than its own.
4. How long a switch takes end to end.  The budget said 10-12 s; "seconds"
   is all that was said.
5. **The USB keyboard fix** (`mnano/usb_host.c`, and the same patch in
   the three siblings): the keyboard was lost until a power cycle, most
   likely a sleeping keyboard re-attaching inside the old 100 ms poll;
   now the stack's own hooks, a stop flag, a 1 s URB timeout (with
   `errorcode` cleared before each submit - this SDK refuses a killed
   URB otherwise, and the first build was dead from boot for it), a
   CLEAR_FEATURE on a stall.  Keyboard works from boot on the dock's
   current firmware (`59d9b9da...`); whether it survives the keyboard's
   sleep is the test, not yet reported.

## Decided, not to be redesigned quietly

The switch needs non-PC power.  On a PC the Partner stays the
programmer and stage 2 never runs - Sipeed's rule, not ours.  Upstream's
unconditional bootloader (`bl616_bootloader_0x20000_nano20k_signed.bin`,
FPGA-Companion #170) would lift that at the cost of the USB-C
programmer; priced on 13 Sep and **declined** - `progress.md`, "The
switch on PC power".

## The two traps of the evening

- `make flash-mcu` flashes `bin/bl616.bin`.  `make fw` leaves the build
  in `build/fw/`.  Copy it, or `FW_BIN=build/fw/bl616.bin`; the target
  now refuses when `build/fw/` is newer.  The 461 712-byte file is the
  old flash-writing firmware - if the flash tool prints that size, stop.
- Every `flash-mcu*` target writes whatever BL616 is `/dev/ttyACM0`.
  The dock and the Tang each go on the PC alone.

## What is committed

Four repositories, all on `main`, **nothing pushed**:

```
tang-ultima    9bb4abd  switch cores through the board's own BL616, into the FPGA's SRAM
               4881e83  fix the USB keyboard lost until a power cycle
tang-uknc      5d626ea  add a UART to the on-board BL616 and fix the lost USB keyboard
tang-pk8000    f3a783d  (same)
tang-korvet    e919628  (same)
```

Left uncommitted in the siblings, as before: their `bin/tang.fs` and
`tang/impl/` rebuild output (uknc 4 Sep, korvet 13 Sep 11:31, from
their own sessions), and in tang-pk8000 two PDFs and `prompts/4`, `5`
that are not mine.  Their shipped `bin/bl616.bin` and `bin/tang.fs` do
not carry CMD 11 or the keyboard fix until they rebuild them.  The host
gcc 15.2 ICEs at -O1 on ff.c: `HOST_OPT=-O0` in the Makefile is the
workaround.

---

# Handover, 13 September 2026 (afternoon)

(The evening handover between this and the night one - the suspect
list for the switch that did not connect - is resolved and folded into
`progress.md`.)

Written at the end of the session that made the core switch work.  It is a
snapshot: what is on the board right now, what is committed, what is known,
and the one decision left open.  `progress.md` is the running record and
`coreswitch.md` is the design; this is the thing to read first when picking
the project back up.

## Where it stands

**The switch works.**  A machine is chosen in the OSD, written into the
FPGA's configuration flash from the SD card, and the board comes up as that
machine.  Verified on the board, not inferred: the Korvet was installed,
the flash read back over JTAG and compared byte for byte with
`bin/korvet.bin`, and it booted.  An install takes **about 20 seconds**.

It took two designs.  The first, Gowin MultiBoot, is dead on this board -
RECONFIG_N cannot be pulsed from user logic - and `multiboot.md` keeps that
account under a SUPERSEDED banner.

## The physical state, right now

```
FPGA flash address 0   the KORVET core (installed by the OSD in this session)
                       the old three-slot MultiBoot image is still behind it
                       from 0x100000 on, stale and unreferenced - harmless
companion BL616        firmware from this session, but built BEFORE the
                       version bump, so its OSD caption still says 0.1.0
SD card                /cores/{uknc,pk8000,korvet}.bin  (907 418 bytes each)
                       /ultima.ini  core=uknc   <- WRONG NOW, the flash has
                       korvet in it; the next successful install fixes it,
                       or edit it by hand
                       /uknc/ /pk8000/ /korvet/  untouched
on-board BL616         Sipeed's FPGA PARTNER at address 0 since 13 Sep 18:36
                       ("USB Debugger"; openFPGALoader works); OUR STAGE 2
                       (onboard/) at 0x40000, the UKNC staged at 0x100000:
                       on non-PC power the chip loads the UKNC into the
                       FPGA's SRAM in 1.1 s - SEEN 13 Sep 18:52.  Chip is
                       FUSED (AES128), 4 MB flash.  Factory image backed up
                       in bin/onboard/backup/ - onboard.md
```

Two loose ends that follow from that:

- **`make flash-mcu`** to put the 0.1.1 firmware on the dock.  Cosmetic -
  only the caption differs.
- **Only the Korvet has been installed and booted.**  The PK8000 and the
  UKNC go through identical code paths, but that is an argument, not a
  test.  Installing each of them is the first thing worth doing.

## What is committed

Four repositories, all on `main`, **nothing pushed**.

```
tang-ultima    5b60079  switch cores by writing the flash, not by MultiBoot
tang-uknc      c0d053a  add a flash writer for tang-ultima
tang-pk8000    49de8f4  add a flash writer for tang-ultima
tang-korvet    2a77755  add a flash writer for tang-ultima
```

`VERSION` is `0.1.1 alpha` and the committed `bin/bl616.bin` carries it.
The siblings still hold pre-existing dirt from 4 September - build output,
and in tang-pk8000 two PDFs and `prompts/4`, `5` - which is not mine and was
left alone.

## What is known, and how

Everything below was seen on the board, not reasoned out.  The point of
listing it is that this project has twice been wrong about what "looks
right" means.

- Driving pin 9 from user logic reconfigures nothing.  An LED latched off
  `sys_reconfig` keeps blinking through CMD 9, so the pulse fires and no
  configuration is attempted.
- **A JTAG RELOAD does reconfigure the device from flash address 0** - that
  is what booted the Korvet without a power cycle, via openFPGALoader's
  end-of-operation reset.  So the controller is willing; the pad is not.
- `-use_mspi_as_gpio` does not stop the FPGA booting from the flash it then
  takes over.
- `openFPGALoader --file-type bin -o 0` writes correctly (read back and
  compared).
- An install is 20 s, and byte-exact.

The schematic facts that took real digging - which pin reaches what, the
test pads, what the on-board BL616 is and is not wired to - are in
`coreswitch.md` under "Could a switch be instant?".  They are worth not
re-deriving.

## The open decision

**Should the power cycle go, and at what price?**  Five routes, compared in
full in `coreswitch.md`; the short of it:

| | wires | switch | effort | the catch |
|---|---|---|---|---|
| **A** keep what we have | 0 | 20 s + power cycle | done | the erase window |
| **B** wire pin 48 -> TP1 | 1 | 20 s + ~3 s | ~1 h | external pulse unproven |
| **C** wire + revive MultiBoot | 1 | **~3 s** | ~3-4 h | external pulse unproven |
| **D** on-board BL616 loads SRAM | **0** | ~3-8 s | ~3-5 days | replaces the board's only programmer |
| **E** companion BL616 loads SRAM | 4 | ~2-8 s | ~2-3 days | JTAG contention, unmeasured |

The recommendation on record: **if the power cycle is to go, it is one wire
and then C** - a 3 s switch that never writes the FPGA's flash again, which
also deletes the only dangerous thing in the current design.  If it is not
to go, **A works, is committed, and is the optimum for zero wires**.

D is the only zero-wire route to an instant switch and it is sound
engineering; the reason to refuse it is that it trades a board that needs a
power cycle for a board that might not be programmable.

Nothing in the tree depends on which way this goes.

## If the answer is "leave it alone"

Then the work left is small and none of it is structural:

1. Install the PK8000 and the UKNC, to test the paths the Korvet did not.
2. `make flash-mcu` for the version bump.
3. A console would still help: `mnano/main.c` puts it on the dock's GPIO
   21/22 at 2 000 000 baud, which needs a 2 Mbit-capable USB-UART.  Dropping
   `CONSOLE_BAUDRATE` to 921600 is one line.  Nothing needs it - the OSD
   carries every install message - but `flash_probe()`'s output goes only
   there.
4. Each machine's own behaviour under this firmware rather than its own.
   The merge is by hand, the keymaps are copied verbatim, and `make
   menu-test` says the forms agree, not the keyboards.
5. `LOADING_RATE`, still untried.

**Not worth doing:** the flash-to-flash copy engine.  It would take an
install from 20 s to about 6 and buy nothing.
