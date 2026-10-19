#!/usr/bin/env bash
# The PS4 proof of concept's build, as far as it has got: Crema (iHaiDeeZ's Cemu for the PS4) built
# as it is, with OpenOrbis' PS4 toolchain and clang 21, on Ubuntu 24.04. A stage at a time:
#
#   ps4/build.sh toolchain   clang 21 from apt.llvm.org; OpenOrbis in /opt/pacbrew/ps4/openorbis,
#                            laid out as PacBrew's packages lay it out (their server was down)
#   ps4/build.sh sources     Crema, DolphinPS4 (its Mesa patch), Mesa 26.0.8 with the PS4 patches,
#                            libdrm's headers, Cemu at Crema's base with Crema's patch
#   ps4/build.sh libcxx      Crema's libc++ 21 and runtime for the PS4
#   ps4/build.sh mesa        Mesa's RADV for the PS4
#   ps4/build.sh deps        Cemu's libraries
#   ps4/build.sh cemu        Crema's Cemu
#   ps4/build.sh             all of them, in that order
#
# Run as root (apt, /opt). Everything else goes in $PS4_WORK (~/ps4 unless set).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${PS4_WORK:-$HOME/ps4}"
CREMA="${CREMA:-$WORK/crema}"
DOLPHINPS4="${DOLPHINPS4:-$WORK/dolphinps4}"
TOOLCHAIN_SRC="${TOOLCHAIN_SRC:-$WORK/toolchain-src}"
PACBREW_RECIPES="${PACBREW_RECIPES:-$WORK/pacbrew-ps4-openorbis}"
export PSB="${PSB:-$WORK/cemups4-build}"
export PS4_BUILD_ROOT="$PSB"
export OPENORBIS="${OPENORBIS:-/opt/pacbrew/ps4/openorbis}"
JOBS="${JOBS:-$(nproc)}"

# what was built against, on 19 October 2026
CREMA_COMMIT=2738764ea6eb91aa88f6d45aa5c83f14a1509a44
DOLPHINPS4_COMMIT=6b359aa944c1e51c1b00838e365179b8d88466d1
PACBREW_RECIPES_COMMIT=16f27e38760ea21eaf1187d0edbf4342c4a44021
OPENORBIS_HEADERS_COMMIT=cd480230b946239f758333ef0dff40d67c5f0c20  # PacBrew's ps4-openorbis-headers 0.5.2-4
PS4_MUSL_COMMIT=140a653299ffcb1457bc63a5ab1f507d7bb37c54           # PacBrew's ps4-openorbis-musl 1.5-2
OPENORBIS_RELEASE=https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.2/v0.5.2.tar.gz
ORBIS_LIB_GEN=https://github.com/OpenOrbis/orbis-lib-gen/releases/download/v1.3/lib.tar.gz
CEMU_COMMIT=cc6fea125a5d4f559c662d13c5f4af0b6dd0876f                 # Crema's base (its CREDITS.md)
MESA_URL=https://archive.mesa3d.org/mesa-26.0.8.tar.xz
LIBDRM_URL=https://dri.freedesktop.org/libdrm/libdrm-2.4.125.tar.xz

git_at() {  # git_at <dir> <repository> <commit or tag>: a shallow checkout
    local dir="$1" repo="$2" ref="$3"
    if [ -d "$dir/.git" ] && [ "$(git -C "$dir" rev-parse HEAD)" = "$(git -C "$dir" rev-parse "$ref^{commit}" 2>/dev/null)" ]; then
        return
    fi
    mkdir -p "$dir"
    git -C "$dir" init -q
    GIT_LFS_SKIP_SMUDGE=1 git -C "$dir" fetch -q --depth 1 "$repo" "$ref"
    GIT_LFS_SKIP_SMUDGE=1 git -C "$dir" checkout -q FETCH_HEAD
}

download() {  # download <file> <url>
    [ -f "$1" ] || curl -fsSL --retry 5 --connect-timeout 30 -o "$1" "$2"
}

commit_all() {  # commit_all <dir> <message>: so a patch can be told from what it was applied to
    git -C "$1" add -A
    git -C "$1" -c user.name=ps4-build -c user.email=ps4-build@localhost commit -q -m "$2"
}

stage_toolchain() {
    if ! command -v clang-21 >/dev/null; then
        . /etc/os-release
        curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/apt-llvm.gpg
        echo "deb [signed-by=/usr/share/keyrings/apt-llvm.gpg] http://apt.llvm.org/$VERSION_CODENAME/ llvm-toolchain-$VERSION_CODENAME-21 main" \
            > /etc/apt/sources.list.d/apt-llvm-21.list
        apt-get update -q
    fi
    apt-get install -y -q clang-21 lld-21 llvm-21 libclang-rt-21-dev meson ninja-build cmake pkg-config \
        glslang-tools python3-venv python3-mako python3-yaml git curl perl

    mkdir -p "$TOOLCHAIN_SRC"
    download "$TOOLCHAIN_SRC/openorbis-v0.5.2.tar.gz" "$OPENORBIS_RELEASE"
    download "$TOOLCHAIN_SRC/orbis-lib-gen-v1.3-lib.tar.gz" "$ORBIS_LIB_GEN"
    # GitHub's source archives are refused through some proxies: commits come with git
    git_at "$TOOLCHAIN_SRC/openorbis-headers" https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain "$OPENORBIS_HEADERS_COMMIT"
    git_at "$TOOLCHAIN_SRC/ps4-openorbis-musl" https://github.com/PacBrew/ps4-openorbis-musl "$PS4_MUSL_COMMIT"
    git_at "$PACBREW_RECIPES" https://github.com/PacBrew/ps4-openorbis "$PACBREW_RECIPES_COMMIT"
    SRC="$TOOLCHAIN_SRC" PACBREW_RECIPES="$PACBREW_RECIPES" OO="$OPENORBIS" bash "$HERE/assemble-openorbis.sh"
}

