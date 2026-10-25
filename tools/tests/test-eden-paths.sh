#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
eden=${EDEN_SOURCE:-$root/.deps/ProsperoEdenX}
if [[ ! -f "$eden/headless/ecam_layout.h" ]]; then
    echo 'Fetch prosperoedenx and apply tools/eden-patches.sh first (or set EDEN_SOURCE).' >&2
    exit 1
fi
cmp "$root/port/app/system_layout.h" "$eden/headless/ecam_layout.h"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" -I"$eden/headless" \
    -DECAM_SHARED_DATA="\"$work/console/ps5cemu\"" -DPS5CEMU_DATA="\"$work/console/ps5cemu\"" \
    "$root/tools/tests/test-eden-paths.cpp" -o "$work/test-eden-paths"
for mode in fresh legacy activated invalid; do
    rm -rf "$work/console"
    "$work/test-eden-paths" "$mode"
done
