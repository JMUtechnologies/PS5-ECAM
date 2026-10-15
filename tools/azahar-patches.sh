#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The port's changes to Azahar's own files (Mihawk's PS5_Azahar), kept as patches/azahar/*.patch
# against the pinned commit.
#
#   tools/azahar-patches.sh apply    put them on a branch named ps5 in .deps/PS5_Azahar, and the
#                                    ones added since on it (ps5cemu_patches_apply in tools/env.sh)
#   tools/azahar-patches.sh export   write the ps5 branch's commits back to patches/azahar
#
# To change Azahar: edit .deps/PS5_Azahar on the ps5 branch, commit, then export.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
azahar=$PS5CEMU_ROOT/.deps/PS5_Azahar
patches=$PS5CEMU_ROOT/patches/azahar
pin=$(python3 -c 'import json,sys; print(next(i["commit"] for i in json.load(open(sys.argv[1]))["items"] if i["name"] == "azahar"))' \
    "$PS5CEMU_ROOT/tools/deps.json")

case ${1:-} in
apply)
    ps5cemu_patches_apply azahar "$azahar" "$patches" "$pin"
    ;;
export)
    [[ -z $(git -C "$azahar" status --porcelain --untracked-files=no) ]] ||
        { echo "commit the changes in .deps/PS5_Azahar first" >&2; exit 1; }
    rm -f "$patches"/*.patch
    mkdir -p "$patches"
    git -C "$azahar" format-patch -q --no-signature --zero-commit --no-numbered -o "$patches" "$pin..ps5"
    ls "$patches"
    ;;
*)
    echo "usage: tools/azahar-patches.sh apply|export" >&2
    exit 2
    ;;
esac
