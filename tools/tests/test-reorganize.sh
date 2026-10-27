#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A folder for each system, and put back (port/app/reorganize.cpp), in a folder of its own as /data:
# tools/tests/test-reorganize.cpp. Needs nothing but a C++20 compiler and a GNU-style linker (--wrap).
# Prints PASS lines; exits 1 on a failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'chmod -R u+rwx "$work" 2>/dev/null; rm -rf "$work"' EXIT
flags=(-std=c++20 -Wall -Wextra -Werror -I"$root/port" -DPS5CEMU_DATA="\"$work/data/ps5cemu\"")
sources=("$root/tools/tests/test-reorganize.cpp" "$root/port/app/reorganize.cpp")
"${CXX:-c++}" "${flags[@]}" "${sources[@]}" -o "$work/test-reorganize"
# and as on a console whose /data refuses lstat to an app without a HEN (app/system_layout.h's Status)
"${CXX:-c++}" "${flags[@]}" -DREFUSED_LSTAT -Wl,--wrap=lstat "${sources[@]}" -o "$work/test-reorganize-refused"
mkdir -p "$work/data"
"$work/test-reorganize" "$work/data"
rm -rf "$work/data" && mkdir -p "$work/data"
echo "lstat refused with EPERM, as /data is in an app without a HEN:"
"$work/test-reorganize-refused" "$work/data"
