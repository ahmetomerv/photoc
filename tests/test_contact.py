"""CLI and decoded-pixel regression tests for JPEG contact sheets."""

import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


BINARY = os.environ["PHOTOC_CONTACT_BINARY"]
IMAGE_TOOL = os.environ["PHOTOC_CONTACT_IMAGE_TOOL"]
FIXTURES = Path(__file__).parent / "fixtures"


def run(*args, status=0):
    result = subprocess.run(
        [BINARY, "contact", *map(str, args)], capture_output=True, text=True
    )
    assert result.returncode == status, (
        f"{args}: status {result.returncode}\n"
        f"stdout={result.stdout}\nstderr={result.stderr}"
    )
    return result


def tool(*args):
    return subprocess.check_output([IMAGE_TOOL, *map(str, args)], text=True).strip()


def probe(path, x, y):
    return tuple(map(int, tool("probe", path, x, y).split()))


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def orientation_six(data):
    # Minimal little-endian TIFF IFD containing EXIF Orientation=6.
    tiff = (
        b"II"
        + struct.pack("<HIH", 42, 8, 1)
        + struct.pack("<HHII", 0x0112, 3, 1, 6)
        + struct.pack("<I", 0)
    )
    payload = b"Exif\0\0" + tiff
    segment = b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload
    return data[:2] + segment + data[2:]


def main():
    with tempfile.TemporaryDirectory(prefix="photoc-contact-") as temporary:
        root = Path(temporary)
        source = root / "source"
        source.mkdir()
        output = root / "sheet.jpg"

        landscape = source / "landscape.jpg"
        portrait = source / "portrait.jpg"
        rotated = source / "rotated.jpg"
        tool("make", landscape, 80, 40)
        tool("make", portrait, 40, 80)
        rotated.write_bytes(orientation_six(landscape.read_bytes()))
        originals = {p.name: digest(p) for p in source.iterdir()}

        # Three tiles leave one empty slot. Letterboxing shows neither image
        # was stretched, and Orientation=6 puts the red half above the blue.
        run(source, "--output", output, "--columns", "2", "--thumb-size", "96")
        assert probe(output, 0, 0)[:2] == (272, 300)
        assert probe(output, 35, 70)[2] > 170  # landscape left: red
        assert probe(output, 105, 70)[4] > 170  # landscape right: blue
        assert probe(output, 50, 40)[2] > 150  # landscape letterbox
        assert probe(output, 160, 30)[2] > 150  # portrait letterbox
        assert probe(output, 180, 70)[2] > 150  # portrait left: red
        assert probe(output, 220, 70)[4] > 150  # portrait right: blue
        assert probe(output, 52, 180)[2] > 150  # rotated upper half: red
        assert probe(output, 52, 260)[4] > 150  # rotated lower half: blue
        assert int(tool("dark", output, 24, 126, 96, 14)) > 0  # filename
        assert {p.name: digest(p) for p in source.iterdir()} == originals
        subprocess.run([BINARY, "check", str(output)], check=True, capture_output=True)

        nested = source / "nested"
        nested.mkdir()
        shutil.copy2(landscape, nested / "nested.jpg")
        recursive_output = root / "recursive.jpg"
        run(source, "--output", recursive_output, "--recursive", "--columns",
            "2", "--thumb-size", "96")
        assert probe(recursive_output, 0, 0)[:2] == (272, 300)
        assert {p.name: digest(p) for p in source.iterdir() if p.is_file()} == originals

        # One photo uses the exact requested name and only one column.
        single = root / "single"
        single.mkdir()
        shutil.copy2(landscape, single / landscape.name)
        one = root / "one.jpeg"
        run(single, "--output", one, "--thumb-size", "96", "--quality", "91")
        assert probe(one, 0, 0)[:2] == (144, 158)
        assert not (root / "one-001.jpeg").exists()

        # A source wider than the thumbnail decode cap must still render: the
        # cap tracks the source so TurboJPEG's coarsest 1/8 scale fits instead
        # of refusing the image.
        wide_source = root / "wide"
        wide_source.mkdir()
        wide = wide_source / "wide.jpg"
        tool("make", wide, 4200, 840)
        wide_output = root / "wide.jpg"
        run(wide_source, "--output", wide_output, "--thumb-size", "96")
        assert probe(wide_output, 0, 0)[:2] == (144, 158)
        assert probe(wide_output, 30, 63)[2] > 150  # left half: red
        assert probe(wide_output, 90, 63)[4] > 150  # right half: blue
        subprocess.run([BINARY, "check", str(wide_output)], check=True,
                       capture_output=True)

        # Exposure lines appear only when requested; missing EXIF is valid.
        metadata_source = root / "metadata"
        metadata_source.mkdir()
        shutil.copy2(FIXTURES / "jpeg_metadata" / "all_metadata.jpg",
                     metadata_source / "a.jpg")
        shutil.copy2(FIXTURES / "jpeg" / "flat.jpg",
                     metadata_source / "b.jpg")
        with_metadata = root / "metadata.jpg"
        run(metadata_source, "--output", with_metadata, "--columns", "2",
            "--thumb-size", "96", "--metadata", "--sort", "date")
        assert probe(with_metadata, 0, 0)[:2] == (272, 194)
        assert int(tool("dark", with_metadata, 24, 144, 96, 14)) > 0
        assert int(tool("dark", with_metadata, 24, 162, 96, 14)) > 0
        assert int(tool("dark", with_metadata, 152, 144, 96, 32)) == 0

        # Seven one-column photos make two pages (six plus one).
        pages_source = root / "pages"
        pages_source.mkdir()
        for number in range(7):
            shutil.copy2(landscape, pages_source / f"{number:02}.jpg")
        paged = root / "paged.jpg"
        run(pages_source, "--output", paged, "--columns", "1",
            "--thumb-size", "96")
        first = root / "paged-001.jpg"
        second = root / "paged-002.jpg"
        assert not paged.exists()
        assert probe(first, 0, 0)[:2] == (144, 868)
        assert probe(second, 0, 0)[:2] == (144, 158)
        subprocess.run([BINARY, "check", str(first)], check=True, capture_output=True)
        subprocess.run([BINARY, "check", str(second)], check=True, capture_output=True)

        # Preflight detects a later-page collision before writing page one.
        first.unlink()
        sentinel = digest(second)
        run(pages_source, "--output", paged, "--columns", "1",
            "--thumb-size", "96", status=1)
        assert not first.exists() and digest(second) == sentinel
        single_sentinel = digest(one)
        run(single, "--output", one, status=1)
        assert digest(one) == single_sentinel

        # Bad JPEG, empty input, invalid path and excessive dimensions fail.
        invalid = root / "invalid"
        invalid.mkdir()
        shutil.copy2(FIXTURES / "jpeg" / "invalid.jpg", invalid / "bad.jpg")
        bad_output = root / "bad.jpg"
        run(invalid, "--output", bad_output, status=1)
        assert not bad_output.exists()
        empty = root / "empty"
        empty.mkdir()
        run(empty, "--output", bad_output, status=1)
        run(landscape, "--output", bad_output, status=1)
        for option, value in (
            ("--columns", "9"),
            ("--columns", "99999999999999999999"),
            ("--thumb-size", "99999999999999999999"),
            ("--quality", "0"),
            ("--sort", "random"),
        ):
            run(single, "--output", bad_output, option, value, status=2)
            assert not bad_output.exists()
        run(single, "--output", root / "bad.png", status=2)
        assert not (root / "bad.png").exists()


if __name__ == "__main__":
    main()
