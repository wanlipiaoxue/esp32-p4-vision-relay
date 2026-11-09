from __future__ import annotations

import argparse
from pathlib import Path


def read_pgm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    offset = 0
    tokens: list[bytes] = []
    while len(tokens) < 4:
        while offset < len(data) and data[offset] in b" \t\r\n":
            offset += 1
        if offset >= len(data) or data[offset] == ord("#"):
            raise ValueError(f"unsupported PGM header in {path}")
        end = offset
        while end < len(data) and data[end] not in b" \t\r\n":
            end += 1
        tokens.append(data[offset:end])
        offset = end
    while offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1

    if tokens[0] != b"P5":
        raise ValueError(f"expected binary PGM P5, got {tokens[0]!r}")
    width = int(tokens[1])
    height = int(tokens[2])
    max_value = int(tokens[3])
    if max_value != 255:
        raise ValueError(f"expected max value 255, got {max_value}")
    pixels = data[offset:]
    if len(pixels) != width * height:
        raise ValueError(
            f"pixel byte count mismatch: expected {width * height}, got {len(pixels)}"
        )
    return width, height, pixels


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pgm", type=Path)
    parser.add_argument("header", type=Path)
    parser.add_argument("--id", required=True, type=int)
    parser.add_argument("--symbol", default="golden_tag_530_gray")
    args = parser.parse_args()

    width, height, pixels = read_pgm(args.pgm)
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"/* Generated test image: Tag36H11 ID {args.id}, {width}x{height}. */",
        f"static const uint8_t {args.symbol}[{width} * {height}] = {{",
    ]
    for start in range(0, len(pixels), 16):
        values = ", ".join(f"0x{value:02x}" for value in pixels[start : start + 16])
        lines.append(f"    {values},")
    lines.extend(["};", ""])
    args.header.write_text("\n".join(lines), encoding="ascii", newline="\n")


if __name__ == "__main__":
    main()
