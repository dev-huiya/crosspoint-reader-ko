"""Copy X4 Pro panel geometry from the FreeInk board profile for host builds."""

import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text(encoding="utf-8")
start = source.index("constexpr BoardProfile XTEINK_X4_PRO = {")
end = source.index("};", start)
profile = source[start:end]
size = re.search(r"\n\s*(\d+),\s*\n\s*(\d+),", profile)
insets = re.search(r"\{(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\},\s*\n\s*true$", profile)
if not size or not insets:
    raise SystemExit("Could not read X4 Pro geometry from BoardConfig.h")
width, height = map(int, size.groups())
top, right, bottom, left = map(int, insets.groups())
Path(sys.argv[2]).write_text(
    "#pragma once\n"
    f"inline constexpr int DESKTOP_PANEL_WIDTH = {width};\n"
    f"inline constexpr int DESKTOP_PANEL_HEIGHT = {height};\n"
    f"inline constexpr int DESKTOP_INSET_TOP = {top};\n"
    f"inline constexpr int DESKTOP_INSET_RIGHT = {right};\n"
    f"inline constexpr int DESKTOP_INSET_BOTTOM = {bottom};\n"
    f"inline constexpr int DESKTOP_INSET_LEFT = {left};\n",
    encoding="utf-8",
)
