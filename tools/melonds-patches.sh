#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The port's changes to melonDS's own files, kept as patches/melonds/*.patch against the pinned
# commit.
#
#   tools/melonds-patches.sh apply    put them on a branch named ps5 in .deps/melonDS, and the ones
#                                     added since on it (ps5cemu_patches_apply in tools/env.sh)
#   tools/melonds-patches.sh export   write the ps5 branch's commits back to patches/melonds
#
# To change melonDS: edit .deps/melonDS on the ps5 branch, commit, then export.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
melonds=$PS5CEMU_ROOT/.deps/melonDS
patches=$PS5CEMU_ROOT/patches/melonds
pin=$(python3 -c 'import json,sys; print(next(i["commit"] for i in json.load(open(sys.argv[1]))["items"] if i["name"] == "melonds"))' \
    "$PS5CEMU_ROOT/tools/deps.json")

case ${1:-} in
apply)
    ps5cemu_patches_apply melonds "$melonds" "$patches" "$pin"
    ;;
export)
    [[ -z $(git -C "$melonds" status --porcelain --untracked-files=no) ]] ||
        { echo "commit the changes in .deps/melonDS first" >&2; exit 1; }
    rm -f "$patches"/*.patch
    mkdir -p "$patches"
    git -C "$melonds" format-patch -q --no-signature --zero-commit --no-numbered -o "$patches" "$pin..ps5"
    ls "$patches"
    ;;
*)
    echo "usage: tools/melonds-patches.sh apply|export" >&2
    exit 2
    ;;
esac
