# Progress

## State, 12 September 2026

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

What has NOT been verified - nothing has been on a board:

1. That RECONFIG_N driven low from user logic reloads this board's FPGA
   from the header's address.  UG290 says so; the on-board BL616 is on
   the same net on some revisions.
2. That `openFPGALoader --file-type bin -o 0` writes the raw 3 MB image
   on the Tang Nano 20K (v1.1.1 has the option and the Gowin external
   flash path; fallback: three `flash-core-*` runs with replugs).
3. The timing of a switch: ~3 s a load at the default 2.5 MHz rate, plus
   the MCU's watchdog restart and USB enumeration.
4. That the card is readable again after a reload without any further
   care (`sdc_reattach` waits for sd_card.v's status and remounts).
5. Every core's own behaviour under this firmware rather than its own:
   the merge is by hand.  The host test says the forms and letters
   agree; it cannot say the keyboards do (the keymaps are copied
   verbatim).

## What is next

- The first flash: `make flash-image`, power-cycle, the UKNC comes up;
  F12, Core, Korvet; watch the console for the walk.  Then the same from
  a power-up with `/ultima.ini` saying korvet - two hops.
- If the switch works: try `LOADING_RATE`, since 3 s a hop is the whole
  of the wait.
- If it does not: the on-board BL616's hold on RECONFIG_N is the first
  suspect (a scope on pin 9 during CMD 9); the second is whether the
  jump address is applied on a RECONFIG_N pulse or only on a failed
  load on this device.
- Later: an "Install core" that writes a slot from a `.fs` on the card
  through the MSPI pins as GPIOs (`-use_mspi_as_gpio`), so a user never
  needs openFPGALoader after the first flash.  Not started.

## Defects

None known that a board has shown; none has been on one.
