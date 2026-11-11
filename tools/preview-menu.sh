#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The in-game menus' sheet (port/app/side_menu.h) and the loading screen (port/app/loading_screen.h) on
# the PC, as PNGs in build/menu-preview, to work on their look without a console
# (tools/menu-preview/menu.cpp says what it shows):
#
#   tools/preview-menu.sh [BACKDROP.png]     the backdrop stands in for the game behind the menu
#
# Needs what tools/preview-shell.sh needs (it builds the launcher's objects this links), and Cemu's
# ImGui from .deps/Cemu (`make deps`).
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
cd "$PS5CEMU_ROOT"
out=$PS5CEMU_BUILD/menu-preview
mkdir -p "$out/obj" "$out/include"
ln -sfn "$PS5CEMU_PACBREW/include/fmt" "$out/include/fmt"
imgui=.deps/Cemu/dependencies/imgui
flags=(-std=c++20 -O2 -w -DFMT_HEADER_ONLY -DPS5CEMU_LAUNCHER_PREVIEW -I "$out/include" -I "$PS5CEMU_SYSROOT/include" -I port -I port/app
    -I "$imgui" -I "$PS5CEMU_ROOT/.deps/reshade/deps/stb" $(pkg-config --cflags freetype2))
objects=()
for source in "$imgui/imgui.cpp" "$imgui/imgui_draw.cpp" "$imgui/imgui_tables.cpp" "$imgui/imgui_widgets.cpp" \
    port/app/lang.cpp port/app/keyboard.cpp port/ui/text.cpp tools/menu-preview/menu.cpp; do
    object=$out/obj/$(echo "${source%.cpp}" | tr / _ | tr -d .).o
    objects+=("$object")
    [[ -f $object && $object -nt $source && $source != tools/menu-preview/menu.cpp ]] || clang++-18 "${flags[@]}" -c "$source" -o "$object" &
done
wait
clang++-18 "${objects[@]}" -lfreetype -lz -lpthread -o "$out/menu-preview"
"$out/menu-preview" "$out" port/ui/fonts "${1:-}"
