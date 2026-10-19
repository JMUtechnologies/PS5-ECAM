#!/usr/bin/env bash
# PacBrew's /opt/pacbrew/ps4/openorbis (github.com/PacBrew/ps4-openorbis), assembled without
# PacBrew's package server (down) or its clang 12: the OpenOrbis v0.5.2 release's prebuilt tools,
# the headers, PS4 musl and stub libraries PacBrew pins, and clang/lld 21 in clang 12's place
# (Crema compiles with clang 21 already). Sources in $SRC, fetched by build.sh's toolchain stage.
set -euo pipefail

SRC=${SRC:?the sources folder}
PACBREW_RECIPES=${PACBREW_RECIPES:?PacBrew's ps4-openorbis recipes}
OO=${OO:-/opt/pacbrew/ps4/openorbis}
TARGET=x86_64-pc-freebsd12-elf

rm -rf "$SRC/release" && mkdir -p "$SRC/release"
tar -xzf "$SRC/openorbis-v0.5.2.tar.gz" -C "$SRC/release"
REL="$SRC/release/OpenOrbis/PS4Toolchain"
HDR="$SRC/openorbis-headers"

mkdir -p "$OO"/{bin/linux,lib,cmake,usr/bin,usr/lib/pkgconfig,usr/include}

# the release's prebuilt tools: create-fself, PkgTool.Core (self-contained .NET), readoelf, create-gp4
cp -a "$REL"/bin/linux/{create-fself,create-gp4,readoelf,PkgTool.Core} "$OO/bin/linux/"
ln -sf linux/create-fself "$OO/bin/create-fself"
ln -sf linux/readoelf "$OO/bin/readoelf"

# the headers PacBrew pins (ps4-openorbis-headers 0.5.2-4): include, link.x, samples, and crtlib.o
cp -a "$HDR/include" "$OO/"
cp -a "$HDR/link.x" "$OO/"
cp -a "$HDR/samples" "$OO/"

# clang/lld 21 where PacBrew's scripts look for its clang 12
for t in clang clang++ ld.lld llvm-ar llvm-ranlib llvm-nm llvm-objcopy llvm-strip; do
    ln -sf "$(command -v "$t-21")" "$OO/bin/$t"
done
cp /usr/lib/llvm-21/lib/clang/21/lib/linux/libclang_rt.builtins-x86_64.a "$OO/lib/"

# the stub libraries (orbis-lib-gen v1.3), and PacBrew's stripped libSceLibcInternal.so (malloc
# and such left to musl)
tar -xzf "$SRC/orbis-lib-gen-v1.3-lib.tar.gz" -C "$SRC/release"
cp "$SRC"/release/lib/* "$OO/lib/"
cp "$PACBREW_RECIPES/orbis-lib-gen/libSceLibcInternal.so" "$OO/lib/"

# PacBrew's PS4 musl (ps4-openorbis-musl 1.5-2), built as its recipe builds it, with clang 21
MUSL="$SRC/ps4-openorbis-musl"
(
    cd "$MUSL"
    make distclean >/dev/null 2>&1 || true
    ./configure --target=x86_64-scei-ps4 --prefix=$OO --disable-shared \
        CC="clang-21 --target=$TARGET" AR=llvm-ar-21 RANLIB=llvm-ranlib-21 \
        CFLAGS="-fPIC -DPS4 -D_LIBUNWIND_IS_BAREMETAL=1 -DPS4_FLEXIBLE_MEMORY=1"
    make --quiet -j"$(nproc)"
    make install
)

# crtlib.o, with the PS4's headers (PacBrew's clang 12 looks there by itself)
clang-21 --target=$TARGET -fPIC --sysroot=$OO -isystem $OO/include -c -o "$OO/lib/crtlib.o" "$HDR/src/crt/crtlib.c"

# libunwind and libc++ from the release, where PacBrew's libcxx 12 package puts them (Crema builds
# its own libc++ 21 into its sysroot, and links $OO/lib/libunwind.a)
cp "$REL/lib/libunwind.a" "$OO/lib/"
[ -f "$OO/lib/libc++.a" ] || cp "$REL/lib/libc++.a" "$OO/lib/"

# PacBrew's environment: ps4vars.sh, cmake/ps4.cmake, openorbis-cmake, openorbis-pkg-config
install -m644 "$PACBREW_RECIPES/vars/ps4vars.sh" "$OO/ps4vars.sh"
install -m644 "$PACBREW_RECIPES/vars/ps4.cmake" "$OO/cmake/ps4.cmake"
install -m755 "$PACBREW_RECIPES/vars/openorbis-cmake" "$OO/usr/bin/openorbis-cmake"
sed "s|@prefix@|$OO|g" "$PACBREW_RECIPES/pkg-config/pkg-config.in" > "$OO/usr/bin/openorbis-pkg-config" 2>/dev/null || true
chmod 755 "$OO/usr/bin/openorbis-pkg-config"

echo "assembled $OO: $(ls "$OO/lib" | wc -l) files in lib"
