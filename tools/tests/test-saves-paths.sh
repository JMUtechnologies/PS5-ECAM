#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Uses fmt (libfmt-dev on Ubuntu); needs no writable /mnt folder.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
read -r -a fmt_flags <<< "$(pkg-config --cflags --libs fmt)"
"${CXX:-c++}" -std=c++20 -DPS5CEMU_VERSION='"test"' -I"$root/port" \
    "$root/tools/tests/test-saves-paths.cpp" "${fmt_flags[@]}" -pthread -o "$work/test-saves-paths"
for mode in legacy new migrated restore; do
    "$work/test-saves-paths" "$work/fixtures" "$mode"
done
