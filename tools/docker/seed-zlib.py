#!/usr/bin/env python3
"""Seed the original checksum-pinned zlib archive from its official mirror."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
expected = 'bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16'
archive = ROOT / '.deps/archives/zlib-1.3.2.tar.gz'
archive.parent.mkdir(parents=True, exist_ok=True)
def valid(path):
    if not path.is_file(): return False
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest() == expected
if not valid(archive):
    partial = archive.with_suffix('.download')
    subprocess.run(['curl', '-fL', '--retry', '4', '--retry-all-errors',
                    '--connect-timeout', '30', '--max-time', '180',
                    'https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz',
                    '-o', str(partial)], check=True)
    if not valid(partial): raise SystemExit('Official zlib archive checksum mismatch')
    partial.replace(archive)
for argument in sys.argv[1:]:
    root = Path(argument).resolve()
    if not root.is_relative_to(ROOT): raise SystemExit('Target must be in this checkout')
    target = root / '.deps/native/zlib/zlib-1.3.2.tar.gz'
    target.parent.mkdir(parents=True, exist_ok=True)
    if not valid(target): shutil.copyfile(archive, target)
    print('Verified pinned zlib:', target)
