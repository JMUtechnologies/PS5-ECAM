#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ProsperoEden's launcher artwork and stylesheet in dark blue and in yellow, with nothing but the
standard library.

    recolour-ui.py PROSPEROEDEN_UI PS5CEMU_RCSS OUTPUT_UI

ProsperoEden's launcher is green: its panels and rows a dark green, the focused ones a lime to teal
gradient, its accents and greys leaning green. PS5CEMU-HAR has two: Cemu's, dark blue to black
under the Wii U Homebrew Launcher's blue, and Azahar's, the same in a warm black and gold under
the 3DS Homebrew Launcher's wave, made yellow. Each theme (THEMES) writes:

  - chrome/ (chrome-3ds/): the panels, rows and buttons, drawn again from the SVGs they were made
    from (rounded rectangles with a fill, a gradient or not, and a stroke), and PS5CEMU-HAR's own
    in the chrome folder beside its stylesheet, with the theme's
    fills and strokes: dark panels and rows, a deep gradient on the focused ones with a light
    outline, so the focus is as plain to see as before. They take PS5CEMU-HAR's shapes, not
    ProsperoEden's (RADII, MARKED, UNDERLINED, GLASS): rounder corners, pill buttons, see-through
    panels with light rows on them, the focus's gradient from top to bottom, an accent bar on
    focused rows and an underline in the menu;
  - styles/app.rcss (app-3ds.rcss): each colour in the theme's source hues turns to its hue. Light
    ones, its text, keep their lightness (a little lighter: blue and gold look darker than green
    at the same lightness) so they read as well on the dark panels; dark ones, its backgrounds and
    dividers, get darker still. Its translucent colours get the alpha RmlUi reads (0 to 255, not
    0 to 1), so the shades behind its screens and dialogs show;
  - styles/ps5cemu.rcss (ps5cemu-3ds.rcss): PS5CEMU-HAR's own stylesheet, which is written in the
    blue: as it is (in gold);
  - icons/ (icons-3ds/): recoloured the same way (the folders); the white ones stay white;
