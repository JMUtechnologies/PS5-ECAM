#!/usr/bin/env python3
"""Diagnose x86 executable-alias rewrite behavior independently of Eden."""
import ctypes
import mmap
import os
import platform

if platform.machine() != 'x86_64':
    raise SystemExit('This diagnostic requires the x86_64 build container')
libc = ctypes.CDLL(None, use_errno=True)
libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int,
                      ctypes.c_int, ctypes.c_int, ctypes.c_long]
libc.mmap.restype = ctypes.c_void_p
libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
size = mmap.PAGESIZE
fd = os.memfd_create('ecam-jit-host-check')
os.ftruncate(fd, size)
views = []
try:
    for prot in (mmap.PROT_READ | mmap.PROT_WRITE,
                 mmap.PROT_READ | mmap.PROT_EXEC):
        address = libc.mmap(None, size, prot, mmap.MAP_SHARED, fd, 0)
        if address == ctypes.c_void_p(-1).value:
            raise OSError(ctypes.get_errno(), 'mmap failed')
        views.append(address)
    rw, rx = views
    ctypes.memmove(rw, b'\xb8\x2a\x00\x00\x00\xc3', 6)
    execute = ctypes.CFUNCTYPE(ctypes.c_int)(rx)
    before = execute()
    ctypes.memmove(rw + 1, b'\x54\x00\x00\x00', 4)
    visible = ctypes.string_at(rx, 6).hex()
    after = execute()
    print(f'Host: {platform.platform()}', flush=True)
    print(f'Initial return={before}; RX bytes after rewrite={visible}; rewritten return={after}', flush=True)
    if before != 42 or visible != 'b854000000c3' or after != 84:
        raise SystemExit('FAIL: executable-alias behavior is unsuitable for this JIT check; do not bypass Eden checks')
    print('PASS: independent executable-alias rewrite behaves correctly')
finally:
    for address in views:
        libc.munmap(address, size)
    os.close(fd)
