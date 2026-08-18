#!/usr/bin/env python3
"""Generates WeaponPaintKitRarityTable.h from CS2's items_game.txt.

Usage:
    python3 generate_weapon_paint_kit_rarities.py <path/to/items_game.txt> [output_header.h]

The script extracts every "paint_kits" section (base + per-operation + items_game_live)
and every "paint_kits_rarity" section (in document order, later sections override earlier
entries, matching the game's merge semantics) and emits a flat constexpr table mapping a
paint kit id to its CS2 rarity.

Rarity values are taken from the "rarities" section of items_game.txt:
    default (0), common (1), uncommon (2), rare (3), mythical (4),
    legendary (5), ancient (6), immortal (7), unusual (99)
"""

import re
import sys

# Table values must match WeaponRarity enum in WeaponRarity.h
RARITY_VALUE = {
    "default": 0,
    "common": 1,
    "uncommon": 2,
    "rare": 3,
    "mythical": 4,
    "legendary": 5,
    "ancient": 6,
    "immortal": 7,
    "unusual": 8,
}

DEFAULT_RARITY = 0


def find_sections(key, text):
    """Finds every section named `key` (at any depth) and returns its body (without braces)."""
    result = []
    for match in re.finditer(r'(?m)^[ \t]*"' + re.escape(key) + r'"\s*$', text):
        brace = text.find("{", match.end())
        if brace == -1:
            continue
        depth = 0
        i = brace
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        result.append(text[brace + 1:i])
    return result


def parse_paint_kits(body):
    """Returns {paint_kit_id: paint_kit_name} for one paint_kits section body."""
    result = {}
    for match in re.finditer(r'(?m)^\s*"(\d+)"\s*\n\s*\{\s*\n\s*"name"\s*"([a-zA-Z0-9_]+)"', body):
        result[int(match.group(1))] = match.group(2)
    return result


def parse_paint_kits_rarity(body):
    """Returns {paint_kit_name: rarity_name} for one paint_kits_rarity section body."""
    result = {}
    for name, rarity in re.findall(r'(?m)^\s*"([a-zA-Z0-9_]+)"\s*"([a-z_]+)"\s*$', body):
        result[name] = rarity
    return result


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else "WeaponPaintKitRarityTable.h"

    with open(input_path, encoding="utf-8", errors="replace") as file:
        text = file.read()

    paint_kit_names = {}
    for body in find_sections("paint_kits", text):
        paint_kit_names.update(parse_paint_kits(body))

    name_to_rarity = {}
    for body in find_sections("paint_kits_rarity", text):
        name_to_rarity.update(parse_paint_kits_rarity(body))

    max_id = max(paint_kit_names) if paint_kit_names else 0
    table = [DEFAULT_RARITY] * (max_id + 1)
    unmapped = []
    for paint_kit_id, name in sorted(paint_kit_names.items()):
        rarity_name = name_to_rarity.get(name)
        if rarity_name is None:
            unmapped.append((paint_kit_id, name))
            continue
        if rarity_name not in RARITY_VALUE:
            print(f"warning: unknown rarity '{rarity_name}' for paint kit {paint_kit_id} ({name}), using default")
            continue
        table[paint_kit_id] = RARITY_VALUE[rarity_name]

    values_per_line = 16
    lines = ["#pragma once", "", "#include <array>", "#include <cstdint>", "", "namespace weapon_rarity", "{", ""]
    lines.append(f"// Paint kit id -> CS2 rarity, generated from items_game.txt ({len(paint_kit_names)} paint kits).")
    lines.append("// Values: 0 = Default, 1 = Consumer, 2 = Industrial, 3 = Mil-Spec, 4 = Restricted,")
    lines.append("//         5 = Classified, 6 = Covert, 7 = Extraordinary/Contraband, 8 = Gold (unusual).")
    lines.append(f"inline constexpr std::array<std::uint8_t, {len(table)}> kPaintKitRarities = {{")
    for i in range(0, len(table), values_per_line):
        chunk = ", ".join(str(value) for value in table[i:i + values_per_line])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("}")

    with open(output_path, "w", encoding="utf-8") as file:
        file.write("\n".join(lines))

    mapped = len(paint_kit_names) - len(unmapped)
    print(f"paint kits: {len(paint_kit_names)}, mapped to rarity: {mapped}, unmapped: {unmapped}")
    print(f"table size: {len(table)} (ids 0..{max_id})")
    print(f"written to {output_path}")


if __name__ == "__main__":
    main()