and fonts/: PS5CEMU-HAR's own, Lexend's atlases beside its stylesheet (tools/render-fonts.py), in
place of ProsperoEden's Montserrat. The glyphs are white and take the text's colour. And sounds/:
the launcher's music and menu sounds, also beside the stylesheet (tools/render-sounds.py).
"""

import colorsys
import math
import os
import re
import shutil
import struct
import sys

MIN_SATURATION = 0.04  # greys keep their faint tint
GREEN = (45.0, 190.0)  # degrees: yellow-green to cyan, ProsperoEden's
BLUE = (195.0, 240.0)  # PS5CEMU-HAR's own stylesheet's

# ProsperoEden's chrome colours (RGB; the alpha stays) in each theme
BLUE_FILLS = {
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
BLUE_STROKES = {
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
YELLOW_FILLS = {
    "a9db63": "9c6e0c",  # focused: the gradient's start
    "245d4a": "3b2905",  # focused: its end
    "0b1713": "130e05",  # panels
    "17241e": "21180a",  # rows
    "15231d": "1f1709",
    "111f1b": "1b1408",
    "102019": "1d1608",  # dropdown
    "16221d": "1c1508",  # the home screen's panel
    "e6ede4": "f4efe4",  # translucent white (buttons, tiles)
    "eef7ed": "faf5ea",
    "d8e8aa": "f0cf6a",  # scrollbar
    "49624a": "5e4a1c",
    "5c9ce6": "f2c14e",  # PS5CEMU-HAR's own accent, blue already (chrome/title-bar.svg)
}
YELLOW_STROKES = {
    "a9db63": "f2c14e",  # the focus outline
    "768e75": "6e5a2c",
    "829a7b": "7a6532",
    "6a8267": "5c4a22",
    "688267": "5c4a22",
    "759078": "655027",
    "a6bf9b": "a88f52",
    "d8e8aa": "f0cf6a",
    "49624a": "5e4a1c",
}

# PS5CEMU-HAR's shapes for that chrome, so the launcher is more than ProsperoEden's look recoloured:
# rounder panels and rows, pill-shaped buttons, and a focus filled from top to bottom with a firmer
# outline. Focused rows and tiles also get an accent bar at their left edge, and the menu's focus
# is an underline instead of a box. Corner radii by the chrome's name, without -normal/-focused:
RADII = {
    "library-row": 18, "dialog-row": 20, "dropdown-panel": 20, "recent-tile": 20, "action": 24, "tile": 20,
    "hero-panel": 28, "last-played-card": 24, "dialog-panel": 32, "modal-panel": 32, "start-panel": 32,
    "hero-button": 36,  # half the height or more: a pill
    "cover-frame": 28, "recent-square": 22, "feature-chip": 32,  # PS5CEMU-HAR's own home screen
}
MARKED = {"library-row-focused", "dialog-row-focused", "recent-tile-focused", "action-focused"}
# Glass where ProsperoEden has dark slabs: the pages' panels let the bubbles and waves show through,
# and the rows on them are a light tint with a fine light edge. Fills and strokes by the chrome's name.
GLASS = {
    "dialog-panel": ("#0b1713a0", "#ffffff26"),
    "hero-panel": ("#0b171388", "#ffffff26"),
    "library-row-normal": ("#ffffff12", "#ffffff1f"),
    "dialog-row-normal": ("#ffffff12", "#ffffff1f"),
    "action-normal": ("#ffffff12", "#ffffff1f"),
    "tile-normal": ("#ffffff12", "#ffffff1f"),  # PS5CEMU-HAR's own: a game's tiles
    "cover-frame": ("#0b171388", "#ffffff2e"),  # and the home screen's
    "recent-square-normal": ("#ffffff10", "#ffffff1f"),
    "feature-chip-normal": ("#ffffff10", "#ffffff24"),
}
# the underline: from x to x, its top, in the chrome's pixels (the menu's labels are centred, the
# home screen's link to the full library is right-aligned at 280)
UNDERLINED = {"nav-focused": (46, 90, 50), "view-all-focused": (200, 280, 35)}

# name: (hue, the hues it replaces, fills, strokes, the suffix of its folders and files)
THEMES = {
    "blue": (214.0, (GREEN,), BLUE_FILLS, BLUE_STROKES, ""),
    "yellow": (44.0, (GREEN, BLUE), YELLOW_FILLS, YELLOW_STROKES, "-3ds"),
}


def recolour(r, g, b, theme="blue"):
    """A colour in the theme's source hues in its own: light ones stay light, dark ones go darker."""
    hue, sources = THEMES[theme][:2]
    h, l, s = colorsys.rgb_to_hls(r / 255.0, g / 255.0, b / 255.0)
    if s < MIN_SATURATION or not any(low <= h * 360.0 <= high for low, high in sources):
        return r, g, b
    if l >= 0.5:
        l += (1.0 - l) * 0.2
    else:
        l *= 0.8
        s = min(s * 1.2, 0.6)
    return tuple(round(c * 255.0) for c in colorsys.hls_to_rgb(hue / 360.0, l, s))


def write_tga(path, width, height, bgra):
    # uncompressed true colour, 32 bits, 8 of alpha, top-down: what the launcher reads (frontend/ui_host.cpp)
    with open(path, "wb") as file:
        file.write(struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 0x28) + bytes(bgra))


def parse_colour(value, table, theme):
    """#rrggbb or #rrggbbaa as (r, g, b, a), its RGB swapped through table."""
    value = value.lstrip("#").lower()
    rgb = table.get(value[:6])
    if rgb is None:
        rgb = "%02x%02x%02x" % recolour(*(int(value[i:i + 2], 16) for i in (0, 2, 4)), theme)
    alpha = int(value[6:8], 16) if len(value) == 8 else 255
    return tuple(int(rgb[i:i + 2], 16) for i in (0, 2, 4)) + (alpha,)


def composite_bar(pixels, width, bx, by, bw, bh, colour):
    """A bar with round ends, in colour (RGBA), over top-down BGRA pixels."""
    height = len(pixels) // (4 * width)
    r = min(bw, bh) / 2
    for py in range(max(int(by) - 1, 0), min(int(math.ceil(by + bh)) + 1, height)):
        for px in range(max(int(bx) - 1, 0), min(int(math.ceil(bx + bw)) + 1, width)):
            qx = abs(px + 0.5 - (bx + bw / 2)) - (bw / 2 - r)
            qy = abs(py + 0.5 - (by + bh / 2)) - (bh / 2 - r)
            d = math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r
            cover = min(max(0.5 - d, 0.0), 1.0) * colour[3] / 255.0
            if cover <= 0.0:
                continue
            o = (py * width + px) * 4
            under = pixels[o + 3] / 255.0 * (1.0 - cover)
            out_a = cover + under
            for i, c in enumerate((colour[2], colour[1], colour[0])):
                pixels[o + i] = round((c * cover + pixels[o + i] * under) / out_a)
            pixels[o + 3] = round(out_a * 255.0)


