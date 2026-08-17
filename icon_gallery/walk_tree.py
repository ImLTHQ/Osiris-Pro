#!/usr/bin/env python3
"""Full CS2 VPK tree walker.

Format (reverse-engineered from pak01_dir.vpk):
  tree = EXT_SECTION*
  EXT_SECTION = EXT '\0' PATH '\0' (NAME '\0' META18)+
                ('\0' PATH '\0' (NAME '\0' META18)+)*
  boundaries after a meta 0xFFFF:
    next byte != 0   -> next NAME of the same (ext, path) group
    '\0' + nonempty  -> new PATH group inside the same ext section
    '\0\0' + EXT     -> new ext section; if EXT == "" then a PATH follows;
                        if that PATH == "" too -> END OF TREE
  META18 = crc32(u32) preload(u16) archive(u16) offset(u32) length(u32) term(0xFFFF)
"""
import struct

META = struct.Struct("<IHHIIH")


def read_cstring(buf, pos):
    end = buf.find(b"\x00", pos)
    if end < 0:
        return None, len(buf)
    return buf[pos:end], end + 1


def walk_tree(tree):
    """Yield (ext, path, name, archive_index, offset, length)."""
    pos = 0
    ext, pos = read_cstring(tree, pos)
    if ext is None:
        return
    need_path = True
    path = b""
    while True:
        if need_path:
            path, pos = read_cstring(tree, pos)
            if path is None or path == b"":
                return
        while True:
            name, pos = read_cstring(tree, pos)
            if name is None or name == b"":
                break
            if pos + 18 > len(tree):
                raise ValueError(f"tree truncated near {name!r}")
            crc, preload, archive, offset, length, term = META.unpack(tree[pos:pos + 18])
            pos += 18
            if term != 0xFFFF:
                raise ValueError(f"bad terminator near {name!r} (ext={ext!r} path={path!r})")
            if preload:
                pos += preload
                continue
            yield ext, path, name, archive, offset, length
        s, pos = read_cstring(tree, pos)
        if s is None:
            return
        if s != b"":
            path = s
            need_path = False
            continue
        ext, pos = read_cstring(tree, pos)
        if ext is None:
            return
        if ext == b"":
            nxt, pos = read_cstring(tree, pos)
            if nxt is None or nxt == b"":
                return
            path = nxt
            need_path = False
        else:
            need_path = True


def main():
    import sys
    tree_path = sys.argv[1]
    with open(tree_path, "rb") as f:
        data = f.read()
    sig, ver, tree_size, *_ = struct.unpack("<IIIIIII", data[:28])
    tree = data[28:28 + tree_size]
    count = 0
    kw = sys.argv[2].encode() if len(sys.argv) > 2 else None
    for ext, path, name, archive, offset, length in walk_tree(tree):
        count += 1
        full = path + b"/" + name
        if ext:
            full += b"." + ext
        if kw is None or kw in full.lower() or kw in name.lower():
            print(f"{full.decode(errors='replace')}  [{archive} @ {offset}, {length}]")
    print(f"// total entries: {count}")


if __name__ == "__main__":
    main()
