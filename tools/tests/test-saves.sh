#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Back up to USB and Restore from USB on a PC (tools/tests/test-saves.cpp, port/app/saves.cpp), into
# /mnt/usb0 as the console's first USB drive: it must be a folder this user can write to (CI makes
# it; on a PC, sudo mkdir -p /mnt/usb0 && sudo chmod 777 /mnt/usb0). Needs clang-18 and fmt's
# library (libfmt-dev). Prints PASS lines; exits 1 on a failure.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ ! -w /mnt/usb0 ]]; then
    echo "/mnt/usb0 is not a folder this user can write to: skipped" >&2
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work" /mnt/usb0/PS5CEMU-HAR-saves' EXIT
clang++-18 -std=c++20 -Wno-deprecated-declarations -DPS5CEMU_VERSION='"test"' -DPS5CEMU_DATA="\"$work/data\"" -I"$root/port" \
    "$root/tools/tests/test-saves.cpp" "$root/port/app/saves.cpp" -lfmt -pthread -o "$work/test-saves"
"$work/test-saves"
