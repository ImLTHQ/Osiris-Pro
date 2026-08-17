#!/usr/bin/env python3
"""One-command generator: extract CS2 menu icons from the game VPK and build the preview gallery.

Usage:
  python3 icon_gallery/generate.py                 # auto-detect game installation
  python3 icon_gallery/generate.py <game_dir>      # explicit game dir (…/Counter-Strike Global Offensive)

Outputs (git-ignored):
  icon_gallery/raw/       raw VPK blobs (binary header + plain svg tail)
  icon_gallery/icons/     extracted .svg files shown in the gallery
  icon_gallery/gallery.html   browsable preview page
"""
import os
import shutil
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
sys.path.insert(0, SCRIPT_DIR)

import vpk_extract

RAW_DIR = os.path.join(SCRIPT_DIR, "raw")
ICONS_DIR = os.path.join(SCRIPT_DIR, "icons")
GALLERY_HTML = os.path.join(SCRIPT_DIR, "gallery.html")

# prefixes extracted from the csgo VPK (see vpk_extract.list_paths)
PREFIXES = [b"panorama/images/icons", b"panorama/images/hud"]

COMMON = "steamapps/common/Counter-Strike Global Offensive"


def find_game_dir():
    roots = []
    for pfx in [r"C:\Program Files (x86)\Steam", r"C:\Program Files\Steam",
                r"D:\SteamLibrary", r"E:\SteamLibrary", r"F:\SteamLibrary",
                "/mnt/c/Program Files (x86)/Steam", "/mnt/c/Program Files/Steam",
                "/mnt/d/SteamLibrary", "/mnt/e/SteamLibrary", "/mnt/f/SteamLibrary"]:
        roots.append(os.path.join(pfx, COMMON))
    for p in roots:
        if os.path.isfile(os.path.join(p, "game", "csgo", "pak01_dir.vpk")):
            return p
    return None


def extract_svgs(dir_vpk, raw_dir, icons_dir):
    tree, tree_end, _ = vpk_extract.read_dir(dir_vpk)
    base = os.path.dirname(dir_vpk)

    entries = []
    for pfx in PREFIXES:
        for path in vpk_extract.list_paths(tree, pfx):
            for name, archive, offset, length in vpk_extract.walk_group(tree, path):
                entries.append((path + b"/" + name, archive, offset, length))

    raw_count = svg_count = 0
    seen = set()
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
        rel = full.decode()
        raw_target = os.path.join(raw_dir, rel)
        os.makedirs(os.path.dirname(raw_target), exist_ok=True)
        with open(raw_target, "wb") as f:
            f.write(blob)
        raw_count += 1
        # CS2 stores the svg as "binary header + plain svg tail"; take the last <svg>…</svg>
        i = blob.rfind(b"<svg")
        if i < 0:
            continue
        tail = blob[i:]
        if not tail.rstrip().endswith(b"</svg>"):
            continue
        svg_target = os.path.join(icons_dir, rel + ".svg")
        os.makedirs(os.path.dirname(svg_target), exist_ok=True)
        with open(svg_target, "wb") as f:
            f.write(tail)
        svg_count += 1
    return raw_count, svg_count


def main():
    game_dir = sys.argv[1] if len(sys.argv) > 1 else find_game_dir()
    if not game_dir:
        sys.exit("未找到 CS2 安装目录，请手动指定: python3 icon_gallery/generate.py <游戏目录>")
    dir_vpk = os.path.join(game_dir, "game", "csgo", "pak01_dir.vpk")
    if not os.path.isfile(dir_vpk):
        sys.exit(f"找不到 {dir_vpk}")

    print(f"游戏目录: {game_dir}")
    for d in (RAW_DIR, ICONS_DIR):
        if os.path.isdir(d):
            shutil.rmtree(d)

    raw_count, svg_count = extract_svgs(dir_vpk, RAW_DIR, ICONS_DIR)
    print(f"提取 {raw_count} 个文件, 其中 {svg_count} 个有效 SVG")

    os.chdir(REPO_ROOT)
    import make_gallery
    make_gallery.main()
    print(f"完成! 用浏览器打开: {GALLERY_HTML}")


if __name__ == "__main__":
    main()
