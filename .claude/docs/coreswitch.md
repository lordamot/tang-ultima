# The core switch: how one board becomes three machines

This is the second design, and it **works on a board** - 13 September 2026,
the Korvet installed from the OSD, read back over JTAG byte-identical, and
booted.  The first design was Gowin MultiBoot and it does not work here;
`multiboot.md` is kept as the record of what was tried and what the board
showed, and `progress.md` has the dates.  Read the one paragraph under "Why
not MultiBoot" before changing anything here.

## The shape of it

```
flash   0x000000   one bitstream, 907 418 bytes - THE machine the board is
        0x0e0000   ... unused, and the remaining 7 MB with it

card    /cores/uknc.bin      the three machines, packed bitstreams,
        /cores/pk8000.bin    byte for byte Gowin's own .bin
        /cores/korvet.bin
        /ultima.ini          core=<name>, which one is in the flash
        /uknc/ /pk8000/ /korvet/   each machine's own files, as before
```

**Power-up always loads flash address 0.**  That is the whole mechanism.
Switching machines means writing a different bitstream to address 0 and
power-cycling the board, and everything below is in service of doing that
from the OSD, with nothing but the board and the card.

## The path a byte takes

```
/cores/korvet.bin
   |  f_read, 256 bytes at a time              mnano/flashwr.c
   v
sd_card.v  ->  sdc.v  ->  the m0s SPI link  ->  the MCU
   |
   |  SYS CMD 10, sub-command 02: into flashwr.v's 512-byte buffer
   v
flashwr.v  ->  MCLK 59, MCS_N 60, MO 61, MI 62  ->  the W25Q64
```

So every byte crosses the m0s link twice: up from the card and back down
to the flash.  That is the price of the simple version, and it costs time,
not correctness - about 20 seconds an install, measured.

## The FPGA: `mister/flashwr.v`

The MSPI pins are the FPGA's own configuration bus until DONE, and user
logic's afterwards.  UG290 4.1.2, table 4-2, "MSPI PORT / Set as GPIO:
FASTRD_N, MCLK, MCS_N, MO and MI are used as GPIO after configuration" -
and `-use_mspi_as_gpio 1`, which `gowin_tcl.py` emits from the process
config's `"MSPI" : true`, is how you ask for it.  Configuration happens
first, always, so a bitstream that does this still boots from the flash it
then takes over.  openFPGALoader does the same thing from the other side:
its Gowin external-flash path loads a pass-through bitstream into SRAM
that bridges JTAG to these pins.  Every `-f` and `--dump-flash` in this
repository's history went through the FPGA driving them.

`flashwr.v` is deliberately **not** a flash controller.  It knows erase
from program from status not at all; the whole W25Q command set is in
`mnano/flashwr.c`.  What it offers is one transaction:

| SYS CMD 10, first byte | what follows | what it does |
|---|---|---|
| `00` status | - | the next byte back is `{7'b0, busy}` |
| `01` set pointer | hi, lo | where the MCU's reads and writes start |
| `02` write | bytes | into the buffer, pointer post-incrementing |
| `03` read | - | bytes come back, one strobe behind, pointer post-incrementing |
| `04` go | TXhi, TXlo, RXhi, RXlo | CS down, shift TX bytes out of the buffer, read RX bytes back into it from offset 0, CS up |

The engine runs at MCLK = clk/4 - 6.25 MHz on the UKNC's 25 MHz, 10 MHz
on the Korvet's 40 - and the MCU waits on `busy`.  **That is why there is
a buffer at all.**  A bit-level bridge would have had to shift eight bits
to the flash inside one 20 MHz m0s byte time, 400 ns, which is a race
nobody needs; with a buffer the two sides are decoupled completely.

Cost, measured in the UKNC build: **+140 LUTs and one BSRAM block**
(30/46 to 31/46), timing gate unchanged at 0 setup and 0 hold violations.

The MCU must not touch the buffer while `busy` - the one memory port is
shared by a mux on that, not by arbitration.

## The MCU: `mnano/flashwr.c`

Composes W25Q commands out of that one primitive: `06` WREN, `05` RDSR,
`03` READ, `02` page program, `D8` 64 KB block erase, `9F` JEDEC ID.  A
page program is `[02, a2, a1, a0, 256 data]` written into the buffer and
`go(260, 0)`; a read is `[03, a2, a1, a0]` and `go(4, N)`.

`flash_install()` does it in an order chosen so that everything that can
be refused is refused **before** anything is destroyed:

1. `9F` must answer with a Winbond manufacturer byte.  If the MSPI pins
   are not driving a flash, nothing else happens.
