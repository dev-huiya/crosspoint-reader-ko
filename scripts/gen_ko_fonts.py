"""Regenerate the KO built-in fonts with the upstream 1.6 compressed format.

Run from the repository root with a Python environment containing freetype-py
and fonttools. The source TTFs are inherited from the KO 1.5.0-ko.3 tree.
"""

from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "lib/EpdFont/builtinFonts"
CONVERTER = ROOT / "lib/EpdFont/scripts/fontconvert.py"
COMMON_INTERVALS = (
    "0x1100,0x11FF",  # Hangul Jamo
    "0x3000,0x303F",  # CJK punctuation
    "0x3130,0x318F",  # Compatibility Jamo
    "0xAC00,0xD7A3",  # Full Hangul syllables
)


def generate(name: str, size: int, source: Path, extra_intervals=()) -> None:
    intervals = (*COMMON_INTERVALS, *extra_intervals)
    args = [sys.executable, str(CONVERTER), name, str(size), str(source), "--2bit", "--compress"]
    for interval in intervals:
        args.extend(("--additional-intervals", interval))
    target = FONT_DIR / f"{name}.h"
    with target.open("wb") as output:
        subprocess.run(args, cwd=ROOT, stdout=output, check=True)
    print(f"{target.relative_to(ROOT)}: {target.stat().st_size} bytes")


if __name__ == "__main__":
    generate(
        "kopub_14_regular",
        14,
        FONT_DIR / "source/KoPub-Batang/KoPub Batang Light.ttf",
        ("0x4E00,0x9FFF",),  # Hanja supplied by KoPub
    )
    generate(
        "pretendard_10_regular",
        10,
        FONT_DIR / "source/Pretendard/Pretendard-Regular.ttf",
    )
