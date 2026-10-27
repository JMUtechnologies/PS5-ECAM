#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Where the Switch side's program finds a game's update and DLC (patches/eden 0018), from the patched
# Eden checkout (tools/eden-patches.sh apply first, or set EDEN_SOURCE)
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
eden=${EDEN_SOURCE:-$root/.deps/ProsperoEdenX}
if [[ ! -f "$eden/headless/game_addons.h" ]]; then
    echo 'Fetch prosperoedenx and apply tools/eden-patches.sh first (or set EDEN_SOURCE).' >&2
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$eden/headless" \
    "$root/tools/tests/test-eden-addons.cpp" -o "$work/test-eden-addons"
"$work/test-eden-addons" "$work/files"