2. The file must open, and be between 64 KB and 1 MB.
3. Its first 256 bytes must be a bitstream **for this device**: `a5 c3`
   at 0x16, and IDCODE `0x0000081b` big-endian at 0x1c.  `tools/mkimage.py`
   checks the same two things when it makes the file, so the two agree by
   construction.
4. Erase - and from here until step 5 finishes there is no bitstream at
   address 0.  **This is the one genuinely dangerous stretch.**
5. Program, page by page.
6. Read it all back and compare.  The point of verifying is to find out
   now, while the user is still in front of the board and the card still
   holds the file.

`flash_probe()` is the read-only half and runs from `ultima_boot()` at
every start: the JEDEC ID, the status register, and the head of address 0
checked against the same two fields.  It uses `9F`, `05` and `03` and never
`06`, so it cannot change the chip however wrong it is - and it is what
tells you, before anything is at stake, whether a switch is possible on
this board at all.

`ultima_switch()` closes every mounted image first: the card and the flash
share the m0s link, and a sector request arriving in the middle of a page
program is a transaction that never completes.

## What it costs

- **A power cycle per switch.**  Unavoidable on the board alone.  A JTAG
  RELOAD *does* reconfigure the device from address 0 - openFPGALoader's
  reset at the end of an operation did it by accident on 13 September, and
  `-r` should do it on purpose - so a host can reload without touching the
  power.  Nothing on the board can, because the on-board BL616 owns the
  FPGA's JTAG.
- **About 20 seconds**, measured on the board: 14 block erases and 3545
  page writes, each page 262 bytes up the m0s link and 260 back down it,
  plus the same again to verify.  A copy engine inside the FPGA (source
  address -> BSRAM -> address 0, never touching the link) would cut it to
  about six, and is **not worth building** - 20 s to change machines, once,
  buys nothing.
- **A power cut between the erase and the end of the write leaves a board
  that will not configure.**  It is recoverable - `make flash-image`, and
  the three masters are still on the card - but it needs a host with
  openFPGALoader.  The OSD says "Do not switch off!" for the duration and
  says it again, with "Do NOT switch off - retry", if the write fails.

## Why not MultiBoot

Because the trigger does not exist.  Every bitstream *can* carry the flash
address of the next one, and a low pulse on RECONFIG_N *should* make the
FPGA load it (UG290 7.5.4).  On this board the pulse is provably generated
and the FPGA provably ignores it: an LED latched off `sys_reconfig` keeps
blinking after CMD 9, which it could only do if no configuration had been
attempted.  Pin 9 reaches nothing but test pad TP1 - the schematic's
`PIN09_SYS_~{RECFG}` appears exactly twice, at the FPGA and at TP1 - so
nothing external is holding it high; an 8 mA output against a 100 µA
internal pull-up must reach 0 V.  What is left is that reusing the pad as
a GPIO cuts it from the configuration controller.

**The way back, if anyone wants it:** one wire from header pin 48 to TP1.
`reconfig_n` is still on pin 48 in all three cores, open-drain, with pin 9
left a true RECONFIG_N input, and SYS CMD 9 is still in `sysctrl.v` and
`mnano/sysctrl.c`.  With that wire the instant switch comes back and only
`mnano/ultima.c` would need to change - the walk is in this repository's
git history.  Nothing here depends on the wire being absent.

## Could a switch be instant?  The four routes, and the facts

Asked three times, so the reasoning lives here.  A switch costs 20 s and a
power cycle because **nothing on the board can trigger a reconfiguration**.
That is the scarce thing - not the destination.  Loading the FPGA's SRAM
instead of its flash would still need a trigger *and* a configuration port,
so it is not a way round the problem, it is the same problem.

### The board facts these rest on

Established on the board, 13 September 2026 (`progress.md` has the how):

- Driving pin 9 from user logic reconfigures nothing; the pulse provably
  fires, so reusing the pad as a GPIO cuts it from the controller.
- **A JTAG RELOAD does reconfigure the device from flash address 0.**  So
  the configuration controller is willing; only the pad is not.
- `-use_mspi_as_gpio` does not stop the FPGA booting from the flash it
  then takes over.
- An install is 20 s and byte-exact.

From the Tang Nano 20K schematic v1.3:

