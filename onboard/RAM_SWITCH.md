# RAM-only switching on the existing board

Status, 17 October 2026: both firmware images build and all six current
bitstreams pass host protocol/decoder checks. This path has not been
installed or validated on a board. Evo's separate video issue remains.

No extra components or wiring are needed. The dock reads a compressed core
from the existing FPGA-connected SD card, transfers it through CMD 11 and
the existing UART, and stage 2 holds it in internal SRAM. It validates the
whole decoded core before acknowledging completion and revalidates before
FPGA erasure. Once the FPGA and its SD/UART bridge disappear, stage 2
independently decodes the RAM buffer into the existing JTAG loader.

Normal switches do not erase or program either MCU flash or FPGA flash.
Diagnostics are RAM-only. Explicit Save to flash still writes FPGA boot
flash; firmware installation and the historical recovery targets also
still write flash when explicitly requested.

## RAM budget

The existing SDK linker script reclaims 128 KiB of unused Wi-Fi RAM when
Wi-Fi is absent: the main RAM region is 447 KiB (0x6fc00), rather than
319 KiB. No external PSRAM region is used. The new stage-2 link map leaves
0x3400 (13 KiB) between __HeapBase and __HeapLimit, with its 4 KiB stack
already reserved separately. Static allocations include:

- Compressed core: 360 KiB maximum.
- Bounded XZ decoder arena: 40 KiB, including its 4 KiB dictionary.
- Decoded output: 4 KiB.
- UART ring: 8 KiB, twice the acknowledged transfer chunk.
- Diagnostic records: 4 KiB.

Core payload sizes with Python's XZ/LZMA2 encoder, preset 9, 4 KiB dictionary
and CRC32: UKNC 361296; PK8000 243508; Korvet 300000; ZS-256 249584;
BK 308868; Evo 298128 bytes. The limit is 368640 bytes. Larger future
builds are refused by the packer and loader, never staged through flash.

## Files and compatibility

`make card-stage` creates `/cores/<name>.bin.ram` beside each existing
`.bin`, using the actual staged binary even when old assets are preserved.
Each sidecar is a 16-byte little-endian header (`URM1`, raw size, raw CRC32,
compressed size), followed by one XZ stream. Original `.bin` files remain
for Save to flash. To pack a custom core:

```
python3 tools/pack_ramcore.py bin/uknc.bin build/uknc.bin.ram
```

Both MCUs need the matching new firmware once, and the SD needs the sidecars.
Existing installed firmware cannot gain RAM-only switching without that
update. The new dock checks `V -> 0x51` before sending `C`; an old loader
is refused before it receives any bitstream. The new loader refuses the
old flash-staging `B` command. There is no automatic flash-writing fallback.

Build candidates: `build/fw/bl616.bin` (dock) and
`build/onboard/stage2.bin` (on-board MCU). Shipped `bin/bl616.bin` and all
sibling binaries have deliberately not been replaced: this is an untested
candidate, not a synchronized release.

The old `make onboard-log` reads the historical flash sector, not current
RAM diagnostics; its contents may be stale. RAM diagnostics do not survive
power-off or entry into ISP. They are also printed on stage 2's debug UART.

## Verification and next board check

```
make ramstage-test
RAMSTAGE_UBSAN=1 make ramstage-test
make onboard-fw
make fw
make card-stage
make menu-test
```

Host checks run the actual dock transport and stage-2 handlers/decoder with
mock SD/SPI/UART/JTAG endpoints. Each core produces byte-identical JTAG
output, including exactly one final chunk. Tests cover repeated loads,
truncation, transfer/decompressed CRC failures, wrong size/device header,
trailing data, oversized dictionaries/buffers, SD errors, old firmware
refusal, corruption after reception and JTAG-failure recovery. Host mocks
supply no flash-writing functions. UART electrical behavior, hardware RAM
availability, JTAG pauses during decoding, timing and real boot success
still require a board test. The dock already waits up to ten seconds for
valid FPGA status after reset; actual decode/load duration must fit that
window and still needs checking.

Before any future on-board firmware installation, follow the existing
backup/efuse/Partner preservation procedure in `.claude/docs/onboard.md`.
This change does not authorize or perform installation. Keep the original
FPGA boot image for the existing JTAG reload recovery path. Test on non-PC
power; check repeated switches and failed-transfer recovery, then re-enter
ISP and compare MCU flash against a post-install baseline to verify that
switching did not alter it. Do not describe the new path as board tested
until those observations exist.
