#!/usr/bin/env python3
"""Generate PC-FX/SuperGrafx-style dual-VDC title/game backgrounds.

The PC-FX has two HuC6270-compatible VDCs behind the VCE/Tetsu.  When the
VCE background color-depth bit is set to 256-color mode, the BG pixels from
VDC0 and VDC1 are combined: VDC0 supplies the high nibble and VDC1 supplies
the low nibble of one 8-bit palette index.  This is the SuperGrafx-style BG
combination path visible in the emulator's VDC/VCE implementation.

For the current 256x240 source PNGs the unique color count is below the 240
reliably displayable combo slots whose low nibble is non-zero.  We therefore
preserve the artwork at source-index granularity instead of forcing one 16-color
palette per tile.  If a future image exceeds that budget, the fallback is a
frequency-weighted k-means quantizer inspired by tiledpalettequant: it keeps the
VDC tile geometry and optimizes the available combo-palette slots in RGB space
before conversion to PC-FX Y8U4V4 palette words.
"""
from __future__ import annotations

import argparse
import math
import random
from collections import Counter
from pathlib import Path
from typing import Iterable

from PIL import Image

WIDTH = 256
HEIGHT = 240
MAP_W = 64
MAP_H = 32
VISIBLE_TILES_X = WIDTH // 8
VISIBLE_TILES_Y = HEIGHT // 8
TILE_BASE = 0x0200
TILE_WORDS = 16
COMBO_SLOTS = [i for i in range(1, 256) if (i & 0x0F) != 0]
TRANSPARENT_YUV = 0x0088


def load_rgb(path: Path) -> tuple[list[tuple[int, int, int]], list[tuple[int, int, int]]]:
    im = Image.open(path).convert("RGB")
    if im.size != (WIDTH, HEIGHT):
        raise ValueError(f"{path}: expected {WIDTH}x{HEIGHT}, got {im.size}")
    pixels = list(im.getdata())
    unique = sorted(set(pixels), key=lambda c: (-pixels.count(c), c))
    return pixels, unique


def clamp(v: float, lo: int = 0, hi: int = 255) -> int:
    return lo if v < lo else hi if v > hi else int(round(v))


def yuv_to_rgb(y: int, u4: int, v4: int) -> tuple[int, int, int]:
    # Matches the PC-FX emulator path closely: U/V nibbles are shifted into
    # the high four bits before the YUV->RGB matrix is applied.  The palette
    # search intentionally keeps fractional offsets until final rounding; this
    # gives lower visible error on the anime/title artwork than mirroring the
    # emulator's intermediate integer truncation.
    u = (u4 << 4) - 128
    v = (v4 << 4) - 128
    r = y - 0.000039457070707 * u + 1.139827967171717 * v
    g = y - 0.394610164141414 * u - 0.580500315656566 * v
    b = y + 2.031999684343434 * u - 0.000481376262626 * v
    return (clamp(r), clamp(g), clamp(b))


# Precompute offsets for the 16x16 chroma grid.  For a fixed U/V, the best Y is
# the least-squares offset along the grey axis, then clamped to 8-bit.
UV_OFFSETS: list[tuple[int, int, float, float, float]] = []
for u4 in range(16):
    for v4 in range(16):
        u = (u4 << 4) - 128
        v = (v4 << 4) - 128
        ro = -0.000039457070707 * u + 1.139827967171717 * v
        go = -0.394610164141414 * u - 0.580500315656566 * v
        bo = 2.031999684343434 * u - 0.000481376262626 * v
        UV_OFFSETS.append((u4, v4, ro, go, bo))