stage_sources() {
    git_at "$CREMA" https://github.com/iHaiDeeZ/Crema "$CREMA_COMMIT"
    git_at "$DOLPHINPS4" https://github.com/iHaiDeeZ/DolphinPS4 "$DOLPHINPS4_COMMIT"
    # its scripts are run by path (one runs another)
    chmod +x "$CREMA"/toolchain/*.sh "$CREMA"/toolchain/bin/* "$CREMA"/scripts/*.sh "$CREMA"/scripts/*.py

    mkdir -p "$PSB/dl" "$PSB/src"
    download "$PSB/dl/mesa-26.0.8.tar.xz" "$MESA_URL"
    download "$PSB/dl/libdrm-2.4.125.tar.xz" "$LIBDRM_URL"
    if [ ! -d "$PSB/src/libdrm" ]; then
        tar -xJf "$PSB/dl/libdrm-2.4.125.tar.xz" -C "$PSB/src" --no-same-owner
        mv "$PSB/src/libdrm-2.4.125" "$PSB/src/libdrm"
    fi
    if [ ! -d "$PSB/src/mesa" ]; then
        tar -xJf "$PSB/dl/mesa-26.0.8.tar.xz" -C "$PSB/src" --no-same-owner
        mv "$PSB/src/mesa-26.0.8" "$PSB/src/mesa"
        git -C "$PSB/src/mesa" init -q
        commit_all "$PSB/src/mesa" "mesa 26.0.8"
        git -C "$PSB/src/mesa" apply "$DOLPHINPS4/patches/mesa-ps4.patch"
        # Crema's patches/mesa-ps4-vertex-fetch-switch.patch, made against its own Mesa tree: the
        # same change against mesa-ps4.patch
        git -C "$PSB/src/mesa" apply "$HERE/patches/mesa-vertex-fetch.patch"
        commit_all "$PSB/src/mesa" "PS4: DolphinPS4's mesa-ps4.patch; Crema's aligned vertex fetch, switchable"
    fi

    if [ ! -d "$PSB/src/Cemu/.git" ]; then
        git_at "$PSB/src/Cemu" https://github.com/cemu-project/Cemu "$CEMU_COMMIT"
        git -C "$PSB/src/Cemu" submodule update -q --init --depth 1
        git -C "$PSB/src/Cemu" checkout -q -b ps4
        git -C "$PSB/src/Cemu" apply --whitespace=nowarn "$CREMA/patches/cemu-ps4.patch"
        commit_all "$PSB/src/Cemu" "Crema's cemu-ps4.patch"
    fi
}

stage_libcxx() {
    bash "$CREMA/toolchain/build-libcxx.sh"
    bash "$CREMA/toolchain/build-runtime.sh"
}

stage_mesa() {
    bash "$CREMA/scripts/install-overlay.sh"
    [ -x "$PSB/mesa-venv/bin/meson" ] || {
        python3 -m venv "$PSB/mesa-venv"
        "$PSB/mesa-venv/bin/pip" install -q meson mako pyyaml packaging
    }
    mkdir -p "$PSB/host-tools/bin"
    ln -sf "$(command -v glslangValidator)" "$PSB/host-tools/bin/glslangValidator"
    bash "$CREMA/scripts/build-mesa.sh" setup
}

stage_deps() {
    # Crema's build-deps.sh takes a library's folder as fetched when it is there: the ones it
    # would download as GitHub source archives come with git instead
    local src="$PSB/src/deps" lib repo ref
    mkdir -p "$src"
    while read -r lib repo ref; do
        rmdir "$src/$lib" 2>/dev/null || true  # left empty by a download that failed
        [ -d "$src/$lib" ] || git_at "$src/$lib" "https://github.com/$repo" "$ref"
    done <<'EOF'
libpng pnggroup/libpng v1.6.47
fmt fmtlib/fmt 12.1.0
glm g-truc/glm 1.0.1
rapidjson Tencent/rapidjson 24b5e7a8b27f42fa16b96fc70aade9106cf7102f
glslang KhronosGroup/glslang 15.1.0
libsmb2 sahlberg/libsmb2 v6.0.0
libnfs sahlberg/libnfs libnfs-8.0.0
EOF
    JOBS="$JOBS" bash "$CREMA/scripts/build-deps.sh"
}

stage_cemu() {
    bash "$CREMA/scripts/configure-cemu.sh"
    JOBS="$JOBS" bash "$CREMA/scripts/build-cemu.sh"
}

stages=("$@")
[ ${#stages[@]} -gt 0 ] || stages=(toolchain sources libcxx mesa deps cemu)
for s in "${stages[@]}"; do
    echo "=== $s"
    "stage_$s"
done
