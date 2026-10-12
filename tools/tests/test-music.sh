#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The launcher's music (port/frontend/music.cpp) on the PC: an MP3 and WAVs made with ffmpeg, played
# through the same code the PS5 runs. Needs ffmpeg, clang++ (or g++), and what `make deps` fetches
# (dr_mp3, pacbrew's fmt).
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/../env.sh"
cd "$PS5CEMU_ROOT"
out=$PS5CEMU_BUILD/test-music
rm -rf "$out" && mkdir -p "$out/music"
ffmpeg -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=2" -ac 1 -b:a 128k "$out/music/A tone.mp3"
ffmpeg -loglevel error -f lavfi -i "sine=frequency=1000:sample_rate=22050:duration=1.5" -ac 2 -c:a pcm_s16le "$out/music/b-tone.WAV"
ffmpeg -loglevel error -f lavfi -i "sine=frequency=500:sample_rate=48000:duration=1" -c:a pcm_u8 "$out/music/not music.wav"
echo "not music" >"$out/music/notes.txt"
compiler=$(command -v clang++ || command -v g++)
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -DFMT_HEADER_ONLY -I port -I "$PS5CEMU_PACBREW/include" -I .deps/dr_libs \
    tools/tests/test-music.cpp port/frontend/music.cpp -o "$out/test-music"
"$out/test-music" "$out/music"
