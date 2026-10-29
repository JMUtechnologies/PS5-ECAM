# ECAM workstation, based on the existing Prospero Store Ubuntu recipe.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git curl wget make cmake ninja-build pkg-config ccache build-essential nasm \
    clang-18 lld-18 llvm-18 llvm-18-dev libclang-18-dev libclang-rt-18-dev \
    libclc-18-dev libllvmspirvlib-18-dev llvm-spirv-18 spirv-tools \
    python3 python3-pip python3-venv python3-mako python3-packaging meson \
    rsync flex bison glslang-tools zip unzip p7zip-full xz-utils patch \
    libarchive-dev libzip-dev libzstd-dev libssl-dev libfmt-dev libvulkan-dev \
    libcurl4-openssl-dev zlib1g-dev ffmpeg libx11-dev libgl1-mesa-dev libclc-18 llvm-18-tools python3-dev python3-numpy python3-pil zstd \
    && rm -rf /var/lib/apt/lists/*
# Ubuntu's Meson 1.3.2 is older than the pinned Mesa source requires (>=1.4).
# Keep distro Python modules visible for Mesa's Mako/packaging imports.
RUN python3 -m venv --system-site-packages /opt/ecam-tools \
    && /opt/ecam-tools/bin/python -m pip install --no-cache-dir meson==1.9.2 cmake==3.31.10 \
    && ln -s /opt/ecam-tools/bin/meson /usr/local/bin/meson \
    && ln -s /opt/ecam-tools/bin/cmake /usr/local/bin/cmake \
    && ln -s /opt/ecam-tools/bin/ctest /usr/local/bin/ctest \
    && ln -s /opt/ecam-tools/bin/cpack /usr/local/bin/cpack \
    && meson --version \
    && cmake --version \
    && ctest --version \
    && cpack --version
RUN ln -s /usr/bin/clang-18 /usr/bin/clang \
    && ln -s /usr/bin/clang++-18 /usr/bin/clang++
ENV PATH=/usr/lib/llvm-18/bin:${PATH}
ENV CC=clang-18 CXX=clang++-18
WORKDIR /src
CMD ["bash"]
