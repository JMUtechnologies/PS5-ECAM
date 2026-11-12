#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Put PS5-ECAM's site together for GitHub Pages, with nothing but the standard library.

    site/build.py [--release RELEASE_JSON] [--out DIR]

Copies site/'s pages into DIR (default _site) with the app's own art beside them: Lexend from
port/ui/fonts, the home screen tile (sce_sys/icon0.png) and the README's banner and PayPal code
(docs/). RELEASE_JSON is the latest release as GitHub's API gives it (gh api
repos/OWNER/REPO/releases/latest); it goes into index.html, so the page shows that release's notes
at once and still has them when the API turns a reader away. The page asks the API for the latest
release as it opens (app.js).

With --compat, the report intake's table goes beside compatibility.html (compatibility.json), and with
the owner's statuses (tools/compat-list.json) it makes status.json: how each game runs, which the console
fetches as the app starts (tools/compat_feed.py, port/app/compatibility.h).
"""

import argparse
import html
import json
import os
import re
import shutil
import sys

SITE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SITE)
REPO = "premohq/PlayTendoh"
ART = {  # the repository's own files the page uses, and their names in it
    "port/ui/fonts/Lexend-Regular.ttf": "fonts/Lexend-Regular.ttf",
    "port/ui/fonts/Lexend-Medium.ttf": "fonts/Lexend-Medium.ttf",
    "port/ui/fonts/Lexend-SemiBold.ttf": "fonts/Lexend-SemiBold.ttf",
    "sce_sys/icon0.png": "icon.png",
    "docs/banner.png": "banner.png",
    "docs/support-qr.svg": "support-qr.svg",
}
SKIP = {"build.py", "__pycache__"}

sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))
import compat_feed  # noqa: E402 (tools/, beside this folder)


def facts(release):
    """What the page shows before its script runs: the same as app.js's facts(), for no script."""
    tag = release.get("tag_name") or ""
    version = re.sub(r"^v", "", tag, flags=re.I)
    zip_name = f"PS5-ECAM-{tag}.zip"
    body = (release.get("body") or "").replace("\r\n", "\n")
    section = re.match(r"\s*#{1,3}\s+download\b[^\n]*\n(.*?)(?=^#{1,2}\s|\Z)", body, re.I | re.S | re.M)
    section = section.group(1) if section else ""
    assets = release.get("assets") or []
    asset = next((a for a in assets if a.get("name") == zip_name), None) or next(
        (a for a in assets if (a.get("name") or "").lower().endswith(".zip")), None)
    linked = re.search(r"\]\((https://github\.com/[^)\s]+\.zip)\)", section, re.I)
    if linked:
        url = linked.group(1)
    elif asset:
        url = asset["browser_download_url"]
    else:
        url = f"https://github.com/{REPO}/raw/releases/{zip_name}" if tag else f"https://github.com/{REPO}/releases/latest"
    size = ""
    if asset and asset.get("size"):
        size = f"{asset['size'] / 1e6:.1f} MB"
    elif m := re.search(r"\((\d+(?:\.\d+)?)\s*MB\)", section, re.I):
        size = m.group(1) + " MB"
    sha = ""
    if asset and re.fullmatch(r"sha256:[0-9a-f]{64}", asset.get("digest") or "", re.I):
        sha = asset["digest"][7:]
    elif m := re.search(r"\b[0-9a-f]{64}\b", section, re.I):
        sha = m.group(0)
    published = release.get("published_at") or ""
    published_text = ""
    if m := re.match(r"(\d{4})-(\d{2})-(\d{2})", published):
        months = ["January", "February", "March", "April", "May", "June", "July", "August",
                  "September", "October", "November", "December"]
        published_text = f"Published {int(m.group(3))} {months[int(m.group(2)) - 1]} {m.group(1)}"
    return {
        "VERSION": version or "the latest",
        "RELEASE_NAME": release.get("name") or (f"Playtendoh {version}" if version else "The latest release"),
        "ZIP_URL": url,
        "ZIP_NAME": url.rsplit("/", 1)[-1] if tag else "Download",
        "SIZE": size,
        "SIZE_LINE": f"ZIP, {size}" if size else "",
        "SHA": sha,
        "NOTES_URL": release.get("html_url") or f"https://github.com/{REPO}/releases/latest",
        "PUBLISHED": published,
        "PUBLISHED_TEXT": published_text,
    }


def baked(release):
    """The release as the page keeps it: what app.js reads, and nothing else."""
    keep = {k: release.get(k) for k in ("tag_name", "name", "published_at", "html_url", "body")}
    keep["assets"] = [{k: a.get(k) for k in ("name", "size", "digest", "browser_download_url")}
                      for a in release.get("assets") or []]
    text = json.dumps(keep, ensure_ascii=False, separators=(",", ":"))
    # safe inside <script>: no "</script>" or "<!--" can end or bend it
    return text.replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")


