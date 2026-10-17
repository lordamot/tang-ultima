#!/usr/bin/env python3
"""Exercise production RAM staging, validation and JTAG output on the host."""
import ctypes as C
import lzma
import os
from pathlib import Path
import re
import struct
import subprocess
import zlib
from pack_ramcore import pack, CAPACITY

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/ramstage-test'
XZ = ROOT / 'tools/bouffalo_sdk/components/lzma/xz'
OUT.mkdir(parents=True, exist_ok=True)
SANITIZE = ['-fsanitize=undefined', '-fno-sanitize-recover=all'] if os.getenv('RAMSTAGE_UBSAN') else []
subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                '-DXZ_DEC_PREALLOC', '-Itests/ramstage_stubs', '-Ionboard', f'-I{XZ}',
                'tests/ramstage_host.c', 'onboard/ramstage.c',
                *[str(XZ / f) for f in ('xz_crc32.c', 'xz_dec_lzma2.c', 'xz_dec_stream.c')],
                *SANITIZE, '-o', str(OUT / 'stage2.so')], cwd=ROOT, check=True)
lib = C.CDLL(str(OUT / 'stage2.so'))
lib.test_begin.argtypes = [C.c_char_p, C.c_uint32]
lib.test_output.restype = C.c_void_p


def wire(payload, raw_len, raw_crc):
    return (struct.pack('<I16sII', len(payload), b'test', raw_len, raw_crc) +
            payload + struct.pack('<I', zlib.crc32(payload)))


def begin(data):
    lib.test_begin(data, len(data))
    return lib.test_reply()


def refused(data, expected=None):
    lib.test_reset()
    ret = begin(data)
    assert ret != 0 and (expected is None or ret == expected), ret
    lib.test_load()
    assert lib.test_reply() != 0 and lib.test_erases() == 0


for name in ('uknc', 'pk8000', 'korvet', 'zs256', 'bk', 'evo'):
    raw = (ROOT / f'bin/{name}.bin').read_bytes()
    container = pack(raw)
    magic, length, crc, packed_len = struct.unpack('<4sIII', container[:16])
    assert magic == b'URM1' and packed_len <= CAPACITY
    payload = container[16:]
    frame = wire(payload, length, crc)
    (OUT / f'{name}.bin.ram').write_bytes(container)
    lib.test_reset()
    assert begin(frame) == 0
    assert lib.test_reply_count() == 2 + (packed_len + 4095) // 4096
    lib.test_load()
    assert lib.test_reply() == 0 and lib.test_erases() == 1
    assert lib.test_reloads() == 0 and lib.test_last_chunks() == 1
    assert lib.test_output_size() == len(raw)
    assert C.string_at(lib.test_output(), len(raw)) == raw
    # Repeated switching must reuse its decoder arena, never exhaust it.
    assert begin(frame) == 0
    lib.test_load()
    assert lib.test_erases() == 2
    # Corruption after reception must be caught before FPGA SRAM is erased.
    lib.test_reset()
    assert begin(frame) == 0
    lib.test_corrupt()
    lib.test_load()
    assert lib.test_reply() != 0 and lib.test_erases() == 0
    for cut in (0, 10, 28, 29, len(frame) // 2, len(frame) - 1):
        refused(frame[:cut], 8)
    bad_crc = bytearray(frame); bad_crc[-1] ^= 1
    refused(bytes(bad_crc), 2)
    corrupt = bytearray(payload); corrupt[len(corrupt) // 2] ^= 1
    refused(wire(bytes(corrupt), length, crc), 2)
    refused(wire(payload, length - 1, crc), 2)
    refused(wire(payload, length, crc ^ 1), 2)
    refused(wire(payload + b'\0', length, crc), 2)
    # Enforce the 4 KiB dictionary even for otherwise valid XZ.
    bigdict = lzma.compress(raw, check=lzma.CHECK_CRC32,
                           filters=[{'id': lzma.FILTER_LZMA2, 'dict_size': 8192}])
    refused(wire(bigdict, length, crc), 2)
    wrong = bytearray(raw); wrong[0x1f] ^= 1
    bad_device = lzma.compress(wrong, check=lzma.CHECK_CRC32,
                             filters=[{'id': lzma.FILTER_LZMA2, 'dict_size': 4096}])
    refused(wire(bad_device, length, zlib.crc32(wrong)), 2)
    for erase_error, load_error in ((1, 0), (0, 1)):
        lib.test_reset(); assert begin(frame) == 0
        lib.test_fail(erase_error, load_error); lib.test_load()
        assert lib.test_reloads() == 1
    # Failed replacement must invalidate an earlier successful transfer.
    lib.test_reset(); assert begin(frame) == 0
    assert begin(frame[:30]) == 8
    lib.test_load(); assert lib.test_erases() == 0
    print(f'{name}: {packed_len}/{CAPACITY} bytes; exact JTAG output, failures refused')

refused(struct.pack('<I16sII', CAPACITY + 1, b'oversize', 907418, 0), 1)
refused(struct.pack('<I16sII', 1, b'oversize', 0x100001, 0), 1)
assert (ROOT / 'onboard/protocol.h').read_bytes() == (ROOT / 'mnano/coreload_proto.h').read_bytes()
# Build-independent source guard complements the host link's absence of flash APIs.
assert not re.search(r'bflb_flash_(?:write|erase)\s*\(', (ROOT / 'onboard/main.c').read_text())
# Companion protocol: verify capability refusal, exact framing, and SD failures.
subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-DSDL', '-Imnano',
                '-Itools/bouffalo_sdk/components/fs/fatfs', 'tests/coreload_host.c',
                *SANITIZE, '-o', str(OUT / 'dock.so')], cwd=ROOT, check=True)
dock = C.CDLL(str(OUT / 'dock.so'))
dock.dock_reset.argtypes = [C.c_char_p, C.c_uint, C.c_int]
dock.dock_wire.restype = C.c_void_p
for name in ('uknc', 'pk8000', 'korvet', 'zs256', 'bk', 'evo'):
    container = (OUT / f'{name}.bin.ram').read_bytes()
    _, rawlen, rawcrc, size = struct.unpack('<4sIII', container[:16])
    dock.dock_reset(container, len(container), 1)
    assert dock.dock_ping() == 0 and dock.dock_claimed() == 1
    assert dock.dock_send() == 0 and dock.dock_closed() == 1
    assert dock.dock_load() == 0
    actual = C.string_at(dock.dock_wire(), dock.dock_wire_size())
    assert actual == b'PVC' + wire(container[16:], rawlen, rawcrc) + b'L'
    # Feed the real dock's bytes into the real stage-2 command handler.
    lib.test_reset(); assert begin(actual[3:-1]) == 0
    lib.test_load(); assert lib.test_erases() == 1
    # Existing flash-writing stage 2 must never receive a BEGIN command.
    dock.dock_reset(container, len(container), 0)
    assert dock.dock_ping() == -28
    assert C.string_at(dock.dock_wire(), dock.dock_wire_size()) == b'PV'
    dock.dock_release(); assert dock.dock_claimed() == 0
    for missing, shortread, expected in ((1, 0, -29), (0, 1, -24)):
        dock.dock_reset(container, len(container), 1)
        dock.dock_fail(missing, shortread)
        assert dock.dock_ping() == 0
        assert dock.dock_send() == expected
        assert dock.dock_wire_size() == 2
print('RAM-only loader and companion host checks passed; no board validation implied')