def rgb_to_yuv_word(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    best_word = TRANSPARENT_YUV
    best_err = 1e30
    # Slightly favor green/luma stability, which better matches visible PC-FX
    # composite/S-video output and tiledpalettequant's weighted RGB distance.
    wr, wg, wb = 2.0, 4.0, 1.0
    for u4, v4, ro, go, bo in UV_OFFSETS:
        y = (wr * (r - ro) + wg * (g - go) + wb * (b - bo)) / (wr + wg + wb)
        yi = clamp(y)
        rr = clamp(yi + ro)
        gg = clamp(yi + go)
        bb = clamp(yi + bo)
        err = wr * (rr - r) * (rr - r) + wg * (gg - g) * (gg - g) + wb * (bb - b) * (bb - b)
        if err < best_err:
            best_err = err
            best_word = (yi << 8) | (u4 << 4) | v4
    return best_word

def color_distance2(a: tuple[int, int, int], b: tuple[int, int, int]) -> float:
    dr = a[0] - b[0]
    dg = a[1] - b[1]
    db = a[2] - b[2]
    return 2.0 * dr * dr + 4.0 * dg * dg + db * db


def weighted_kmeans(colors: list[tuple[int, int, int]], counts: list[int], k: int, seed: int = 0) -> list[tuple[int, int, int]]:
    rng = random.Random(seed)
    if len(colors) <= k:
        return colors[:]

    centers: list[tuple[float, float, float]] = [tuple(float(x) for x in colors[counts.index(max(counts))])]
    while len(centers) < k:
        weights = []
        total = 0.0
        for c, n in zip(colors, counts):
            d = min(color_distance2(c, (int(cc[0]), int(cc[1]), int(cc[2]))) for cc in centers)
            w = max(1.0, d) * n
            weights.append(w)
            total += w
        pick = rng.random() * total
        acc = 0.0
        chosen = colors[-1]
        for c, w in zip(colors, weights):
            acc += w
            if acc >= pick:
                chosen = c
                break
        centers.append(tuple(float(x) for x in chosen))

    assignments = [0] * len(colors)
    for _ in range(32):
        changed = False
        sums = [[0.0, 0.0, 0.0, 0.0] for _ in range(k)]
        for i, (c, n) in enumerate(zip(colors, counts)):
            best_i = 0
            best_d = 1e30
            for ci, center in enumerate(centers):
                d = color_distance2(c, (int(center[0]), int(center[1]), int(center[2])))
                if d < best_d:
                    best_d = d
                    best_i = ci
            if assignments[i] != best_i:
                changed = True
                assignments[i] = best_i
            sums[best_i][0] += c[0] * n
            sums[best_i][1] += c[1] * n
            sums[best_i][2] += c[2] * n
            sums[best_i][3] += n
        for ci in range(k):
            if sums[ci][3] <= 0:
                c = colors[rng.randrange(len(colors))]
                centers[ci] = tuple(float(x) for x in c)
            else:
                inv = 1.0 / sums[ci][3]
                centers[ci] = (sums[ci][0] * inv, sums[ci][1] * inv, sums[ci][2] * inv)
        if not changed:
            break
    return [(clamp(c[0]), clamp(c[1]), clamp(c[2])) for c in centers]


def quantize_to_slots(pixels: list[tuple[int, int, int]], max_colors: int) -> tuple[dict[tuple[int, int, int], int], list[tuple[int, int, int]]]:
    counts = Counter(pixels)
    colors = list(counts.keys())
    weights = [counts[c] for c in colors]
    if len(colors) <= max_colors:
        palette_colors = sorted(colors, key=lambda c: (-counts[c], c))
    else:
        palette_colors = weighted_kmeans(colors, weights, max_colors, seed=0xC0FFEE)

    mapping: dict[tuple[int, int, int], int] = {}
    if len(colors) <= max_colors:
        for i, c in enumerate(palette_colors):
            mapping[c] = COMBO_SLOTS[i]
    else:
        # Snap centers to actual PC-FX YUV-representable colors for assignment.
        snapped_rgb = []
        for c in palette_colors:
            w = rgb_to_yuv_word(c)
            snapped_rgb.append(yuv_to_rgb((w >> 8) & 0xFF, (w >> 4) & 0x0F, w & 0x0F))
        for c in colors:
            best_i = min(range(len(snapped_rgb)), key=lambda i: color_distance2(c, snapped_rgb[i]))
            mapping[c] = COMBO_SLOTS[best_i]
    # The VCE palette is programmed from the unsnapped centers; hardware YUV
    # snapping is applied by rgb_to_yuv_word when emitting the header.
    return mapping, palette_colors


def encode_tile(nibbles: Iterable[int]) -> tuple[int, ...]:
    pix = list(nibbles)
    words_lo: list[int] = []
    words_hi: list[int] = []
    for y in range(8):
        p0 = p1 = p2 = p3 = 0
        for x in range(8):
            n = pix[y * 8 + x] & 0x0F
            bit = 1 << (7 - x)
            if n & 1:
                p0 |= bit
            if n & 2:
                p1 |= bit
            if n & 4:
                p2 |= bit
            if n & 8:
                p3 |= bit
        words_lo.append((p1 << 8) | p0)
        words_hi.append((p3 << 8) | p2)
    return tuple(words_lo + words_hi)


def convert_image(path: Path, symbol: str) -> dict[str, object]:
    pixels, unique = load_rgb(path)
    color_to_slot, palette_colors = quantize_to_slots(pixels, len(COMBO_SLOTS))

    vce_palette = [TRANSPARENT_YUV] * 256
    # palette_colors order corresponds to COMBO_SLOTS order when no fallback
    # quantization happened; when fallback happened, mapping may use a subset.
    if len(unique) <= len(COMBO_SLOTS):
        for c, slot in color_to_slot.items():
            vce_palette[slot] = rgb_to_yuv_word(c)
    else:
        for c, slot in zip(palette_colors, COMBO_SLOTS):
            vce_palette[slot] = rgb_to_yuv_word(c)

    slot_pixels = [color_to_slot[p] for p in pixels]
    bat = [0] * (MAP_W * MAP_H)
    tile_lookup: dict[tuple[tuple[int, ...], tuple[int, ...]], int] = {}
    tiles_hi: list[tuple[int, ...]] = []
    tiles_lo: list[tuple[int, ...]] = []

    for ty in range(VISIBLE_TILES_Y):
        for tx in range(VISIBLE_TILES_X):
            slots = [slot_pixels[(ty * 8 + y) * WIDTH + (tx * 8 + x)] for y in range(8) for x in range(8)]
            hi = encode_tile([(s >> 4) & 0x0F for s in slots])
            lo = encode_tile([s & 0x0F for s in slots])
            key = (hi, lo)
            tile_no = tile_lookup.get(key)
            if tile_no is None:
                tile_no = len(tiles_hi)
                tile_lookup[key] = tile_no
                tiles_hi.append(hi)
                tiles_lo.append(lo)
            bat[ty * MAP_W + tx] = TILE_BASE + tile_no

    if TILE_BASE + len(tiles_hi) > 0x1000:
        raise ValueError(f"{symbol}: VDC tile index overflow: {len(tiles_hi)} unique tiles")

    recon = [yuv_to_rgb((vce_palette[s] >> 8) & 0xFF, (vce_palette[s] >> 4) & 0x0F, vce_palette[s] & 0x0F) for s in slot_pixels]
    mse = sum(color_distance2(a, b) for a, b in zip(pixels, recon)) / len(pixels)
    used_slots = len(set(slot_pixels))
    return {
        "symbol": symbol,
        "bat": bat,
        "tiles_hi": tiles_hi,
        "tiles_lo": tiles_lo,
        "palette": vce_palette,
        "used_slots": used_slots,
        "tile_count": len(tiles_hi),
        "mse": mse,
    }


def flat(tiles: list[tuple[int, ...]]) -> list[int]:
    return [w for tile in tiles for w in tile]


def write_array(f, typename: str, name: str, values: list[int], per_line: int = 8) -> None:
    f.write(f"static const {typename} {name}[{len(values)}] = {{\n")
    for i in range(0, len(values), per_line):
        f.write("\t" + ", ".join(f"0x{x:04x}" for x in values[i:i + per_line]))
        if i + per_line < len(values):
            f.write(",")
        f.write("\n")
    f.write("};\n\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=Path("assets/source"))
    ap.add_argument("--out", type=Path, default=Path("assets/generated/pcfx/vdc_background.h"))
    ap.add_argument("--map", type=Path, default=None, help="accepted for Makefile compatibility; not used by dual-VDC conversion")
    ns = ap.parse_args()

    images = [
        ("title", ns.src / "title.png"),
        ("game", ns.src / "background2.png"),
    ]
    converted = [convert_image(path, symbol) for symbol, path in images]

    ns.out.parent.mkdir(parents=True, exist_ok=True)
    with ns.out.open("w", newline="\n") as f:
        f.write("/* Auto-generated by tools/generate_pcfx_vdc_background.py. */\n")
        f.write("#ifndef CFX_PC_FX_VDC_BACKGROUND_H\n#define CFX_PC_FX_VDC_BACKGROUND_H\n\n")
        f.write(f"#define PCFX_VDC_BG_MAP_WORDS {MAP_W * MAP_H}\n")
        f.write(f"#define PCFX_VDC_BG_TILE_BASE 0x{TILE_BASE:04x}\n")
        f.write(f"#define PCFX_VDC_BG_TILE_WORDS {TILE_WORDS}\n")
        f.write("#define PCFX_VDC_BG_PALETTE_WORDS 256\n")
        f.write("#define PCFX_VDC_BG_USES_DUAL_COMBO 1\n\n")
        for data in converted:
            symbol = str(data["symbol"])
            f.write(f"#define PCFX_VDC_{symbol.upper()}_TILE_COUNT {data['tile_count']}\n")
            f.write(f"#define PCFX_VDC_{symbol.upper()}_USED_COLORS {data['used_slots']}\n")
            f.write(f"#define PCFX_VDC_{symbol.upper()}_WEIGHTED_MSE {data['mse']:.3f}\n")
            write_array(f, "unsigned short", f"pcfx_vdc_{symbol}_palette", data["palette"])  # type: ignore[arg-type]
            write_array(f, "unsigned short", f"pcfx_vdc_{symbol}_bat", data["bat"])  # type: ignore[arg-type]
            write_array(f, "unsigned short", f"pcfx_vdc_{symbol}_tiles_hi", flat(data["tiles_hi"]))  # type: ignore[arg-type]
            write_array(f, "unsigned short", f"pcfx_vdc_{symbol}_tiles_lo", flat(data["tiles_lo"]))  # type: ignore[arg-type]
        f.write("#endif\n")

    for data in converted:
        print(f"{data['symbol']}: colors={data['used_slots']} tiles={data['tile_count']} weighted_mse={data['mse']:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