def render_svg(svg, theme, name=""):
    """ProsperoEden's chrome SVGs: one rounded rectangle, filled with a colour or a gradient, with a
    stroke, in PS5CEMU-HAR's shapes and glass (RADII, MARKED, UNDERLINED, GLASS) when name is one of
    them. Returns width, height and top-down BGRA pixels."""
    fills, strokes = THEMES[theme][2:4]
    stem = os.path.splitext(name)[0]
    accent = parse_colour("#a9db63", strokes, theme)[:3] + (255,)  # the focus outline's colour
    width = int(re.search(r'<svg[^>]*\bwidth="(\d+)"', svg).group(1))
    height = int(re.search(r'<svg[^>]*\bheight="(\d+)"', svg).group(1))
    rect = re.search(r"<rect([^>]*)/>", svg).group(1)
    attribute = lambda name, default=None: (re.search(rf'\b{name}="([^"]*)"', rect) or [None, default])[1]
    if stem in UNDERLINED:
        left, right, top = UNDERLINED[stem]
        pixels = bytearray(width * height * 4)
        composite_bar(pixels, width, left, top, right - left, 4, accent)
        return width, height, bytes(pixels)
    x, y, w, h = (float(attribute(n)) for n in ("x", "y", "width", "height"))
    radius = float(RADII.get(re.sub(r"-(normal|focused)$", "", stem), attribute("rx", "0")))
    radius = min(radius, w / 2, h / 2)  # as SVG clamps it
    glass = GLASS.get(stem)
    stroke = attribute("stroke") if not glass else glass[1]
    stroke = parse_colour(stroke, strokes, theme) if stroke else None
    stroke_width = float(attribute("stroke-width", "1"))
    fill = attribute("fill") if not glass else glass[0]
    # ProsperoEden's gradients, its focus, run left to right; these run top to bottom, with a 2-pixel outline
    vertical = fill.startswith("url(")
    if vertical:
        stops = [parse_colour(c, fills, theme) for c in re.findall(r'stop-color="([^"]+)"', svg)]
        gradient = (stops[0], stops[-1])
        stroke_width = max(stroke_width, 2.0)
    else:
        gradient = (parse_colour(fill, fills, theme),) * 2

    def fill_colour(px, py):
        t = (py + 0.5 - y) / h if vertical else (px + 0.5 - x) / w
        t = min(max(t, 0.0), 1.0)
        return tuple(a + (b - a) * t for a, b in zip(*gradient))

    def distance(px, py):
        # signed distance from the pixel's centre to the rectangle's edge, negative inside
        qx = abs(px + 0.5 - (x + w / 2)) - (w / 2 - radius)
        qy = abs(py + 0.5 - (y + h / 2)) - (h / 2 - radius)
        return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - radius

    def pixel(px, py):
        d = distance(px, py)
        r, g, b, a = fill_colour(px, py)
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

    # Away from the edges a row's pixels are all alike (and, with a horizontal gradient, every row
    # is the same): work them out once, and the edges for each row.
    inner_left, inner_right = int(x + radius + 2), int(x + w - radius - 2)
    inner_rows = range(int(y + radius + 2), int(y + h - radius - 2)) if inner_left < inner_right else range(0)
    middle_row = int(y + h / 2)
    shared = b"".join(pixel(px, middle_row) for px in range(width))
    rows = []
    for py in range(height):
        if py in inner_rows:
            left = b"".join(pixel(px, py) for px in range(inner_left))
            right = b"".join(pixel(px, py) for px in range(inner_right, width))
            middle = pixel(inner_left, py) * (inner_right - inner_left) if vertical else shared[inner_left * 4:inner_right * 4]
            rows.append(left + middle + right)
        else:
            rows.append(b"".join(pixel(px, py) for px in range(width)))
    pixels = bytearray(b"".join(rows))
    if stem in MARKED:
        # the accent bar, inside the left edge, over the middle half of the height
        composite_bar(pixels, width, x + 6, y + h / 4, 5, h / 2, accent)
    return width, height, bytes(pixels)


