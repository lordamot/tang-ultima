# The on-board BL616: reflashing the board's own programmer

Written 13 September 2026, when the Makefile targets were prepared.
**Nothing in this file has been run on the board yet** except `make
onboard-status` and `make onboard-fetch`; every other claim is upstream's
documentation, the SDK's headers and the flash tool's own manual, and
the section "What was seen" at the end is where board results go.

## What the chip is

The Tang Nano 20K carries its own BL616 (a BL616C, in-package flash),
behind the USB-C socket.  It is a different chip from the M0S Dock's
BL616 that runs `mnano/`.  From the schematic (v1.3, see
`coreswitch.md`) it reaches the FPGA on:

```
JTAG    TMS/TCK/TDI/TDO   FPGA pins 5/6/7/8   BL616 GPIO 16/10/12/14
UART                      FPGA pins 69/70     BL616 GPIO 11 (its TX) / 13 (its RX)
SPI     CS/DAT/DIR/SCLK   FPGA pins 86/76/75/13   BL616 GPIO 0 CS, 1 SCK, 2 MISO, 3 MOSI
                          (which of 76/75 is MISO, and which of 69/70 is which: the schematic)
and nothing else - no SD card, no MSPI flash
```

Its factory firmware, `20K's FRIEND`, emulates an FTDI FT2232 (USB
`0403:6010`, manufacturer `SIPEED`), which is what openFPGALoader and
the Gowin programmer talk to.  **That firmware is the only way this
board's FPGA is programmed from a PC**, and it is the thing the targets
below overwrite.  Why one would: the chip owns the FPGA's JTAG, and a
firmware of ours on it could load a core into the FPGA's SRAM in
seconds instead of the 20 s flash write plus power cycle - route D of
`coreswitch.md`.  This file is step zero of that route: getting a
firmware onto the chip and back off it, safely.

## The three faces of the chip

What `make onboard-status` (`tools/onboard.sh`) tells apart:

| USB | strings | meaning |
|---|---|---|
| `0403:6010` | `SIPEED` / `20K's FRIEND` | factory firmware; openFPGALoader works |
| `0403:6010` | anything else (upstream: `SIPEED USB Debugger`) | Sipeed's **FPGA Partner** - FT2232 emulation that also launches a second stage; openFPGALoader works |
| `349b:6160` | `Bouffalo` / `Bouffalo CDC DEMO`, a `/dev/ttyACMn` | the **boot ROM's ISP**: UPDATE was held at power-up, *or* the image at address 0 does not run on this chip |
| nothing | | a firmware that is a USB host, not a device (a companion build), or unplugged |

The M0S Dock's chip shows the same `349b:6160` in boot mode and nothing
at all when running, so `onboard.sh check-boot` refuses when a CDC DEMO
is on the bus together with the board's FT2232: that CDC DEMO is the
dock.  `make flash-mcu` is the dock's target; the `flash-mcu-onboard-*`
targets are this chip's.

**Boot mode**: hold **UPDATE** - the small button beside the HDMI
socket, not S1/S2, which are the FPGA's MODE pins - while plugging the
board in, release it once enumerated.  The FRIEND's two `ttyUSB` ports
vanish and a `ttyACM` appears.  After a flash: unplug, plug in without
the button.

## The flash map, and the encryption fuse

```
0x000000   the primary image - the boot ROM starts whatever is here
0x040000   the second stage the FPGA Partner starts when no PC is attached
           (upstream's fpga_companion_nano20k.bin goes here; ours would)
0x020000   an alternative layout: upstream's bl616_bootloader_0x20000_nano20k_signed.bin
           at 0 starts a firmware at 0x20000 UNCONDITIONALLY - a companion with
           no FT2232 at all, for fused chips.  Not fetched, not needed here.
```

The catch, from upstream's `Versions_TangNano20k` wiki page: Sipeed
began setting the BL616's **flash-encryption efuse** on boards sold from
about the start of 2024, to stop clones.  A fused chip runs only
encrypted images at address 0; an unfused one only plain ones.  Sipeed's
FPGA Partner exists only encrypted, so:

