#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Compile and run the settings store from the patched child runtime, with assertions enabled.
set -euo pipefail
root=$(cd "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
child=$root/.deps/eglantine/EglantinePS5
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
bash "$root/tools/eglantine-patches.sh" apply
"${CXX:-c++}" -std=c++20 -UNDEBUG ${CPPFLAGS:-} \
    "$child/headless/preferences_check.cpp" -o "$work/settings-check"
"$work/settings-check"
python3 -B "$child/tools/check-performance-settings.py"
