#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ProsperoEden's launcher artwork and stylesheet in dark blue, with nothing but the standard library.

    recolour-ui.py PROSPEROEDEN_UI OUTPUT_UI

ProsperoEden's launcher is green: its panels and rows a dark green, the focused ones a lime to teal
gradient, its accents and greys leaning green. PS5Cemu's is dark blue to black instead, under the
Wii U Homebrew Launcher's blue:

  - chrome/: the panels, rows and buttons, drawn again from the SVGs they were made from (rounded
    rectangles with a fill, a gradient or not, and a 1-pixel stroke) with the colours in FILLS and
    STROKES: navy-black panels and rows, a deep blue gradient on the focused ones with a light blue
    outline, so the focus is as plain to see as before;
  - styles/app.rcss: each green colour (hue HUE_FROM to HUE_TO degrees) turns blue. Light ones,
    its text, keep their lightness (a little lighter: blue looks darker than green at the same
    lightness) so they read as well on the dark panels; dark ones, its backgrounds and dividers,
    get darker still. Its translucent colours get the alpha RmlUi reads (0 to 255, not 0 to 1),
    so the shades behind its screens and dialogs show;
  - icons/: recoloured the same way (the folders); the white ones stay white;
  - fonts/: as they are; the glyphs are white and take the text's colour.
"""

import colorsys
import math
import os
import re
import shutil
import struct
import sys

HUE_FROM, HUE_TO = 45.0, 190.0  # degrees: yellow-green to cyan
BLUE = 214.0                     # degrees
MIN_SATURATION = 0.04            # greys keep their faint tint

# ProsperoEden's chrome colours (RGB; the alpha stays) and PS5Cemu's
FILLS = {
    "a9db63": "2a63a6",  # focused: the gradient's start
    "245d4a": "0d2140",  # focused: its end
    "0b1713": "070d18",  # panels
    "17241e": "0d1828",  # rows
    "15231d": "0c1726",
    "111f1b": "0b1522",
    "102019": "0a1424",  # dropdown
    "16221d": "0b1424",  # the home screen's panel
    "e6ede4": "e6ecf4",  # translucent white (buttons, tiles)
    "eef7ed": "eef3fa",
    "d8e8aa": "8fb8e6",  # scrollbar
    "49624a": "26405e",
}
STROKES = {
    "a9db63": "5c9ce6",  # the focus outline
    "768e75": "34506f",
    "829a7b": "3a5878",
    "6a8267": "2a4462",
    "688267": "2a4462",
    "759078": "2e4866",
    "a6bf9b": "55779f",
    "d8e8aa": "8fb8e6",
    "49624a": "26405e",
}


def recolour(r, g, b):
    """A green colour as a blue one: light ones stay light, dark ones go darker."""
    h, l, s = colorsys.rgb_to_hls(r / 255.0, g / 255.0, b / 255.0)
    if s < MIN_SATURATION or not HUE_FROM <= h * 360.0 <= HUE_TO:
        return r, g, b
    if l >= 0.5:
        l += (1.0 - l) * 0.2
    else:
        l *= 0.8
        s = min(s * 1.2, 0.6)
    return tuple(round(c * 255.0) for c in colorsys.hls_to_rgb(BLUE / 360.0, l, s))


def write_tga(path, width, height, bgra):
    # uncompressed true colour, 32 bits, 8 of alpha, top-down: what the launcher reads (frontend/ui_host.cpp)
    with open(path, "wb") as file:
        file.write(struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 0x28) + bytes(bgra))


def parse_colour(value, table):
    """#rrggbb or #rrggbbaa as (r, g, b, a), its RGB swapped through table."""
    value = value.lstrip("#").lower()
    rgb = table.get(value[:6])
    if rgb is None:
        rgb = "%02x%02x%02x" % recolour(*(int(value[i:i + 2], 16) for i in (0, 2, 4)))
    alpha = int(value[6:8], 16) if len(value) == 8 else 255
    return tuple(int(rgb[i:i + 2], 16) for i in (0, 2, 4)) + (alpha,)


