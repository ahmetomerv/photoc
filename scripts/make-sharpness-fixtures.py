#!/usr/bin/env python3
"""Regenerate the synthetic sharp, blurred, and flat JPEG fixtures.

Requires the optional `cjpeg` command from libjpeg-turbo. Tests use the
checked-in JPEGs and do not run this script.
"""

from pathlib import Path
import subprocess


SIZE = 256
RADIUS = 4
FIXTURES = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "jpeg"


def box_blur(values: list[int]) -> list[int]:
    horizontal = []
    for y in range(SIZE):
        for x in range(SIZE):
            total = sum(values[y * SIZE + min(SIZE - 1, max(0, x + delta))]
                        for delta in range(-RADIUS, RADIUS + 1))
            horizontal.append((total + RADIUS) // (2 * RADIUS + 1))
    blurred = []
    for y in range(SIZE):
        for x in range(SIZE):
            total = sum(horizontal[min(SIZE - 1, max(0, y + delta)) * SIZE + x]
                        for delta in range(-RADIUS, RADIUS + 1))
            blurred.append((total + RADIUS) // (2 * RADIUS + 1))
    return blurred


def save_jpeg(name: str, values: list[int], size: int = SIZE) -> None:
    pixels = bytes(channel for value in values for channel in (value,) * 3)
    ppm = f"P6\n{size} {size}\n255\n".encode("ascii") + pixels
    encoded = subprocess.run(["cjpeg", "-quality", "95"], input=ppm,
                             capture_output=True, check=True).stdout
    (FIXTURES / name).write_bytes(encoded)


def main() -> None:
    sharp = [230 if ((x // 16 + y // 16) & 1) else 25
             for y in range(SIZE) for x in range(SIZE)]
    save_jpeg("sharp.jpg", sharp)
    save_jpeg("blurred.jpg", box_blur(sharp))
    save_jpeg("flat.jpg", [128] * (SIZE * SIZE))
    large_size = 2048
    large = [230 if ((x // 32 + y // 32) & 1) else 25
             for y in range(large_size) for x in range(large_size)]
    save_jpeg("large_sharp.jpg", large, large_size)


if __name__ == "__main__":
    main()
