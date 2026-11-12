#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The games' manuals, as Nintendo keeps them online: a table of each manual's address for the Timeline's
museum label and the Game hub, which show it as a QR code to open on a phone.

    render-manuals.py [OUTPUT]       default: port/app/gametdb/manuals.tsv.gz

Reads Nintendo's own pages of manuals and writes one line a game, sorted, its fields separated by tabs:

    system  name  title  address

system is nes, snes or wiiu; name the title as ps5gameinfo matches names (render-carts.py's words: lower
case, letters and digits run together, what is in brackets and "the", "of", "and", "a", "version",
"edition" left out); title Nintendo's; address the PDF's. The sources: the NES Classic Edition's manuals
(each game's printed booklet, www.nintendo.co.jp/clv/manuals/en), the Super NES Classic Edition's
(www.nintendo.co.jp/clvs/manuals/en_us) and Nintendo UK's list of Wii U games' electronic manuals. Only the
addresses are committed; the manuals stay Nintendo's, on Nintendo's servers. Needs only Python's standard
library.
"""

import gzip
import html
import importlib.util
import os
import re
import sys
import urllib.parse
import urllib.request

AGENT = {"User-Agent": "PS5-ECAM/6.0"}
SOURCES = [
    ("nes", "https://www.nintendo.co.jp/clv/manuals/en/index.html"),
    ("snes", "https://www.nintendo.co.jp/clvs/manuals/en_us/index.html"),
    ("wiiu", "https://www.nintendo.com/en-gb/Support/Legacy-system/Wii-U-games-electronic-manuals-850950.html"),
]


def page(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT)) as response:
        return response.read().decode("utf-8", "replace")


def title(text):
    text = html.unescape(text)
    for mark in ("™", "®", "©"):
        text = text.replace(mark, "")
    return " ".join(text.replace("’", "'").split())


def classics(system, url, text):
    """The Classic Editions' pages: each game's box art (its name in alt), then its manuals; a game's printed
    booklet (the PDF without "_en") first."""
    found = []
    for match in re.finditer(r'alt="([^"]+)"(.*?)(?=alt="|$)', text, re.S):
        pdfs = re.findall(r'href="([^"]+\.pdf)"', match.group(2))
        if not pdfs or "CLV-P-" not in pdfs[0]:
            continue  # the console's own manuals
        printed = [p for p in pdfs if not p.endswith("_en.pdf")]
        found.append((title(match.group(1)), urllib.parse.urljoin(url, (printed or pdfs)[0])))
    return found


def wiiu(text):
    found = []
    for address, name in re.findall(r'<a href="([^"]+ElectronicManual[^"]+_EN\.pdf)"[^>]*>([^<]+?)\s*-\s*Manual', text):
        found.append((title(name), address))
    return found


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "port/app/gametdb/manuals.tsv.gz")
    spec = importlib.util.spec_from_file_location("render_carts", os.path.join(root, "tools/render-carts.py"))
    carts = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(carts)
    lines = {}
    for system, url in SOURCES:
        text = page(url)
        found = wiiu(text) if system == "wiiu" else classics(system, url, text)
        print(f"{system}: {len(found)} manuals", file=sys.stderr)
        for name, address in found:
            key = carts.words(name)
            if len(key) >= 2 and len(address) <= 200:  # a QR code holds 213 bytes
                lines.setdefault((system, key), f"{system}\t{key}\t{name}\t{address}")
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with gzip.open(output, "wt", encoding="utf-8", compresslevel=9) as out:
        for key in sorted(lines):
            out.write(lines[key] + "\n")
    print(f"{output}: {len(lines)} manuals", file=sys.stderr)


if __name__ == "__main__":
    main()