def render_svg(svg):
    """ProsperoEden's chrome SVGs: one rounded rectangle, filled with a colour or a horizontal
    gradient, with a stroke. Returns width, height and top-down BGRA pixels."""
    width = int(re.search(r'<svg[^>]*\bwidth="(\d+)"', svg).group(1))
    height = int(re.search(r'<svg[^>]*\bheight="(\d+)"', svg).group(1))
    rect = re.search(r"<rect([^>]*)/>", svg).group(1)
    attribute = lambda name, default=None: (re.search(rf'\b{name}="([^"]*)"', rect) or [None, default])[1]
    x, y, w, h = (float(attribute(n)) for n in ("x", "y", "width", "height"))
    radius = min(float(attribute("rx", "0")), w / 2, h / 2)  # as SVG clamps it
    stroke = parse_colour(attribute("stroke"), STROKES) if attribute("stroke") else None
    stroke_width = float(attribute("stroke-width", "1"))
    fill = attribute("fill")
    if fill.startswith("url("):
        stops = [parse_colour(c, FILLS) for c in re.findall(r'stop-color="([^"]+)"', svg)]
        gradient = (stops[0], stops[-1])
    else:
        gradient = (parse_colour(fill, FILLS),) * 2

    def fill_colour(px):
        t = min(max((px + 0.5 - x) / w, 0.0), 1.0)
        return tuple(a + (b - a) * t for a, b in zip(*gradient))

    def distance(px, py):
        # signed distance from the pixel's centre to the rectangle's edge, negative inside
        qx = abs(px + 0.5 - (x + w / 2)) - (w / 2 - radius)
        qy = abs(py + 0.5 - (y + h / 2)) - (h / 2 - radius)
        return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - radius

    def pixel(px, py):
        d = distance(px, py)
        r, g, b, a = fill_colour(px)
        fill_cover = min(max(0.5 - d, 0.0), 1.0)
        out_a = a / 255.0 * fill_cover
        out = [r * out_a, g * out_a, b * out_a]  # premultiplied while compositing
        if stroke:
            stroke_cover = min(max(0.5 + stroke_width / 2 - abs(d), 0.0), 1.0) * stroke[3] / 255.0
            out = [s * stroke_cover + o * (1.0 - stroke_cover) for s, o in zip(stroke, out)]
            out_a = stroke_cover + out_a * (1.0 - stroke_cover)
        if out_a <= 0.0:
            return b"\0\0\0\0"
        return bytes((round(out[2] / out_a), round(out[1] / out_a), round(out[0] / out_a), round(out_a * 255.0)))

    # Away from the edges every row is the same: work it out once, and the edges for each row.
    inner_left, inner_right = int(x + radius + 2), int(x + w - radius - 2)
    inner_rows = range(int(y + radius + 2), int(y + h - radius - 2))
    middle_row = int(y + h / 2)
    shared = b"".join(pixel(px, middle_row) for px in range(width))
    rows = []
    for py in range(height):
        if py in inner_rows:
            left = b"".join(pixel(px, py) for px in range(inner_left))
            right = b"".join(pixel(px, py) for px in range(inner_right, width))
            rows.append(left + shared[inner_left * 4:inner_right * 4] + right)
        else:
            rows.append(b"".join(pixel(px, py) for px in range(width)))
    return width, height, b"".join(rows)


def recolour_tga(source, target):
    data = bytearray(open(source, "rb").read())
    if data[2] != 2 or data[16] != 32:
        sys.exit(f"{source} is not an uncompressed 32-bit TGA")
    cache = {}
    for i in range(18 + data[0], len(data) - 3, 4):
        bgr = bytes(data[i:i + 3])
        out = cache.get(bgr)
        if out is None:
            r, g, b = recolour(bgr[2], bgr[1], bgr[0])
            out = cache[bgr] = bytes((b, g, r))
        data[i:i + 3] = out
    with open(target, "wb") as file:
        file.write(data)


def recolour_rcss(text):
    def hex_colour(match):
        value = match.group(1)
        r, g, b = recolour(*(int(value[i:i + 2], 16) for i in (0, 2, 4)))
        return "#%02x%02x%02x%s" % (r, g, b, value[6:])

    def rgba(match):
        r, g, b = recolour(*(int(v) for v in match.group(2, 3, 4)))
        alpha = match.group(5) or ""
        if "." in alpha:
            # RmlUi reads rgba()'s alpha as 0 to 255, like the other three, so ProsperoEden's
            # fractions (0.5) drew nothing: they become what they meant
            alpha = ", %d" % round(float(alpha.strip(" ,")) * 255.0)
        return f"{match.group(1)}({r}, {g}, {b}{alpha})"

    text = re.sub(r"#([0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?)\b", hex_colour, text)
    return re.sub(r"\b(rgba?)\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(\s*,[^)]*)?\)", rgba, text)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source, target = sys.argv[1:3]
    os.makedirs(os.path.join(target, "chrome"), exist_ok=True)
    for name in sorted(os.listdir(os.path.join(source, "chrome"))):
        if name.endswith(".svg"):
            with open(os.path.join(source, "chrome", name)) as file:
                width, height, pixels = render_svg(file.read())
            write_tga(os.path.join(target, "chrome", name[:-4] + ".tga"), width, height, pixels)
    os.makedirs(os.path.join(target, "icons"), exist_ok=True)
    for name in sorted(os.listdir(os.path.join(source, "icons"))):
        if name.endswith(".tga"):
            recolour_tga(os.path.join(source, "icons", name), os.path.join(target, "icons", name))
    shutil.copytree(os.path.join(source, "fonts"), os.path.join(target, "fonts"), dirs_exist_ok=True)
    os.makedirs(os.path.join(target, "styles"), exist_ok=True)
    with open(os.path.join(source, "styles", "app.rcss")) as file:
        text = file.read()
    with open(os.path.join(target, "styles", "app.rcss"), "w") as file:
        file.write(recolour_rcss(text))


if __name__ == "__main__":
    main()
