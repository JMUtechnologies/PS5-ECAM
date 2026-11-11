#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Give the systems a shipped ES-DE theme has no art for the art of their nearest system.

    tools/add-esde-switch2-theme-alias.py <themes folder>     (tools/build-esde.sh)

Switch 2 takes the Switch's art in every theme (no theme has its own yet); where a theme's folder lacks
one of the others, it takes the first of its stand-ins that the folder has: the Super Game Boy the Game
Boy's, the arcade (the VS. System's system) MAME's or FBNeo's, Game & Watch the LCD games'. A folder's
own art is never replaced. tools/check-esde-themes.py then checks every system has some.
"""
import pathlib
import shutil
import sys

# system: its stand-ins, in order
ALIASES = {
    "switch2": ["switch"],
    "sgb": ["gb"],
    "arcade": ["mame", "fbneo", "fba", "neogeo"],
    "gameandwatch": ["lcdgames", "gw"],
}


def entries(folder):
    """a folder's entries by stem (the name before its first dot), lowercase"""
    named = {}
    for entry in folder.iterdir():
        named.setdefault(entry.name.split(".", 1)[0].lower(), []).append(entry)
    return named


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    themes = pathlib.Path(sys.argv[1])
    for theme in themes.iterdir():
        if not theme.is_dir():
            continue
        # deepest first, so a copied folder is not walked again
        for folder in sorted((path for path in theme.rglob("*") if path.is_dir()), key=lambda p: len(p.parts), reverse=True):
            named = entries(folder)
            for system, stand_ins in ALIASES.items():
                if system in named:
                    continue
                source_stem = next((stem for stem in stand_ins if stem in named), None)
                if source_stem is None:
                    continue
                for source in named[source_stem]:
                    target = folder / (system + source.name[len(source_stem):])
                    if target.exists():
                        continue
                    if source.is_dir():
                        shutil.copytree(source, target)
                    elif source.is_file():
                        shutil.copy2(source, target)


if __name__ == "__main__":
    main()
