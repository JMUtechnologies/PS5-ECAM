#!/usr/bin/env python3
"""Provide the verbatim LLVM 18.1.8 notice required by Porpoise staging.
Source: llvm/llvm-project, llvmorg-18.1.8, llvm/LICENSE.TXT
Git blob: fa6ac540007032cbd0ec772a1c72e6cb5527a4fe
The SDK runtime license includes the Apache text, LLVM exceptions and legacy notice.
"""
import hashlib
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[2]
source = root / "tools/docker/licenses/llvm-18.1.8-LICENSE.TXT"
expected = "8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee"
if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
    raise SystemExit("LLVM license checksum mismatch")
target = root / "build/retro-source/porpoise/.deps/llvm-project/llvm/LICENSE.TXT"
if not target.parents[3].is_dir():
    raise SystemExit("Prepare Porpoise before staging its LLVM notice")
target.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(source, target)
print("Staged verified upstream LLVM runtime license:", target)
