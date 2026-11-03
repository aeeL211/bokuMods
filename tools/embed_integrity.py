#!/usr/bin/env python3
"""Embed the integrity hash of the read-only segments into the built library.

The runtime hashes every non-writable PT_LOAD segment at startup and compares
it with the value written here. The value lives in the writable data segment,
so patching it does not change the hash it describes, and any edit to the code
or to the encrypted target blob invalidates the check.
"""

import struct
import sys

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MARKER = b"BOKUINTEGRITYv1"
MARKER_SIZE = 16
PT_LOAD = 1
PF_W = 2


def fnv1a64(data, value=FNV_OFFSET):
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def patch(path):
    data = bytearray(open(path, "rb").read())
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit(f"{path}: not a little-endian ELF64 file")

    e_machine = struct.unpack_from("<H", data, 0x12)[0]
    if e_machine != 183:
        raise SystemExit(f"{path}: not an AArch64 library")
    e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x36)[0]
    e_phnum = struct.unpack_from("<H", data, 0x38)[0]

    value = FNV_OFFSET
    for i in range(e_phnum):
        offset = e_phoff + i * e_phentsize
        p_type, p_flags, p_offset, _p_vaddr, _p_paddr, p_filesz, _p_memsz, _p_align = (
            struct.unpack_from("<IIQQQQQQ", data, offset)
        )
        if p_type == PT_LOAD and not (p_flags & PF_W):
            value = fnv1a64(data[p_offset : p_offset + p_filesz], value)

    if value == 0:
        raise SystemExit(f"{path}: computed integrity hash is zero")

    positions = []
    start = 0
    while True:
        found = data.find(MARKER, start)
        if found < 0:
            break
        positions.append(found)
        start = found + 1
    if len(positions) != 1:
        raise SystemExit(f"{path}: expected one integrity marker, found {len(positions)}")
    patch_at = positions[0] + MARKER_SIZE
    if patch_at + 8 > len(data):
        raise SystemExit(f"{path}: integrity marker is truncated")
    struct.pack_into("<Q", data, patch_at, value)
    open(path, "wb").write(data)
    print(f"integrity hash 0x{value:016x}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: embed_integrity.py <library.so>")
    patch(sys.argv[1])
