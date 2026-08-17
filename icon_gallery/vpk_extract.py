#!/usr/bin/env python3
"""Extract icon files from CS2 pak01_dir.vpk (VPK v2, CS2 tree layout).

CS2 tree layout (reverse-engineered from the binary):
  [EXT]\0 [PATH]\0  (NAME\0 META18)+
where META18 = crc32(u32) preload(u16) archive_index(u16) offset(u32) length(u32) terminator(0xFFFF)
Group boundary: an empty string after a META ends the (NAME...)+ run.
Data lives in pak01_%03d.vpk at (offset, length); archive_index 0x7FFF = embedded in dir vpk.
"""
import os
import struct
import sys

MAGIC = 0x55AA1234
HEADER = struct.Struct("<IIIIIII")  # sig, ver, tree_size, fdata, amd5, omd5, sigsz
META = struct.Struct("<IHHIIH")


def read_dir(path):
    with open(path, "rb") as f:
        data = f.read()
    sig, ver, tree_size, fdata_size, *_ = HEADER.unpack(data[:28])
    assert sig == MAGIC, f"bad magic in {path}"
    assert ver == 2, f"unsupported version {ver}"
    tree_end = 28 + tree_size
    tree = data[28:tree_end]
    return tree, tree_end, fdata_size


def read_cstring(buf, pos):
    end = buf.find(b"\x00", pos)
    if end < 0:
        return None, pos
    return buf[pos:end], end + 1


def walk_group(tree, path_bytes):
    """Find group(s) whose PATH equals path_bytes; yield (name, archive, offset, length)."""
    needle = b"\x00" + path_bytes + b"\x00"
    start = 0
    while True:
        i = tree.find(needle, start)
        if i < 0:
            break
        pos = i + len(needle)
        while pos < len(tree):
            name, pos = read_cstring(tree, pos)
            if name is None or name == b"":
                break
            if pos + 18 > len(tree):
                break
            crc, preload, archive, offset, length, term = META.unpack(tree[pos:pos + 18])
            pos += 18
            if term != 0xFFFF:
                break
            if preload != 0:
                # preload data is inline in the dir file; skip (unexpected here)
                pos += preload
                continue
            yield name, archive, offset, length
        start = i + 1


def list_paths(tree, prefix_bytes):
    """Enumerate distinct PATH strings starting with prefix."""
    paths = set()
    needle = b"\x00" + prefix_bytes
    start = 0
    while True:
        i = tree.find(needle, start)
        if i < 0:
            break
        s, end = read_cstring(tree, i + 1)
        if s:
            paths.add(s)
        start = i + 1
    return sorted(paths)


def main():
    dir_vpk = sys.argv[1]
    out_root = sys.argv[2]
    prefixes = [p.encode() for p in sys.argv[3:]]

    tree, tree_end, fdata_size = read_dir(dir_vpk)
    print(f"tree size {len(tree)}, embedded data at {tree_end} ({fdata_size} bytes)")

    if not prefixes:
        # list only
        for pfx in (b"panorama/images/icons", b"panorama/images/hud"):
            for p in list_paths(tree, pfx):
                print(p.decode())
        return

    base = os.path.dirname(dir_vpk)
    entries = []
    for pfx in prefixes:
        for p in list_paths(tree, pfx):
            for name, archive, offset, length in walk_group(tree, p):
                entries.append((p + b"/" + name, archive, offset, length))

    seen = set()
    extracted = 0
    for full, archive, offset, length in entries:
        if full in seen:
            continue
        seen.add(full)
        if archive == 0x7FFF:
            with open(dir_vpk, "rb") as f:
                f.seek(tree_end + offset)
                blob = f.read(length)
        else:
            chunk = os.path.join(base, f"pak01_{archive:03d}.vpk")
            with open(chunk, "rb") as f:
                f.seek(offset)
                blob = f.read(length)
        target = os.path.join(out_root, full.decode())
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as f:
            f.write(blob)
        extracted += 1

    print(f"extracted {extracted} files to {out_root}")


if __name__ == "__main__":
    main()