COMPAT_FIELDS = {"side": str, "titleId": str, "name": str, "status": str, "consoles": int, "sessions": int, "ended": int, "failed": int,
                 "crashed": int, "closed": int, "medianMinutes": (int, type(None)), "firmware": list, "models": list, "apps": list,
                 "commonError": (str, type(None)), "lastSeen": str}


def compat_table(path):
    """The intake's table, kept to the fields compatibility.html shows: empty when there is none yet."""
    table = {"schema": 1, "generated": None, "games": []}
    if not path:
        return table
    try:
        with open(path, encoding="utf-8") as f:
            given = json.load(f)
    except (OSError, ValueError) as e:
        print(f"build.py: no compatibility table ({e})", file=sys.stderr)
        return table
    if not isinstance(given, dict) or given.get("schema") != 1 or not isinstance(given.get("games"), list):
        print("build.py: the compatibility table is not schema 1: left out", file=sys.stderr)
        return table
    table["generated"] = given.get("generated") if isinstance(given.get("generated"), str) else None
    for game in given["games"]:
        if isinstance(game, dict) and all(isinstance(game.get(k), t) for k, t in COMPAT_FIELDS.items()):
            table["games"].append({k: game[k] for k in COMPAT_FIELDS})
    return table


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--release", help="the latest release, as JSON from GitHub's API")
    parser.add_argument("--out", default=os.path.join(REPO_ROOT, "_site"))
    parser.add_argument("--compat", help="the report intake's table (GET /v1/compatibility.json), for compatibility.html")
    parser.add_argument("--reports-url", default="", help="the report intake's address: compatibility.html reads its table live from there")
    args = parser.parse_args()

    release = {}
    if args.release:
        try:
            with open(args.release, encoding="utf-8") as f:
                release = json.load(f)
        except (OSError, ValueError) as e:
            print(f"build.py: no release baked in ({e})", file=sys.stderr)
        if not isinstance(release, dict) or not release.get("tag_name"):
            release = {}

    if os.path.exists(args.out):
        shutil.rmtree(args.out)
    os.makedirs(args.out)
    for name in sorted(os.listdir(SITE)):
        if name in SKIP or name.startswith("."):
            continue
        src = os.path.join(SITE, name)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(args.out, name))
        elif name != "index.html":
            shutil.copy2(src, args.out)
    for src, dst in ART.items():
        os.makedirs(os.path.dirname(os.path.join(args.out, dst)), exist_ok=True)
        shutil.copy2(os.path.join(REPO_ROOT, src), os.path.join(args.out, dst))

    with open(os.path.join(SITE, "index.html"), encoding="utf-8") as f:
        page = f.read()
    values = {k: html.escape(v) for k, v in facts(release).items()}
    values["RELEASE_JSON"] = baked(release) if release else "null"
    missing = sorted(set(re.findall(r"%([A-Z_]+)%", page)) - values.keys())
    if missing:
        sys.exit(f"build.py: index.html has placeholders with no value: {', '.join(missing)}")
    page = re.sub(r"%([A-Z_]+)%", lambda m: values[m.group(1)], page)
    with open(os.path.join(args.out, "index.html"), "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    # compatibility.html reads the intake's table itself, so it is never older than the intake's hour, whether or
    # not GitHub runs this workflow's schedule; the table baked in below is what it shows when the intake can't be reached.
    origin = re.match(r"^https://[A-Za-z0-9.-]+(?::[0-9]+)?", args.reports_url.strip())
    with open(os.path.join(SITE, "compatibility.html"), encoding="utf-8") as f:
        page = f.read().replace("%REPORTS_ORIGIN%", html.escape(origin.group(0)) if origin else "")
    with open(os.path.join(args.out, "compatibility.html"), "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    games = compat_table(args.compat)
    with open(os.path.join(args.out, "compatibility.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(games, f, ensure_ascii=False, separators=(",", ":"))
    # the console's: every game's status, the list's and the reports' (tools/compat_feed.py)
    with open(compat_feed.LIST, encoding="utf-8") as f:
        listed = f.read()
    status = compat_feed.feed(listed, games)
    # compatibility.html's: the owner's games, whose status wins over the reports' (tools/compat-list.json)
    with open(os.path.join(args.out, "list.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"schema": 1, "games": compat_feed.from_list(listed)}, f, ensure_ascii=False, separators=(",", ":"))
    with open(os.path.join(args.out, "status.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(status, f, ensure_ascii=False, separators=(",", ":"))
    print(f"build.py: {args.out}, with {release.get('tag_name') or 'no release'} baked in, {len(games['games'])} games reported, "
          f"{len(status['games'])} with a status")


if __name__ == "__main__":
    main()
