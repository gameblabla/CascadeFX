#!/usr/bin/env python3
"""Regenerate the Cascade FX palette/index remap for the fast 32x32 mapper.

The texture atlas is re-indexed so every texel index is < 128.  On V810 and
SH1, signed byte loads can then be OR-packed directly into a 16-bit pixel pair
without zero-extending the low/right byte.

PC-FX DEBUG_FPS uses the SUP layer with palette bank 12, so entries 192..207
are kept unchanged.  Two very rare image-only colors are approximated to keep
that fixed bank and the duplicated texture colors inside the 256-entry palette.
"""
from __future__ import annotations

import argparse
import collections
import re
from pathlib import Path
from PIL import Image

HEX_RE = re.compile(r"0x[0-9a-fA-F]+")
FIXED = {0, 255, *range(192, 208)}
# Rare image-only colors.  They do not appear in the texture; these are mapped
# to the nearest available colors to make room for duplicating texture colors
# 197/198 into the low signed-byte-safe range while preserving SUP bank 12.
SACRIFICE = {252: 203, 240: 217}


def parse_hex(path: Path) -> list[int]:
    return [int(x, 16) for x in HEX_RE.findall(path.read_text())]


def build_mapping(root: Path) -> list[int]:
    texture_indices = sorted(set(Image.open(root / "textures.png").getdata()))
    old_to_new: list[int | None] = [None] * 256
    used_new: set[int] = set()

    next_low = 1
    for old in texture_indices:
        while next_low in FIXED:
            next_low += 1
        old_to_new[old] = next_low
        used_new.add(next_low)
        next_low += 1

    for old in FIXED:
        if old_to_new[old] is None:
            old_to_new[old] = old
            used_new.add(old)

    for old, target in SACRIFICE.items():
        old_to_new[old] = old_to_new[target]

    for old in range(256):
        if old_to_new[old] is not None:
            continue
        if old not in used_new and old not in FIXED:
            new = old
        else:
            new = next(i for i in range(1, 255) if i not in used_new and i not in FIXED)
        old_to_new[old] = new
        used_new.add(new)

    mapping = [int(x) for x in old_to_new]
    if max(mapping[i] for i in texture_indices) >= 128:
        raise ValueError("texture indices are not all below 128 after remap")
    return mapping


def write_mapping(path: Path, mapping: list[int], texture_count: int) -> None:
    with path.open("w", newline="\n") as f:
        f.write("# Cascade FX palette mapping: old_index new_index\n")
        f.write(f"# Texture atlas colors are mapped below 128 for signed-byte fast loops. Distinct texture colors: {texture_count}.\n")
        f.write("# PC-FX SUP palette bank 12 (192..207) is preserved for DEBUG_FPS text; texture colors 197/198 are duplicated.\n")
        f.write("# Rare image-only colors 240 and 252 are remapped to near colors 217 and 203 to keep the palette within 256 entries.\n")
        for old, new in enumerate(mapping):
            f.write(f"{old} {new}\n")


def write_gamepal(path: Path, mapping: list[int]) -> None:
    oldpal = parse_hex(path)[:256]
    newpal = oldpal[:]
    for old, new in enumerate(mapping):
        if old in SACRIFICE:
            continue
        newpal[new] = oldpal[old]
    for old in FIXED:
        newpal[old] = oldpal[old]

    with path.open("w", newline="\n") as f:
        f.write("/* Auto-remapped for fast texture inner loops.\n")
        f.write("   Texture palette indices live below 128 so signed byte loads can be packed without zero-extension.\n")
        f.write("   PC-FX SUP palette bank 12 is preserved for DEBUG_FPS text. */\n")
        f.write("const unsigned short gamepal[256] = {\n")
        for i in range(0, 256, 8):
            f.write("\t" + ", ".join(f"0x{x:04x}" for x in newpal[i:i + 8]))
            if i + 8 < 256:
                f.write(",")
            f.write("\n")
        f.write("};\n\nconst int gamepal_length = 256;\n")


def remap_packed_header(path: Path, mapping: list[int]) -> None:
    text = path.read_text()
    m = re.search(r"(const\s+unsigned\s+short\s+\w+\s*\[\s*\d+\s*\]\s*=\s*\{)", text)
    if not m:
        raise ValueError(f"{path}: no unsigned short array declaration found")
    words = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{4}", text)]
    out = [((mapping[(w >> 8) & 0xff] << 8) | mapping[w & 0xff]) for w in words]
    with path.open("w", newline="\n") as f:
        f.write("/* Auto-remapped with tools/remap_palette_assets.py. */\n")
        f.write(m.group(1) + "\n")
        for i in range(0, len(out), 8):
            f.write("\t" + ", ".join(f"0x{x:04x}" for x in out[i:i + 8]))
            if i + 8 < len(out):
                f.write(",")
            f.write("\n")
        f.write("};\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("root", type=Path, nargs="?", default=Path("."))
    ns = ap.parse_args()
    root = ns.root
    mapping = build_mapping(root)
    texture_count = len(set(Image.open(root / "textures.png").getdata()))
    write_mapping(root / "tools/texture_low_index_map.txt", mapping, texture_count)
    write_gamepal(root / "include/pcfx/gamepal.h", mapping)
    write_gamepal(root / "include/casloopy/gamepal.h", mapping)
    remap_packed_header(root / "include/pcfx/title.h", mapping)
    remap_packed_header(root / "src/bg.h", mapping)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
