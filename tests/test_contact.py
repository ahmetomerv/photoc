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


# Sheets are checked with structural probes and tolerant thresholds rather
# than a byte or decoded-pixel golden: libjpeg-turbo's scaling and chroma
# upsampling differ between macOS and Linux builds, so exact output is not
# reproducible. Thresholds sit well below measured values (a 0-255 checker
# range asserted above 100, quadrant colours asserted by channel dominance) so
# routine codec differences cannot flip them.
def region_stats(path, x, y, width, height):
    minimum, maximum, mean, dark, bright = tool(
        "stats", path, x, y, width, height).split()
    return {
        "min": int(minimum),
        "max": int(maximum),
        "mean": float(mean),
        "dark": int(dark),
        "bright": int(bright),
    }


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


def test_detail_and_scale(root):
    """Quadrant colours, centred boundaries and checker detail at both ends of
    the thumbnail-size range. A wrong fit moves the colour boundaries away
    from the middle, a coarse decode blurs the checker to flat grey, and a
    blank tile loses the colours entirely."""
    source = root / "card-source"
    source.mkdir()
    tool("make-card", source / "card.jpg", 480, 480)

    for thumb in (96, 512):
        output = root / f"card-{thumb}.jpg"
        run(source, "--output", output, "--columns", "1",
            "--thumb-size", str(thumb), "--quality", "100")
        assert probe(output, 0, 0)[:2] == (thumb + 48, thumb + 62)
        left = 16 + 8  # CONTACT_MARGIN + CONTACT_PADDING

        def at(fx, fy):
            return probe(output, left + int(thumb * fx),
                         left + int(thumb * fy))

        # Probe results are (width, height, r, g, b).
        top_left, top_right = at(0.125, 0.125), at(0.875, 0.125)
        bottom_left, bottom_right = at(0.125, 0.875), at(0.875, 0.875)
        assert top_left[2] > 170 and top_left[2] - top_left[3] > 100
        assert top_right[3] > 150 and top_right[3] - top_right[2] > 100
        assert bottom_left[4] > 150 and bottom_left[4] - bottom_left[2] > 100
        assert (bottom_right[2] > 170 and bottom_right[3] > 150 and
                bottom_right[4] < 100)
        # The red/green edge stays near x = 0.5 and red/blue near y = 0.5.
        assert at(0.42, 0.06)[2] > at(0.42, 0.06)[3]
        assert at(0.58, 0.06)[3] > at(0.58, 0.06)[2]
        assert at(0.06, 0.42)[2] > at(0.06, 0.42)[4]
        assert at(0.06, 0.58)[4] > at(0.06, 0.58)[2]
        # Corner samples confirm the source is not cropped inward.
        assert at(0.03, 0.03)[2] > at(0.03, 0.03)[3]
        assert at(0.97, 0.97)[2] > at(0.97, 0.97)[4]
        # Measured range is 0-255 with the checker intact; a blurred or blank
        # tile would collapse towards a single mid-grey value.
        detail = region_stats(output, left + int(thumb * 0.35),
                              left + int(thumb * 0.35),
                              int(thumb * 0.3), int(thumb * 0.3))
        assert detail["max"] - detail["min"] > 100
        assert detail["dark"] > 0 and detail["bright"] > 0
        subprocess.run([BINARY, "check", str(output)], check=True,
                       capture_output=True)


def test_orientation_card(root):
    """EXIF Orientation=6 rotates the stored quadrants a quarter turn; a
    missing or reversed rotation leaves them in their stored positions."""
    source = root / "rotated-source"
    source.mkdir()
    base = source / "rotated.jpg"
    tool("make-card", base, 480, 480)
    base.write_bytes(orientation_six(base.read_bytes()))
    output = root / "rotated-card.jpg"
    run(source, "--output", output, "--columns", "1", "--thumb-size", "240",
        "--quality", "100")
    assert probe(output, 0, 0)[:2] == (240 + 48, 240 + 62)
    thumb, left = 240, 24

    def at(fx, fy):
        return probe(output, left + int(thumb * fx), left + int(thumb * fy))

    # Stored order is red, green, blue, yellow; after a clockwise quarter
    # turn blue is top-left, red top-right, yellow bottom-left, green
    # bottom-right.
    assert at(0.125, 0.125)[4] - at(0.125, 0.125)[2] > 100
    assert at(0.875, 0.125)[2] - at(0.875, 0.125)[3] > 100
    assert at(0.125, 0.875)[2] > 170 and at(0.125, 0.875)[3] > 150
    assert at(0.875, 0.875)[3] - at(0.875, 0.875)[2] > 100


def test_source_beyond_decode_cap(root):
    """A 5000 px source, longer than the old 4096 px decode cap, must still
    render the whole ramp in order with the correct letterbox."""
    source = root / "gradient-source"
    source.mkdir()
    tool("make-gradient", source / "gradient.jpg", 5000, 800)
    output = root / "gradient.jpg"
    run(source, "--output", output, "--columns", "1", "--thumb-size", "96",
        "--quality", "100")
    assert probe(output, 0, 0)[:2] == (144, 158)
    left = 24
    # 96*800/5000 rounds to a 15 px band spanning rows 64-78 of the 96 px box.
    band_y = left + (96 - 96 * 800 // 5000) // 2 + 7
    fractions = (0.05, 0.25, 0.5, 0.75, 0.95)
    reds = [probe(output, left + int(96 * fx), band_y)[2] for fx in fractions]
    blues = [probe(output, left + int(96 * fx), band_y)[4] for fx in fractions]
    assert reds == sorted(reds), reds
    assert blues == sorted(blues, reverse=True), blues
    assert reds[-1] - reds[0] > 150, reds
    # The fitted band must start and end where the aspect ratio puts it; a
    # stretched or shrunk fit moves these rows into the letterbox.
    for row in (66, 76):
        pixel = probe(output, left + 48, row)
        assert max(pixel[2:]) - min(pixel[2:]) > 80, pixel
    # Rows outside the band stay a uniform light background.
    for row in (58, 82):
        strip = region_stats(output, left, row, 96, 5)
        assert strip["max"] - strip["min"] < 20, strip
        assert strip["mean"] > 225, strip


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

        # Thumbnail-scale decode: detail, orientation and a source beyond the
        # old decode cap.
        test_detail_and_scale(root)
        test_orientation_card(root)
        test_source_beyond_decode_cap(root)

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
