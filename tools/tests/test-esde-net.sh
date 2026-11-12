#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# ES-DE's getaddrinfo on the console (port/esde/ps5/net_ecam.cpp) on the PC, over a stand-in for the
# PS5's resolver (tools/tests/test-esde-net.cpp). ESDE_NET_ONLINE=1 also has curl reach the hosts ES-DE's
# scraper uses, over TLS, with only that getaddrinfo to find them.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cxx=${CXX:-c++}
"$cxx" -std=c++17 -Wall -Wextra -Werror -I"$root/port/esde/ps5" "$root/tools/tests/test-esde-net.cpp" \
    "$root/port/esde/ps5/net_ecam.cpp" -o "$work/test-esde-net" -ldl
mkdir -p "$work/home"
"$work/test-esde-net" "$work/home"

[[ ${ESDE_NET_ONLINE:-0} == 1 ]] || exit 0
"$cxx" -std=c++17 -Wall -Wextra -Werror -shared -fPIC -DECAM_NET_LIBRARY -I"$root/port/esde/ps5" \
    "$root/tools/tests/test-esde-net.cpp" "$root/port/esde/ps5/net_ecam.cpp" -o "$work/net_ecam.so" -ldl
# ScreenScraper's API, TheGamesDB's API and its covers, as ES-DE's scraper asks them (curl's own
# resolver given only net_ecam.cpp's getaddrinfo: the program's, preloaded)
for url in https://api.screenscraper.fr/ https://api.thegamesdb.net/ https://cdn.thegamesdb.net/images/; do
    code=$(LD_PRELOAD="$work/net_ecam.so" curl -s -o /dev/null -w '%{http_code}' --max-time 20 "$url" 2>"$work/said") ||
        { echo "FAIL $url: curl could not reach it"; exit 1; }
    grep -q "resolver was asked [1-9]" "$work/said" || { echo "FAIL $url: curl did not ask net_ecam.cpp"; exit 1; }
    echo "ok   $url: HTTP $code, its name through net_ecam.cpp"
done
echo "online: all reached"
