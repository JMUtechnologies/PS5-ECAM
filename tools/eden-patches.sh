#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The port's changes to ProsperoEdenX's own files (the Switch side's program), kept as
# patches/eden/*.patch against the pinned commit.
#
#   tools/eden-patches.sh apply    put them on a branch named ps5 in .deps/ProsperoEdenX, and the ones
#                                  added since on it (ps5cemu_patches_apply in tools/env.sh)
#   tools/eden-patches.sh export   write the ps5 branch's commits back to patches/eden
#
# To change ProsperoEdenX: edit .deps/ProsperoEdenX on the ps5 branch, commit, then export.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
eden=$PS5CEMU_ROOT/.deps/ProsperoEdenX
patches=$PS5CEMU_ROOT/patches/eden
pin=$(python3 -c 'import json,sys; print(next(i["commit"] for i in json.load(open(sys.argv[1]))["items"] if i["name"] == "prosperoedenx"))' \
    "$PS5CEMU_ROOT/tools/deps.json")

case ${1:-} in
apply)
    ps5cemu_patches_apply eden "$eden" "$patches" "$pin"
    ;;
export)
    [[ -z $(git -C "$eden" status --porcelain --untracked-files=no) ]] ||
        { echo "commit the changes in .deps/ProsperoEdenX first" >&2; exit 1; }
    rm -f "$patches"/*.patch
    mkdir -p "$patches"
    git -C "$eden" format-patch -q --no-signature --zero-commit --no-numbered -o "$patches" "$pin..ps5"
    ls "$patches"
    ;;
*)
    echo "usage: tools/eden-patches.sh apply|export" >&2
    exit 2
    ;;
esac
