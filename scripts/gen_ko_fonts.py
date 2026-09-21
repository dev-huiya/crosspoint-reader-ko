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
# On top of fontconvert.py's Latin/punctuation/math/arrows defaults. Korean
# books lean on the KS X 1001 symbol repertoire (※ ★ ☆ ♪ ① ℃, box drawing,
# fullwidth forms) and quote kana; both TTFs carry them, and a glyph the
# built-in lacks renders as nothing at all when no SD fallback font exists.
COMMON_INTERVALS = (
    "0x1100,0x11FF",  # Hangul Jamo
    "0x2100,0x218F",  # Letterlike symbols, number forms
    "0x2460,0x24FF",  # Enclosed alphanumerics
    "0x2500,0x27BF",  # Box drawing, blocks, shapes, misc symbols, dingbats
    "0x3000,0x303F",  # CJK punctuation
    "0x3040,0x30FF",  # Hiragana, katakana
    "0x3130,0x318F",  # Compatibility Jamo
    "0xAC00,0xD7A3",  # Full Hangul syllables
    "0xFF00,0xFFEF",  # Halfwidth and fullwidth forms
)


def generate(name: str, size: int, source: Path, extra_intervals=()) -> None:
    intervals = (*COMMON_INTERVALS, *extra_intervals)
    # Relative paths: the converter records its command line in the header.
    args = [sys.executable, str(CONVERTER.relative_to(ROOT)).replace("\\", "/"), name, str(size),
            str(source.relative_to(ROOT)).replace("\\", "/"), "--2bit", "--compress"]
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
