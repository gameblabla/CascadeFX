#!/usr/bin/env python3
"""Convert an 8-bit indexed, non-interlaced PNG texture atlas to Cascade FX C header.

The runtime stores 8-bit palette indices packed two pixels per uint16_t in
big-endian order so the generated values match the original hand-generated
headers, e.g. bytes [0x12, 0x34] become 0x1234.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

PNG_SIG = b"\x89PNG\r\n\x1a\n"


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_indexed_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIG):
        raise ValueError(f"{path}: not a PNG file")
    pos = len(PNG_SIG)
    width = height = bit_depth = color_type = compression = png_filter = interlace = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, compression, png_filter, interlace = struct.unpack(">IIBBBBB", chunk)
        elif ctype == b"IDAT":
            idat.extend(chunk)
        elif ctype == b"IEND":
            break
    if width is None:
        raise ValueError(f"{path}: missing IHDR")
    if bit_depth != 8 or color_type != 3 or compression != 0 or png_filter != 0 or interlace != 0:
        raise ValueError(
            f"{path}: expected 8-bit indexed non-interlaced PNG; "
            f"got bit_depth={bit_depth}, color_type={color_type}, interlace={interlace}"
        )
    raw = zlib.decompress(bytes(idat))
    row_bytes = width
    expected = height * (1 + row_bytes)
    if len(raw) != expected:
        raise ValueError(f"{path}: decompressed size {len(raw)} != expected {expected}")
    out = bytearray(width * height)
    prev = bytearray(row_bytes)
    rp = op = 0
    for _y in range(height):
        f = raw[rp]
        rp += 1
        src = raw[rp:rp + row_bytes]
        rp += row_bytes
        row = bytearray(row_bytes)
        if f == 0:
            row[:] = src
        elif f == 1:
            for x, val in enumerate(src):
                left = row[x - 1] if x else 0
                row[x] = (val + left) & 0xff
        elif f == 2:
            for x, val in enumerate(src):
                row[x] = (val + prev[x]) & 0xff
        elif f == 3:
            for x, val in enumerate(src):
                left = row[x - 1] if x else 0
                up = prev[x]
                row[x] = (val + ((left + up) >> 1)) & 0xff
        elif f == 4:
            for x, val in enumerate(src):
                left = row[x - 1] if x else 0
                up = prev[x]
                up_left = prev[x - 1] if x else 0
                row[x] = (val + paeth(left, up, up_left)) & 0xff
        else:
            raise ValueError(f"{path}: unsupported PNG filter {f}")
        out[op:op + row_bytes] = row
        op += row_bytes
        prev = row
    return width, height, bytes(out)


def read_index_map(path: Path) -> list[int]:
    """Read a whitespace/comment old->new palette index map."""
    mapping = list(range(256))
    seen = set()
    for lineno, line in enumerate(path.read_text().splitlines(), start=1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2:
            raise ValueError(f"{path}:{lineno}: expected 'old new'")
        old, new = (int(parts[0], 0), int(parts[1], 0))
        if not (0 <= old <= 255 and 0 <= new <= 255):
            raise ValueError(f"{path}:{lineno}: palette indices must be 0..255")
        mapping[old] = new
        seen.add(old)
    if len(seen) != 256:
        raise ValueError(f"{path}: map must define all 256 old indices")
    return mapping


def apply_index_map(pixels: bytes, mapping: list[int]) -> bytes:
    return bytes(mapping[b] for b in pixels)

def write_header(path: Path, symbol: str, pixels: bytes) -> None:
    with path.open("w", newline="\n") as f:
        f.write("// Auto-generated from textures.png. Do not edit by hand.\n")
        f.write("// 8-bit palette indices, one byte per texel. Optional low-index remap may be applied.\n")
        f.write(f"const unsigned char {symbol}[{len(pixels)}] = {{\n")
        for i in range(0, len(pixels), 16):
            f.write("\t")
            f.write(", ".join(f"0x{b:02x}" for b in pixels[i:i + 16]))
            if i + 16 < len(pixels):
                f.write(",")
            f.write("\n")
        f.write("};\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("png", type=Path)
    ap.add_argument("header", type=Path)
    ap.add_argument("symbol")
    ap.add_argument("--width", type=int, default=16)
    ap.add_argument("--height", type=int, default=112)
    ap.add_argument("--index-map", type=Path, default=None, help="optional old->new palette index mapping")
    ns = ap.parse_args()
    width, height, pixels = read_indexed_png(ns.png)
    if (width, height) != (ns.width, ns.height):
        raise ValueError(f"{ns.png}: expected {ns.width}x{ns.height}, got {width}x{height}")
    if ns.index_map is not None:
        pixels = apply_index_map(pixels, read_index_map(ns.index_map))
    ns.header.parent.mkdir(parents=True, exist_ok=True)
    write_header(ns.header, ns.symbol, pixels)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"png_indexed_to_texture_header.py: {e}", file=sys.stderr)
        raise SystemExit(1)
