#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the launcher's fonts: Lexend as the bitmap atlases its font engine reads.

    render-fonts.py FONT_TTF OUTPUT_DIR

writes OUTPUT_DIR/Lexend-SIZE.fnt and .tga (the text's weight) and Lexend-Bold-SIZE.fnt and .tga
(titles, font-weight: bold) for each size the launcher loads (port/frontend/ui_host.cpp), and the
large title sizes in bold only (TITLE_SIZES). Each
.fnt is an AngelCode BMFont description in XML and each .tga its glyphs, white with their coverage
as alpha, in a 32-bit top-down TGA: what ProsperoEden's bitmap font engine reads, as it read its
Montserrat atlases. The glyphs are ASCII, Latin-1 (so game names such as "Pokémon" keep their
accents) and the typographic marks game names and the launcher's text use, with the font's
kerning between ASCII pairs.

The atlases are in the repository (port/frontend/ui/fonts), so a build needs no font tools; run
this again only to change the font, its weights or sizes. It needs Pillow with FreeType and Raqm
(Raqm applies the font's kerning). FONT_TTF is Lexend's variable font, tools/fonts/Lexend[wght].ttf
(SIL Open Font License 1.1, tools/fonts/OFL.txt).
"""

import math
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont, features

FAMILY = "Lexend"
SIZES = (20, 24, 28, 32, 36, 40, 48)
TITLE_SIZES = (56, 64, 72)  # bold only: the home screen's and a game's page's titles
# the variable font's weight axis: the text's, and the titles'
WEIGHTS = {"": 400, "-Bold": 600}
CHARACTERS = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) +
              [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x20AC, 0x2122])
ATLAS_WIDTH = 512
PADDING = 1


def font_at(path, size, weight):
    font = ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.RAQM)
    font.set_variation_by_axes([weight])
    return font


def metrics(font, size):
    """The line's ascent and height: the capitals centred in the line, as Montserrat's were (its
    ascent less its descent is its capital height)."""
    cap = -font.getbbox("H", anchor="ls")[1]
    descent = round(size * 0.21)
    base = round(cap) + descent
    return base, base + descent


def glyphs(font):
    out = []
    for code in CHARACTERS:
        ch = chr(code)
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        advance = round(font.getlength(ch))
        if x1 <= x0 or y1 <= y0:
            out.append((code, None, 0, 0, advance))
            continue
        image = Image.new("L", (x1 - x0, y1 - y0), 0)
        ImageDraw.Draw(image).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        out.append((code, image, x0, y0, advance))
    return out


def pack(items):
    """Shelves, tallest first: each glyph's place in an ATLAS_WIDTH-wide atlas, and its height."""
    order = sorted((i for i, g in enumerate(items) if g[1] is not None), key=lambda i: -items[i][1].height)
    places, x, y, shelf = {}, PADDING, PADDING, 0
    for i in order:
        w, h = items[i][1].size
        if x + w + PADDING > ATLAS_WIDTH:
            x, y, shelf = PADDING, y + shelf + PADDING, 0
        places[i] = (x, y)
        x += w + PADDING
        shelf = max(shelf, h)
    height = 1 << max(6, math.ceil(math.log2(y + shelf + PADDING)))
    return places, height


def kerning(font):
    ascii_chars = [chr(c) for c in range(0x21, 0x7F)]
    widths = {c: font.getlength(c) for c in ascii_chars}
    pairs = []
    for a in ascii_chars:
        for b in ascii_chars:
            amount = round(font.getlength(a + b) - widths[a] - widths[b])
            if amount:
                pairs.append((ord(a), ord(b), amount))
    return pairs


def write_tga(path, image):
    # white, with the coverage as alpha: BGRA, top-down (as ui_host.cpp reads it)
    alpha = image.tobytes()
    pixels = bytearray(len(alpha) * 4)
    pixels[0::4] = pixels[1::4] = pixels[2::4] = b"\xff" * len(alpha)
    pixels[3::4] = alpha
    with open(path, "wb") as file:
        file.write(struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, image.width, image.height, 32, 0x28) + pixels)


def render(path, output, size, suffix, weight):
    font = font_at(path, size, weight)
    base, line_height = metrics(font, size)
    items = glyphs(font)
    places, height = pack(items)
    atlas = Image.new("L", (ATLAS_WIDTH, height), 0)
    chars = []
    for i, (code, image, x0, y0, advance) in enumerate(items):
        if image is None:
            chars.append(f'    <char id="{code}" x="0" y="0" width="0" height="0" xoffset="0" yoffset="{base}" '
                         f'xadvance="{advance}" page="0" chnl="15"/>')
            continue
        x, y = places[i]
        atlas.paste(image, (x, y))
        chars.append(f'    <char id="{code}" x="{x}" y="{y}" width="{image.width}" height="{image.height}" '
                     f'xoffset="{x0}" yoffset="{base + y0}" xadvance="{advance}" page="0" chnl="15"/>')
    name = f"{FAMILY}{suffix}-{size}"
    write_tga(os.path.join(output, name + ".tga"), atlas)
    pairs = kerning(font)
    bold = 1 if suffix else 0
    lines = ['<?xml version="1.0"?>', "<font>",
             f'  <info face="{FAMILY}" size="{size}" bold="{bold}" italic="0" charset="" unicode="1" stretchH="100" '
             'smooth="1" aa="1" padding="0,0,0,0" spacing="1,1" outline="0"/>',
             f'  <common lineHeight="{line_height}" base="{base}" scaleW="{ATLAS_WIDTH}" scaleH="{height}" pages="1" '
             'packed="0" alphaChnl="0" redChnl="4" greenChnl="4" blueChnl="4"/>',
             "  <pages>", f'    <page id="0" file="{name}.tga"/>', "  </pages>",
             f'  <chars count="{len(chars)}">', *chars, "  </chars>",
             f'  <kernings count="{len(pairs)}">',
             *(f'    <kerning first="{a}" second="{b}" amount="{n}"/>' for a, b, n in pairs),
             "  </kernings>", "</font>", ""]
    with open(os.path.join(output, name + ".fnt"), "w", newline="\n") as file:
        file.write("\n".join(lines))
    return name, ATLAS_WIDTH, height, len(pairs)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    if not features.check("raqm"):
        sys.exit("Pillow needs Raqm for the font's kerning")
    path, output = sys.argv[1:3]
    os.makedirs(output, exist_ok=True)
    for size in SIZES:
        for suffix, weight in WEIGHTS.items():
            name, width, height, pairs = render(path, output, size, suffix, weight)
            print(f"{name}: {width}x{height}, {pairs} kerning pairs")
    for size in TITLE_SIZES:
        name, width, height, pairs = render(path, output, size, "-Bold", WEIGHTS["-Bold"])
        print(f"{name}: {width}x{height}, {pairs} kerning pairs")


if __name__ == "__main__":
    main()
