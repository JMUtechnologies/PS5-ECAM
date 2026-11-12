#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The retro sides' cartridges for the Timeline: each NES, Super NES, Game Boy, Game Boy Color, Game Boy
Advance, Nintendo 64, Virtual Boy and Pokemon mini game's year, publisher and the pictures of its cartridge
in each region it came out in, and each Game & Watch game's own unit, from the LaunchBox Games Database.

    render-carts.py [OUTPUT] [--from Metadata.zip]       default: port/app/gametdb/launchbox-carts.tsv.gz

Downloads the database (gamesdb.launchbox-app.com/Metadata.zip, about 110 MB), or reads it from the file
--from names, and writes one line a name of a game (its own and its other names), sorted, its fields
separated by tabs:

    system  name  year  publisher  carts

system is nes, snes, gb, gbc, gba, n64, vb, mini or gw; name the title as boxart.cpp's Words match names (lower
case, letters and digits run together, what is in brackets and "the", "of", "and", "a", "version", "edition"
left out); carts the pictures as region=file, separated by commas, the region one letter (U America, J Japan,
E Europe, W the world, O elsewhere) and the file the picture's name at images.launchbox-app.com without its
".png". Only "Cart - Front" pictures (the cartridges' own photographs and scans, not fan art). The file is
committed, so a build needs no network; the pictures are fetched by the app when the Timeline shows them.
The information and pictures are the LaunchBox Games Database's and its contributors'
(https://gamesdb.launchbox-app.com), which the app credits. Needs only Python's standard library.
"""

import gzip
import io
import os
import sys
import urllib.request
import xml.etree.ElementTree as ElementTree
import zipfile

URL = "https://gamesdb.launchbox-app.com/Metadata.zip"
PLATFORMS = {
    "Nintendo Entertainment System": "nes",
    "Super Nintendo Entertainment System": "snes",
    "Nintendo Game Boy": "gb",
    "Nintendo Game Boy Color": "gbc",
    "Nintendo Game Boy Advance": "gba",
    "Nintendo 64": "n64",
    "Nintendo Virtual Boy": "vb",
    "Nintendo Pokemon Mini": "mini",
    "Nintendo Game & Watch": "gw",
}
REGIONS = {
    "North America": "U", "United States": "U", "Canada": "U",
    "Japan": "J",
    "Europe": "E", "United Kingdom": "E", "Germany": "E", "France": "E", "Spain": "E", "Italy": "E",
    "The Netherlands": "E", "Sweden": "E", "Scandinavia": "E", "Nordic": "E", "Greece": "E", "Finland": "E", "Norway": "E",
    "Denmark": "E", "Russia": "E", "Poland": "E",
    "World": "W",
}
LEFT_OUT = {"the", "version", "edition", "a", "and", "of"}


def words(name):
    """boxart.cpp's Words, run together: as the app matches a game file's name."""
    for old, new in (("é", "e"), ("É", "e"), ("&", " and ")):
        name = name.replace(old, new)
    out, word, depth = [], "", 0

    def flush():
        nonlocal word
        if word and word not in LEFT_OUT:
            out.append(word)
        word = ""

    for byte in name.encode("utf-8"):
        c = chr(byte)
        if c in "([":
            flush()
            depth += 1
        elif c in ")]" and depth:
            depth -= 1
        elif not depth and byte < 0x80 and c.isalnum():
            word += c.lower()
        else:
            flush()
    flush()
    return "".join(out)


def clean(value):
    return " ".join((value or "").replace("\t", " ").split())


def main():
    args = sys.argv[1:]
    source = None
    if "--from" in args:
        at = args.index("--from")
        source = args[at + 1]
        del args[at:at + 2]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output = args[0] if args else os.path.join(root, "port/app/gametdb/launchbox-carts.tsv.gz")
    if source:
        archive = zipfile.ZipFile(source)
    else:
        print(f"downloading {URL}", file=sys.stderr)
        with urllib.request.urlopen(URL) as response:
            archive = zipfile.ZipFile(io.BytesIO(response.read()))

    games, names, carts = {}, {}, {}
    with archive.open("Metadata.xml") as xml:
        for _, element in ElementTree.iterparse(xml):
            tag = element.tag
            if tag == "Game":
                system = PLATFORMS.get(element.findtext("Platform") or "")
                if system:
                    date = element.findtext("ReleaseDate") or ""
                    year = date[:4] if date[:4].isdigit() else (element.findtext("ReleaseYear") or "")
                    games[element.findtext("DatabaseID")] = (system, element.findtext("Name") or "", year, clean(element.findtext("Publisher")))
                element.clear()
            elif tag == "GameAlternateName":
                names.setdefault(element.findtext("DatabaseID"), []).append(element.findtext("AlternateName") or "")
                element.clear()
            elif tag == "GameImage":
                if element.findtext("Type") == "Cart - Front":
                    region = REGIONS.get(element.findtext("Region") or "", "O") if element.findtext("Region") else "W"
                    file = element.findtext("FileName") or ""
                    if file.endswith(".png"):
                        carts.setdefault(element.findtext("DatabaseID"), {}).setdefault(region, file[:-4])
                element.clear()
            elif tag not in ("LaunchBox",):
                pass

    lines = set()
    kept = 0
    for game_id, (system, name, year, publisher) in games.items():
        own = carts.get(game_id, {})
        if not own and not year:
            continue
        kept += 1
        listed = ",".join(f"{region}={file}" for region, file in sorted(own.items()))
        for title in [name] + names.get(game_id, []):
            key = words(title)
            if len(key) >= 2:
                lines.add(f"{system}\t{key}\t{year}\t{publisher}\t{listed}")
    # a name two games share (a remake's, a region's other title): the one with pictures, then the older
    best = {}
    for line in sorted(lines):
        system, key, year, publisher, listed = line.split("\t")
        held = best.get((system, key))
        if held is None or (bool(listed), -(int(year) if year else 9999)) > (bool(held[3]), -(int(held[1]) if held[1] else 9999)):
            best[(system, key)] = (key, year, publisher, listed)
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with gzip.open(output, "wt", encoding="utf-8", compresslevel=9) as out:
        for (system, key), (_, year, publisher, listed) in sorted(best.items()):
            out.write(f"{system}\t{key}\t{year}\t{publisher}\t{listed}\n")
    pictures = sum(len(v) for k, v in carts.items() if k in games)
    print(f"{output}: {kept} games, {len(best)} names, {pictures} cartridges", file=sys.stderr)


if __name__ == "__main__":
    main()
