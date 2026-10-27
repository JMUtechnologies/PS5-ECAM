#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A Switch game's update and DLC as its page shows them (port/eden/addons.h)
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I"$root/port" "$root/tools/tests/test-switch-addons.cpp" -o "$work/test-switch-addons"
"$work/test-switch-addons"
