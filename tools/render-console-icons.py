#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# The consoles' marks (port/frontend/shell/consoles.cpp's DrawConsole): RetroArch's XMB Monochrome icons
# (libretro/retroarch-assets, CC BY 4.0, port/ui/CC-BY-4.0-RetroArch.txt), each console's as white on clear,
# at two sizes, the small one for the marks drawn small (no mipmaps on the GPU: a 256 picture drawn at 30
# would shimmer). Every pixel white, so a tint is the console's colour and its edges never darken. The
# Switch 2's is the Switch's with a raised 2 at its top right, as a square is written (Lexend's, the app's
# type); a side with no icon of its own in the set (the Super Game Boy, the Satellaview, VS. System, the
# iQue Player) keeps its drawing.
#
#   git clone https://github.com/libretro/retroarch-assets && git -C retroarch-assets checkout COMMIT
#   tools/render-console-icons.py retroarch-assets port/ui/consoles
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFont

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
# the Switch 2's: the Switch's, smaller and to the left, the 2 raised beside its top right corner
FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "port", "ui", "fonts", "Lexend-SemiBold.ttf")


def squared(alpha):
    """The Switch's coverage (alpha) as the Switch 2's: the console at 82 %, the 2 at 60 % of its height
    above its right end, the two together where the Switch was in the square."""
    n = alpha.width
    left, top, right, bottom = alpha.getbbox()
    shape = alpha.crop((left, top, right, bottom))
    scale = 0.82
    shape = shape.resize((round(shape.width * scale), round(shape.height * scale)), Image.LANCZOS)
    # the 2, drawn large and shrunk to its height (the coverage alone, as the icons are)
    digit_height = round(shape.height * 0.6)
    font = ImageFont.truetype(FONT, 400)
    box = font.getbbox("2")
    drawn = Image.new("L", (box[2] - box[0] + 20, box[3] - box[1] + 20), 0)
    ImageDraw.Draw(drawn).text((10 - box[0], 10 - box[1]), "2", font=font, fill=255)
    drawn = drawn.crop(drawn.getbbox())
    digit = drawn.resize((round(drawn.width * digit_height / drawn.height), digit_height), Image.LANCZOS)
    gap = round(n * 0.025)
    # its foot a little under the console's top: raised, as a square is
    rise = round(digit_height * 0.55)
    width = shape.width + gap + digit.width
    height = shape.height + rise
    x = left + ((right - left) - width) // 2
    y = (top + bottom) // 2 - height // 2
    console = Image.new("L", (n, n), 0)
    console.paste(shape, (x, y + rise))
    two = Image.new("L", (n, n), 0)
    two.paste(digit, (x + shape.width + gap, y))
    return ImageChops.lighter(console, two)


COMPOSED = {"switch2": ("switch", squared)}


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__ or "render-console-icons.py RETROARCH_ASSETS OUTPUT_DIR")
    source, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    alphas = {}
    for name, icon in ICONS.items():
        picture = Image.open(os.path.join(source, "xmb/monochrome/png", icon + ".png")).convert("RGBA")
        alphas[name] = picture.getchannel("A")
    for name, (base, make) in COMPOSED.items():
        alphas[name] = make(alphas[base])
    for name, alpha in alphas.items():
        for suffix, size in SIZES.items():
            # the coverage shrunk alone (white everywhere), so the shrinking never mixes in a clear pixel's colour
            shrunk = alpha.resize((size, size), Image.LANCZOS)
            white = Image.new("RGBA", (size, size), (255, 255, 255, 0))
            white.putalpha(shrunk)
            white.save(os.path.join(out, name + suffix + ".png"), optimize=True)
    print(f"{len(alphas)} icons from retroarch-assets {COMMIT[:7]} in {out}")


if __name__ == "__main__":
    main()