def recolour_tga(source, target, theme):
    data = bytearray(open(source, "rb").read())
    if data[2] != 2 or data[16] != 32:
        sys.exit(f"{source} is not an uncompressed 32-bit TGA")
    cache = {}
    for i in range(18 + data[0], len(data) - 3, 4):
        bgr = bytes(data[i:i + 3])
        out = cache.get(bgr)
        if out is None:
            r, g, b = recolour(bgr[2], bgr[1], bgr[0], theme)
            out = cache[bgr] = bytes((b, g, r))
        data[i:i + 3] = out
    with open(target, "wb") as file:
        file.write(data)


def recolour_rcss(text, theme):
    def hex_colour(match):
        value = match.group(1)
        r, g, b = recolour(*(int(value[i:i + 2], 16) for i in (0, 2, 4)), theme)
        return "#%02x%02x%02x%s" % (r, g, b, value[6:])

    def rgba(match):
        r, g, b = recolour(*(int(v) for v in match.group(2, 3, 4)), theme)
        alpha = match.group(5) or ""
        if "." in alpha:
            # RmlUi reads rgba()'s alpha as 0 to 255, like the other three, so ProsperoEden's
            # fractions (0.5) drew nothing: they become what they meant
            alpha = ", %d" % round(float(alpha.strip(" ,")) * 255.0)
        return f"{match.group(1)}({r}, {g}, {b}{alpha})"

    text = re.sub(r"#([0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?)\b", hex_colour, text)
    return re.sub(r"\b(rgba?)\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(\s*,[^)]*)?\)", rgba, text)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    source, own, target = sys.argv[1:4]
    with open(os.path.join(source, "styles", "app.rcss")) as file:
        # in PS5CEMU-HAR's font, Lexend (fonts/, tools/render-fonts.py), not ProsperoEden's Montserrat
        app = file.read().replace('font-family: "Montserrat"', 'font-family: "Lexend"')
    with open(own) as file:
        ps5cemu = file.read()
    os.makedirs(os.path.join(target, "styles"), exist_ok=True)
    # ProsperoEden's chrome, and PS5CEMU-HAR's own beside its stylesheet (in ProsperoEden's colours)
    svgs = [os.path.join(source, "chrome", name) for name in sorted(os.listdir(os.path.join(source, "chrome")))]
    own_chrome = os.path.join(os.path.dirname(own), "chrome")
    if os.path.isdir(own_chrome):
        svgs += [os.path.join(own_chrome, name) for name in sorted(os.listdir(own_chrome))]
    for theme, (_, _, _, _, suffix) in THEMES.items():
        os.makedirs(os.path.join(target, "chrome" + suffix), exist_ok=True)
        for svg in svgs:
            if svg.endswith(".svg"):
                with open(svg) as file:
                    width, height, pixels = render_svg(file.read(), theme, os.path.basename(svg))
                write_tga(os.path.join(target, "chrome" + suffix, os.path.basename(svg)[:-4] + ".tga"), width, height, pixels)
        os.makedirs(os.path.join(target, "icons" + suffix), exist_ok=True)
        for name in sorted(os.listdir(os.path.join(source, "icons"))):
            if name.endswith(".tga"):
                recolour_tga(os.path.join(source, "icons", name), os.path.join(target, "icons" + suffix, name), theme)
        with open(os.path.join(target, "styles", f"app{suffix}.rcss"), "w") as file:
            file.write(recolour_rcss(app, theme))
        # PS5CEMU-HAR's own is written in the blue already
        with open(os.path.join(target, "styles", f"ps5cemu{suffix}.rcss"), "w") as file:
            file.write(ps5cemu if theme == "blue" else recolour_rcss(ps5cemu, theme))
    for folder in ("fonts", "sounds"):
        shutil.copytree(os.path.join(os.path.dirname(own), folder), os.path.join(target, folder), dirs_exist_ok=True)


if __name__ == "__main__":
    main()