```
pin 9  PIN09_SYS_~{RECFG}   appears EXACTLY TWICE: the FPGA, and TP1
test pads   TP1 RECONFIG_N   TP2 GND   TP3 TCK   TP4 TDO   TP5 TDI   TP6 TMS
SD card     FPGA pins 80-85 ONLY (R53..R57 10K pull-ups, R49 22R on CLK)
on-board BL616 (a BL616C, in-package flash) reaches the FPGA on:
            JTAG 5/6/7/8, UART 69/70, SPI_CS/DAT/DIR/SCLK on 86/76/75/13
            and NOTHING else - in particular no SDIO
MODE0/MODE1 pins 88/87, which are the two buttons, strapped for MSPI:
            change them and the board stops booting from flash at all
free pins   25..32 (bank 5), 49 (bank 3); 48 is reconfig_n now.  The LCD_*
            nets do reach the headers - the dock sits on 41/42, which are
            PIN41_LCD_R4 and PIN42_LCD_R3
companion BL616  ~440 KB SRAM; 4 MB flash, mfg partition 1.44 MB at 0x210000
```

### The routes

| | wires | switch | at power-up | writes FPGA flash | effort | risk |
|---|---|---|---|---|---|---|
| **A** what we have | 0 | 20 s + power cycle by hand | direct | every switch | done | the erase window |
| **B** wire pin 48 -> TP1, keep the writer | 1 | 20 s + ~3 s | direct | every switch | ~1 h | external pulse unproven |
| **C** wire + revive MultiBoot, 3 slots | 1 | **~3 s** | direct | **never** | ~3-4 h | external pulse unproven |
| **D** on-board BL616 loads SRAM | **0** | ~3-8 s | double boot | **never** | ~3-5 days | **the programmer** |
| **E** companion BL616 loads SRAM | 4 | ~2-8 s | double boot | never | ~2-3 days | JTAG contention, unmeasured |

**B and C** need one wire, from header pin 48 to TP1.  `reconfig_n` is
already on pin 48, open drain, with pin 9 left a RECONFIG_N input, and CMD
9 is already in every core and in `mnano/sysctrl.c`.  C is the better end
state: a switch becomes a 3 s reconfiguration, the FPGA's flash is never
written again, and the erase window disappears.  `mkimage.py`'s ring
packer, the `--multiboot-addr` plumbing and `ultima.c`'s walk are all in
commit `9e81c1a`; `flashwr.c` would stay, for *installing* a core into a
slot from the card without a toolchain.  **What is unproven for both:** we
know driving pin 9 as a GPIO does nothing; we have never seen a pulse
*arriving* on pin 9 from outside work.  That is the pin's documented normal
use and the controller is demonstrably willing, but it is an inference, and
it is one wire and one build to settle.

**D** is the only zero-wire route to an instant switch, and it is sound.
The on-board BL616 already owns the JTAG and SRAM loading through it is not
speculative - it is what `make flash-fpga` does in every sibling
(`openFPGALoader -b tangnano20k bin/tang.fs`, no `-f`, and `-m/--write-sram`
is the default).  It cannot see the card, so it would read the bitstream
through a running core over the 86/76/75/13 link and stage it in its own
in-package flash; the choice would live on the card and be re-applied at
every power-up, so it survives power-off at the cost of a double boot.  The
reason not to build it: **it replaces the firmware of the board's only
programmer.**  `openFPGALoader` stops working unless the new firmware also
emulates the FT2232 - upstream MiSTeryNano has an `ft2232d_emulator`, which
`mnano/CMakeLists.txt` still globs for and which is **not in this tree or
any sibling** - and a bad flash of that chip leaves nothing able to
configure the FPGA until it is recovered through its BOOT pin.  That trades
a board needing a power cycle for a board that might not be programmable.

**E** is strictly worse than both B and D: four wires into TP3..TP6, and it
must share TCK/TMS/TDI with the on-board BL616, whose idle behaviour on
those pins nobody has measured.

**Not an option:** SRAM without persistence.  A core loaded into SRAM is
gone at power-off, and `.claude/rules/guideline.md` makes "the choice
remembered across power cycles" part of the goal.  Any SRAM design has to
re-apply the choice at power-up (D and E do) or write the flash anyway, at
which point it is route A with extra steps.

**Recommendation, if the power cycle is to go:** one wire, then C.  If it is
not, A works, is committed, and is the optimum for zero wires.

## Where the three must agree

- `Makefile`'s `CORES` and `DEFAULT_CORE`;
- `mnano/ultima.c`'s `ultima_cores[]`, whose `dir` field names both the
  card directory and `/cores/<dir>.bin`;
- the files actually on the card.

The order no longer matters - there is no ring to walk - but a name that
does not match means the OSD reports a missing file, which is at least a
harmless failure.
