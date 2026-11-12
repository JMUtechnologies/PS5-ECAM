#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""How each game runs, for the console: one small JSON every system shares (port/app/compatibility.h).

    tools/compat_feed.py [--reports TABLE] [--out FILE]

From the owner's statuses (tools/compat-list.json: a game the owner played, and how it runs) and, when given, the report
intake's table (GET /v1/compatibility.json: what players' consoles reported, site/build.py has it). The
site puts it at premohq.github.io/PlayTendoh/status.json every hour; tools/package.sh bundles the list's
own (no reports) as assets/compatibility.json for a console that never got the site's. The site
(compatibility.html) is where players see how games run.

    {"schema": 1, "generated": "2026-11-11T05:00:00Z", "games": [
      {"side": "wiiu", "titleId": "00050000101c9500", "name": "...", "status": "playable",
       "region": "USA, v208", "notes": "...", "consoles": 3, "sessions": 10, "ended": 9}]}

side: the launcher's side keys (launcher.h SideName). status: playable, issues or broken (Not playable).
titleId (16 hex digits), region and the counts only when there are any. The console finds a game
by side and title ID, else by side and name, compared without case, spaces or punctuation.

A game the owner gave a status keeps it; the reports give it their counts. A game only the
reports have gets a status from them once two of its sessions said something: one that ended from the
game's menu went well, one that did not start or crashed did not (closed under it says nothing). Four in
five went well: playable; none did: broken; else issues.
"""

import argparse
import datetime
import json
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIST = os.path.join(REPO_ROOT, "tools", "compat-list.json")

STATUSES = ("playable", "issues", "broken")
TITLE_ID = re.compile(r"[0-9a-f]{16}")


def key(name):
    """A name as the console compares it: lower case letters and digits only (compatibility.cpp's Key)."""
    return "".join(c for c in name.lower() if c.isalnum() and c.isascii())


def from_list(text):
    """The owner's games (tools/compat-list.json): side, name, status, and a region when it matters."""
    games = []
    for g in json.loads(text).get("games") or []:
        if isinstance(g, dict) and isinstance(g.get("side"), str) and isinstance(g.get("name"), str) and g.get("status") in STATUSES:
            games.append({k: g[k] for k in ("side", "name", "status", "region") if isinstance(g.get(k), str) and g[k]})
    return games


def from_reports(table):
    """The intake's games (site/build.py's compat_table), as the feed has them: a status only when they say enough."""
    games = []
    for row in table.get("games") or []:
        if not isinstance(row, dict) or not isinstance(row.get("side"), str) or not isinstance(row.get("titleId"), str):
            continue
        ended = int(row.get("ended") or 0)
        bad = int(row.get("failed") or 0) + int(row.get("crashed") or 0)
        told = ended + bad
        game = {"side": row["side"], "name": str(row.get("name") or ""), "status": None}
        if TITLE_ID.fullmatch(row["titleId"]) and row["titleId"] != "0" * 16:
            game["titleId"] = row["titleId"]
        if told >= 2:
            game["status"] = "playable" if ended >= told * 0.8 else "broken" if ended == 0 else "issues"
        game.update(consoles=int(row.get("consoles") or 0), sessions=told, ended=ended)
        games.append(game)
    return games


def merge(listed, reported):
    """The list's games with the reports' counts and title IDs, then the reports' other games that have a status."""
    out = [dict(g) for g in listed]
    index = {(g["side"], key(g["name"])): g for g in out}
    for report in reported:
        mine = index.get((report["side"], key(report["name"])))
        if mine is not None:
            # the list's say, the reports' numbers (a game with two title IDs, two regions: the list's one row
            # adds them up, and keeps the first ID)
            if report.get("titleId") and "titleId" not in mine:
                mine["titleId"] = report["titleId"]
            for field in ("consoles", "sessions", "ended"):
                mine[field] = mine.get(field, 0) + report[field]
        elif report["status"] and (report.get("titleId") or key(report["name"])):
            out.append(report)
    out.sort(key=lambda g: (g["side"], key(g["name"])))
    return out


def feed(list_text, reports=None, now=None):
    now = now or datetime.datetime.now(datetime.timezone.utc)
    games = merge(from_list(list_text), from_reports(reports or {}))
    return {"schema": 1, "generated": now.strftime("%Y-%m-%dT%H:%M:%SZ"), "games": games}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--list", default=LIST, help="the owner's statuses (default: tools/compat-list.json)")
    parser.add_argument("--reports", help="the report intake's table (GET /v1/compatibility.json)")
    parser.add_argument("--out", default="-", help="where to write it (default: standard output)")
    args = parser.parse_args()
    with open(args.list, encoding="utf-8") as f:
        text = f.read()
    reports = {}
    if args.reports:
        try:
            with open(args.reports, encoding="utf-8") as f:
                reports = json.load(f)
        except (OSError, ValueError) as e:
            print(f"compat_feed.py: no reports ({e})", file=sys.stderr)
        if not isinstance(reports, dict) or reports.get("schema") != 1:
            reports = {}
    text = json.dumps(feed(text, reports), ensure_ascii=False, separators=(",", ":"))
    if args.out == "-":
        print(text)
    else:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)


if __name__ == "__main__":
    main()
