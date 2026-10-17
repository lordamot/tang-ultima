#!/usr/bin/env python3
"""Pack an existing Gowin .bin for RAM-only switching; never write a device."""
import argparse
import lzma
from pathlib import Path
import struct
import zlib

CAPACITY = 360 * 1024
DICT_SIZE = 4096
MAGIC = b'URM1'


def pack(data):
    if not 0x10000 <= len(data) <= 0x100000:
        raise ValueError('bitstream must be between 64 KiB and 1 MiB')
    if data[0x16:0x18] != b'\xa5\xc3' or data[0x1c:0x20] != b'\0\0\x08\x1b':
        raise ValueError('not a bitstream for GW2AR-18')
    payload = lzma.compress(data, check=lzma.CHECK_CRC32,
                          filters=[{'id': lzma.FILTER_LZMA2, 'dict_size': DICT_SIZE, 'preset': 9}])
    if len(payload) > CAPACITY:
        raise ValueError(f'compressed core needs {len(payload)} bytes; RAM limit is {CAPACITY}')
    return struct.pack('<4sIII', MAGIC, len(data), zlib.crc32(data), len(payload)) + payload


def pack_file(source, target):
    blob = pack(Path(source).read_bytes())
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(blob)
    return len(blob) - 16


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    args = parser.parse_args()
    size = pack_file(args.source, args.target)
    print(f'{args.source.name}: {size} / {CAPACITY} bytes of staging RAM')
