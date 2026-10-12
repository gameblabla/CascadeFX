#!/usr/bin/env python3
"""Emit the PC-FX 8bpp texture as a signed-byte C array."""

from pathlib import Path
import sys


EXPECTED_BYTES = 32 * 224


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.bin output.h", file=sys.stderr)
        return 2

    source = Path(sys.argv[1])
    target = Path(sys.argv[2])
    pixels = source.read_bytes()
    if len(pixels) != EXPECTED_BYTES:
        raise SystemExit(
            f"{source}: expected {EXPECTED_BYTES} texture bytes, got {len(pixels)}"
        )

    signed_pixels = [value if value < 0x80 else value - 0x100 for value in pixels]
    lines = [
        "/* Generated from textures.png by comp.sh. */",
        "#include <stdint.h>",
        "int8_t texture[32 * 224] __attribute__((aligned(2))) = {",
    ]
    for start in range(0, len(signed_pixels), 16):
        chunk = signed_pixels[start : start + 16]
        lines.append("\t" + ", ".join(map(str, chunk)) + ",")
    lines.append("};")
    target.write_text("\n".join(lines) + "\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
