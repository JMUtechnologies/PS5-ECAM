#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# The consoles' marks (port/frontend/shell/consoles.cpp's DrawConsole): RetroArch's XMB Monochrome icons
# (libretro/retroarch-assets, CC BY 4.0, port/ui/CC-BY-4.0-RetroArch.txt), each console's as white on clear,
# at two sizes, the small one for the marks drawn small (no mipmaps on the GPU: a 256 picture drawn at 30
# would shimmer). Every pixel white, so a tint is the console's colour and its edges never darken. A side
# with no icon of its own in the set (the Switch 2, the Super Game Boy, the Satellaview, VS. System, the
# iQue Player) keeps its drawing.
#
#   git clone https://github.com/libretro/retroarch-assets && git -C retroarch-assets checkout COMMIT
#   tools/render-console-icons.py retroarch-assets port/ui/consoles
import os
import sys

from PIL import Image

COMMIT = "8c918e2872482139273374ef4cc6c7aff34d7585"  # the icons the committed ones are made from
SIZES = {"": 192, "-small": 64}
ICONS = {  # the file's name (registry.h's key, or the first four sides'), the icon in the set
    "wiiu": "Nintendo - Wii U",
    "switch": "Nintendo - Switch",
    "3ds": "Nintendo - Nintendo 3DS",
    "ds": "Nintendo - Nintendo DS",
    "gamecube": "Nintendo - GameCube",
    "wii": "Nintendo - Wii",
    "n64": "Nintendo - Nintendo 64",
    "nes": "Nintendo - Nintendo Entertainment System",
    "gb": "Nintendo - Game Boy",
    "gbc": "Nintendo - Game Boy Color",
    "gba": "Nintendo - Game Boy Advance",
    "snes": "Nintendo - Super Nintendo Entertainment System",
    "vb": "Nintendo - Virtual Boy",
    "pokemini": "Nintendo - Pokemon Mini",
    "fds": "Nintendo - Family Computer Disk System",
    "gw": "Nintendo - Game & Watch",
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__ or "render-console-icons.py RETROARCH_ASSETS OUTPUT_DIR")
    source, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for name, icon in ICONS.items():
        picture = Image.open(os.path.join(source, "xmb/monochrome/png", icon + ".png")).convert("RGBA")
        alpha = picture.getchannel("A")
        for suffix, size in SIZES.items():
            # the coverage shrunk alone (white everywhere), so the shrinking never mixes in a clear pixel's colour
            shrunk = alpha.resize((size, size), Image.LANCZOS)
            white = Image.new("RGBA", (size, size), (255, 255, 255, 0))
            white.putalpha(shrunk)
            white.save(os.path.join(out, name + suffix + ".png"), optimize=True)
    print(f"{len(ICONS)} icons from retroarch-assets {COMMIT[:7]} in {out}")


if __name__ == "__main__":
    main()