- **fused chip**: the Partner runs, a second stage at 0x40000 runs
  behind it, and the programmer and a custom firmware coexist.  Factory
  state is `friend_20k_encrypted_bl616.bin`.
- **unfused chip**: the Partner does not start - the chip sits in
  `Bouffalo CDC DEMO` until a plain image is written - and the
  programmer and a custom firmware are **mutually exclusive**: our
  firmware at address 0 means flashing the FRIEND back (a minute, no
  risk) every time the FPGA's flash is to be written from a PC.  Factory
  state is `friend_20k_bl616.bin`.

Upstream says the two "cannot be told apart beforehand - try it".  They
can: efuse word 0, bits [1:0], is `ef_sf_aes_mode` (0 none, 1 AES128, 2
AES256, 3 AES192) in the SDK's own
`drivers/soc/bl616/std/include/hardware/ef_data_reg.h`, and
`BLFlashCommand --efuse --read` reads it (the tool's manual, §5.4).
`make onboard-efuse` does that and `tools/efuse_bl616.py` decodes it.
That the ROM's rule is exactly "aes_mode set = encrypted only, clear =
plain only" is the wiki's observed behaviour joined to the field's
name, not a datasheet quote; the Partner flash is the confirmation.

**This board**, read 13 September 2026: **fused** - `EF_CFG_0 =
0x431`, AES128 - although the FRIEND's serial string `2023030621` reads
as a 2023 build, so that string says nothing about the fuse.  The
FPGA Partner runs on it (seen).  The oldest, "unmarked" boards also
have a capacitor C51 on the SPI MISO line that blocks the BL616-FPGA
SPI link for the companion firmware (upstream's `rework_c51` note);
whether this board is one of those is not known - a fused chip argues
against it, since fusing came later than the C51 boards.

## The targets

```
make onboard-status          what is on the bus and what it means (no boot mode needed)
make onboard-fetch           bin/onboard/*.bin from upstream, sha256-checked (no board needed)
make onboard-backup          the chip's first 1 MB -> bin/onboard/backup/bl616-<date>.bin
make onboard-efuse           the efuse -> fused / not fused
make flash-mcu-onboard-orig            friend_20k (plain)      at 0, section erase
make flash-mcu-onboard-orig-encrypted  friend_20k_encrypted    at 0, chip erase (as upstream)
make flash-mcu-onboard-ftdi            FPGA Partner            at 0, section erase
make flash-mcu-onboard-stage2          STAGE2=<file>           at 0x40000, section erase
make flash-mcu-onboard-restore BACKUP=bin/onboard/backup/<file>   at 0, section erase
```

All but the first two need boot mode.  Each write goes through the same
checks: the tool exists, exactly one CDC DEMO is on the bus and it is
not the dock, the port is writable, and the file starts `BFNP` with
`FCFG` at 8 (every BL616 boot image does - `bin/bl616.bin` included).
The `.ini` written to `build/onboard/write.ini` is upstream's, with the
file and address filled in.  "Section erase" (`erase = 1`) clears only
the range the file covers, so a Partner write leaves 0x40000 alone and a
stage-2 write leaves address 0 alone.

The images (`bin/onboard/`, pinned to FPGA-Companion commit `49ebb11`
and release `v1.4.29`, sha256 in the Makefile):

```
friend_20k_bl616.bin             89 024  factory, plain
friend_20k_encrypted_bl616.bin   81 872  factory, encrypted
bl616_fpga_partner_20kNano.bin   90 672  Sipeed's Partner (encrypted); identical
                                         to the release's bl616_fpga_partner_nano20k.bin
fpga_companion_nano20k.bin      666 528  upstream's second stage, v1.4.29
```

## The order to do things in

1. `make onboard-status` - the FRIEND is there.  (Seen.)
2. Boot mode, then **`make onboard-backup`** - this board's own factory
   image, the right variant by construction, and the one restore that
   is certainly right.  Commit it.
3. **`make onboard-efuse`** - fused or not.  This decides everything
   after it; write the answer into "What was seen" below.
4. Fused: `make flash-mcu-onboard-ftdi`, replug, `make onboard-status`
   should show an FT2232 again, and `openFPGALoader -b tangnano20k
   --detect` should still find the FPGA.  Then `make
   flash-mcu-onboard-stage2` with upstream's companion, to see the
   0x40000 launch happen with a known-good second stage before a
   firmware from here goes there.
   Unfused: the Partner is off the table.  `flash-mcu-onboard-ftdi`
   would only park the chip in CDC DEMO until `flash-mcu-onboard-orig`
   (which is the safe, reversible way to *confirm* the fuse reading, if
   wanted).  A firmware of ours then goes to address 0 as the primary,
   and the FRIEND goes back whenever the PC needs the FPGA.
5. Only then an internal build of `mnano/` - see the last section.

## Recovery

What cannot happen: the chip cannot be bricked by anything written to
its flash.  The ISP these targets talk to is in the BL616's mask ROM,
UPDATE-at-power-up always reaches it, and the FPGA boots from its own
flash whatever state the BL616 is in - the machine keeps working, only
PC-side programming is lost until the FRIEND is back.  Nothing here
writes efuses (`--efusefile` is never passed), and efuses are the only
thing that is not reversible.

| symptom | cause | do |
|---|---|---|
| after a flash and replug the board is still `Bouffalo CDC DEMO`, no button held | the image at 0 does not run on this chip - wrong encryption variant, or a bad write | it is already in ISP mode: `make flash-mcu-onboard-restore BACKUP=...`, or `-orig` / `-orig-encrypted`, the other one if the first does not take |
| the board enumerates as nothing at all | a firmware that is a USB host (a companion build at 0), or no power | hold UPDATE while plugging in, then restore as above |
| `BFLB IMG LOAD HANDSHAKE FAIL` | the port opened and nothing answered - not in boot mode, or the wrong port | replug with UPDATE held; `make onboard-status` shows the port |
| FRIEND is back but the FPGA does not come up | the FPGA's own flash - an interrupted install, unrelated to this chip | `make flash-image`, the usual recovery |
| our firmware is on the chip and the FPGA needs a PC write (unfused board) | by design | `flash-mcu-onboard-orig`, `make flash-image`, our firmware back |
| the chip is really dead (no CDC DEMO with UPDATE held, on a known-good cable and port) | hardware | the FPGA still boots from its flash; program it through TP3..TP6 (TCK/TDO/TDI/TMS, TP2 GND, `coreswitch.md`) with any cable openFPGALoader knows - an FT232H/FT2232H board - and the OSD's own install from the card still works |

Two things to keep the plan honest: the first flash is the one that
tells whether `onboard-backup` and `onboard-efuse` behave as the manual
says - they are untried here - and `-orig-encrypted` uses upstream's
chip erase, which also clears whatever else the chip's flash holds (a
manufacturing partition, if this chip has one like the dock's at
0x210000); the backup covers only the first 1 MB, `BACKUP_LEN=0x400000`
takes all of a 4 MB part if that matters.

## Stage 2 as it is (`onboard/`)

```
stage.h    the chip's flash: LOG sector at 0x0FE000, STAGE at 0x100000 -
           a descriptor sector (magic TUL1, length, crc32, idcode, name)
           and the packed bitstream; tools/mkstage.py writes that form
jtag.c     bit-banged on GPIO 10/12/14/16 (TCK/TDI/TDO/TMS), straight
           register writes, JTAG_NOPS paces TCK
gowin.c    openFPGALoader's SRAM sequence, routine for routine: idcode,
           status, eraseSRAM (force_state on CRC_ERROR, CONFIG_ENABLE,
           ERASE_SRAM, poll MEMORY_ERASE, XFER_DONE, CONFIG_DISABLE),
           writeSRAM (CONFIG_ENABLE, INIT_ADDR, XFER_WRITE, the bytes MSB
           first in one DR shift, 0x0a + 32 zero bits + 0x08,
           CONFIG_DISABLE, NOOP), DONE_FINAL in the status
main.c     erase the LOG, log the flash, check the descriptor and the crc,
           settle 3 s, idcode, erase, load, log every step, stop
```

Every step logs a (tag, value) pair into the LOG sector so a run with no
PC attached can still be read afterwards (`make onboard-log`).  The
`.bin`'s bytes go out MSB first: that is openFPGALoader's
`FsParser(reverseByte = MEM_MODE)` plus its LSB-first shift, and the
packed `.bin` is the `.fs` bits in order.  The checksum sent through
0x0a is zero - on the GW2A openFPGALoader does not verify it either.

## The switch through stage 2 (built 13 Sep evening; WORKS, seen that night)

```
dock (mnano/coreload.c)  --SPI CMD 11-->  FPGA (mister/coreload.v)  --UART 2 Mbaud-->  stage 2 (onboard/main.c)
   ultima_switch():                         2 KB TX FIFO -> pin 69        GPIO 13 RX: P/B/data/crc/L/R/S
   cl_ping, cl_send, cl_load,               pin 70 -> {count, last}       GPIO 11 TX: one-byte answers
   sys_reset_mcu()                          status {room,count} 1 byte    flash slot 0x100000, then the JTAG load
```

Why one-byte answers: `build/sim/tb2.v` runs the real mcu_spi -> sysctrl
-> flashwr chain under an SPI master model and shows a pointer-style read
of N bytes returns byte 0 twice (the byte set at strobe k is what byte
k+1 carries).  flashwr's install still passed its verify on the board,
which this does not explain; rather than depend on it, coreload.v only
ever answers with a byte that is set at the sub-command and repeated on
every strobe - fw_busy's shape, proven.  `build/sim/tb3.v` loops the
UART back: 8 bytes out clean, 8 counted, last right, flush works.

Timing budget per switch: erase ~2 s + 222 chunks x ~30 ms + load 1.1 s
+ the MCU's restart: about 10-12 s.  The first test stuck on
"Connecting"; the evening's diagnosis and the working result are in
`progress.md` ("The switch works").  Stage 2 v3 logs the link as well as
the load: `LOG_PINS` (GPIO 13 and 11 read as inputs before the UART
takes them - `3` is both idle high), `LOG_RX`/`LOG_TX` (the first 32
bytes each way, `ms << 8 | byte`), `LOG_READY` (ms since start when it
began listening); the log is erased at power-up and kept through every
load of that power-up.

Where the answer to "no answer" lives now: the OSD's second line is
`CMD 11: FIFO never has room (st xx)` when the FPGA's own side never
reports room - the core has no CMD 11, or the link into the FPGA is
broken - and `no answer from stage 2 (st xx)` when it does and nothing
comes back - on a PC, or no stage 2, or the UART; `st` is the raw
status byte, `80` being room and nothing received.  Then `make
onboard-log`.

## What an internal build of mnano/ will have to answer

Not done, not started; noted so the pins are not re-derived.  `mnano/`
already builds "for internal" when `M0S_DOCK` is not defined
(`spi.c`, `main.c`), but its pinout is MiSTeryNano's old one, and
upstream has moved:

| signal | `mnano/spi.c` internal | FPGA-Companion `nano20k` (3921) | FPGA pin |
|---|---|---|---|
| SPI CSN | GPIO 0 | GPIO 0 | 86 (SPI_CS) |
| SPI SCK | GPIO 1 | GPIO 1 | 13 (SPI_SCLK) |
| SPI MISO | **GPIO 10 (= JTAG TCK)**, "GPIO 2 filtered on the TN20k" | GPIO 2 | 76 or 75 (SPI_DAT/DIR - which is which: the schematic) / 6 |
| SPI MOSI | GPIO 3 | GPIO 3 | the other of 76/75 |
| IRQ | **GPIO 12 (= JTAG TDI)** | GPIO 13 (= UART RX) | 7 / one of 69, 70 |
| console TX | - | GPIO 11 (= UART TX) | the other of 69, 70 |
| JTAG | given up for MISO/IRQ | TCK/TDI/TDO/TMS on 10/12/14/16, kept | 6/7/8/5 |

(The FPGA pins are `coreswitch.md`'s schematic reading; the SPI_DAT /
SPI_DIR and UART directions were not resolved there and are not here.)

The old one routes around C51 by taking JTAG pins, which an SRAM loader
cannot give up; the new one keeps JTAG and needs C51 gone on the oldest
boards.  Whichever is chosen, the FPGA side of every core has to put
the link on those pins (the siblings' `tang/` constraints), which is
their change, and the 3923 revision moves MISO/MOSI again (GPIO 30/27).
`mnano/CMakeLists.txt` also globs `ft2232d_emulator/*.c`, which no tree
here has - the Partner makes it unnecessary on a fused chip.

## Sources

- MiSTle-Dev/FPGA-Companion `src/bl616/README.md`, `friend_20k/README.md`,
  `bl616_fpga_partner/README.md`, `buildall.sh`, `flash.ini` (commit
  `49ebb11`, 12 Sep 2026) and release v1.4.29.
- MiSTle-Dev/.github wiki: `Versions_TangNano20k`,
  `Firmware-Installation-BL616-µC`.
- `tools/bouffalo_sdk/tools/bflb_tools/bouffalo_flash_cube/docs/FlashCube_User_Guide.pdf`
  §5.4 for `--flash --read` and `--efuse --read`.
- `tools/bouffalo_sdk/drivers/soc/bl616/std/include/hardware/ef_data_reg.h`
  for the efuse bits.

## What was seen

- 13 Sep 2026: `onboard-status` - `0403:6010 SIPEED 20K's FRIEND
  2023030621`.  `onboard-fetch` - four files, sums as recorded.
- 13 Sep 2026, 18:35: boot mode reached as described (UPDATE held while
  plugging in; CDC DEMO on `/dev/ttyACM0`).  **`onboard-backup` works**:
  1 MB in 2.6 s, `bin/onboard/backup/bl616-20260913-183535.bin`, a BFNP
  image, content ending at 0x14040 (~82 KB - the encrypted variant's
  size, not the plain one's), 0x20000 and 0x40000 blank.  It matched
  none of upstream's files byte for byte (52 888 of 81 872 bytes differ
  from `friend_20k_encrypted`), so it IS the only exact restore of this
  board.  **`onboard-efuse` works**: `EF_CFG_0 = 0x00000431` -
  `ef_sf_aes_mode = 1` (AES128), `ef_sboot_en = 3`: **FUSED**, despite
  the 2023 serial string.  Chip id / MAC `b4c2e0b31f99`.
- 13 Sep 2026, 18:36: **`flash-mcu-onboard-ftdi` written and running.**
  The tool's XIP SHA256 over the flash equals the file's
  (`bc5b1f73…3203`).  After a replug without the button:
  `0403:6010 SIPEED "USB Debugger" 2025030317`, two `ttyUSB`, and
  `openFPGALoader -b tangnano20k --detect` finds `idcode 0x81b
  GW2A(R)-18(C)`.  The programmer survived; 0x40000 is empty and
  available.
- 13 Sep 2026, 18:50: **stage 2 of our own (`onboard/`) at 0x40000 and
  the UKNC staged at 0x100000**, both verified by the chip's SHA256.
  Board moved to a power bank with the Korvet in the FPGA's flash:
  **screen black, then the UKNC** (the Korvet was configured - the log's
  status-before says DONE_FINAL - but the monitor had not locked before
  the erase).  `make onboard-log` afterwards:

  ```
  01 20260913 start     02 001660c8 jedec (GD25Q32)   03 00400000 4 MB
  04 0 descriptor ok    05 000dd89a length            06 0 crc ok
  07 0000081b idcode    08 00006020 status before: DONE_FINAL
  09 0 erase ok         0a 00000020 after erase: MEMORY_ERASE
  0b 00006020 after load: DONE_FINAL   0c 1   0d 00000479 = 1145 ms   0e 0
  ```

  **The route works: a core staged in the on-board chip is loaded into
  the FPGA's SRAM over its JTAG in 1.1 s, no wires, the FPGA's flash
  untouched.**  A flash-to-SRAM switch on this board is therefore
  Partner boot + 3 s settle + 1.1 s.
