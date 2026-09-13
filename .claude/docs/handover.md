# Handover, 13 September 2026

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
on-board BL616         stock Sipeed firmware, untouched
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
