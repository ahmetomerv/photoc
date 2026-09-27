#!/usr/bin/env python3
"""Regenerate synthetic sharp / blurred / flat JPEG fixtures.

Requires `cjpeg` from libjpeg-turbo. Tests use the checked-in JPEGs and do not
run this script. All pixels are procedurally generated (checkerboards and solid
fills); no photographs are used.

Usage:
  python3 scripts/make-sharpness-fixtures.py
"""

from __future__ import annotations

from pathlib import Path
import subprocess


# Small enough for comparative Laplacian scores, large enough for stable order.
SIZE = 128
RADIUS = 3
LARGE_SIZE = 2048
FIXTURES = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "jpeg"


def box_blur(values: list[int], size: int = SIZE) -> list[int]:
    horizontal = []
    for y in range(size):
        for x in range(size):
            total = sum(values[y * size + min(size - 1, max(0, x + delta))]
                        for delta in range(-RADIUS, RADIUS + 1))
            horizontal.append((total + RADIUS) // (2 * RADIUS + 1))
    blurred = []
    for y in range(size):
        for x in range(size):
            total = sum(horizontal[min(size - 1, max(0, y + delta)) * size + x]
                        for delta in range(-RADIUS, RADIUS + 1))
            blurred.append((total + RADIUS) // (2 * RADIUS + 1))
    return blurred


def save_jpeg(name: str, values: list[int], size: int, quality: int,
              grayscale: bool = False) -> None:
    if grayscale:
        header = f"P5\n{size} {size}\n255\n".encode("ascii")
        body = bytes(values)
        args = ["cjpeg", "-quality", str(quality), "-grayscale", "-optimize"]
    else:
        header = f"P6\n{size} {size}\n255\n".encode("ascii")
        body = bytes(channel for value in values for channel in (value,) * 3)
        args = ["cjpeg", "-quality", str(quality), "-optimize"]
    encoded = subprocess.run(args, input=header + body, capture_output=True,
                             check=True).stdout
    (FIXTURES / name).write_bytes(encoded)


def main() -> None:
    FIXTURES.mkdir(parents=True, exist_ok=True)
    cell = 8
    sharp = [230 if ((x // cell + y // cell) & 1) else 25
             for y in range(SIZE) for x in range(SIZE)]
    save_jpeg("sharp.jpg", sharp, SIZE, quality=80)
    save_jpeg("blurred.jpg", box_blur(sharp), SIZE, quality=80)
    save_jpeg("flat.jpg", [128] * (SIZE * SIZE), SIZE, quality=80)
    # Dimension-only fixture for scaled decode. Solid gray compresses tightly.
    save_jpeg("large_sharp.jpg", [128] * (LARGE_SIZE * LARGE_SIZE), LARGE_SIZE,
              quality=10, grayscale=True)
    print(f"wrote sharpness fixtures under {FIXTURES}")


if __name__ == "__main__":
    main()
