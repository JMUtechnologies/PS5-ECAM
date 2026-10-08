#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Where the app's CPU time went, from the sampling profiler's files (port/ps5/profiler.h).

    profile-report.py ELF LOGS [--top N] [--thread NAME]

ELF is the unstripped build/cemu/ps5cemu.elf of the build that ran; LOGS the folder with boot.log,
profile.bin and profile-threads.txt copied from /data/ps5cemu/logs. With "profile": true in
ps5cemu.json the app samples every few milliseconds of its CPU time the thread that used it, so a
thread's share of the samples is its share of the CPU, and its samples are where it spent it.

For each thread, busiest first: its functions by the samples taken in them ("self"), and by the
samples taken in them or in what they called ("in", from the code addresses found on the stack:
the code has no frame pointers, so it is close, not exact). A sample outside the eboot's code is
in a system library (a system call, the C library) or in code Cemu recompiled; it is shown under
the eboot function it was called from.
"""

import argparse
import bisect
import collections
import os
import re
import shutil
import struct
import subprocess
import sys

ANCHOR = "PS5Cemu_ProfileSignal"
SAMPLE = struct.Struct("<8Q")  # rip, thread, six code addresses from the stack


def tool(name):
    for candidate in (f"{name}-18", name):
        if shutil.which(candidate):
            return candidate
    sys.exit(f"{name}-18 (or {name}) is needed: apt install llvm-18")


def short(name):
    """A function's name without its parameters' types or LTO's suffix."""
    name = re.sub(r" \(\.llvm\.\d+\)$", "", name)
    depth, cut = 0, None
    for i, c in enumerate(name):
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        elif c == "(" and depth == 0 and i > 0 and not name.startswith("(anonymous", i):
            cut = i
    return name[:cut] if cut is not None else name


def functions(elf):
    """The ELF's functions, sorted: (addresses, names), and the code's end."""
    out = subprocess.run([tool("llvm-nm"), "-C", "--defined-only", "-n", elf], capture_output=True, text=True,
                         check=True).stdout
    starts, names, anchor = [], [], None
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) < 3:
            continue
        address, kind, name = int(parts[0], 16), parts[1], parts[2]
        if name == ANCHOR:
            anchor = address
        if kind in "tTwW":
            starts.append(address)
            names.append(short(name))
    if anchor is None:
        sys.exit(f"{ANCHOR} is not in {elf}: not a build with the profiler")
    headers = subprocess.run([tool("llvm-readelf"), "-lW", elf], capture_output=True, text=True, check=True).stdout
    code_end = 0
    for line in headers.splitlines():
        fields = line.split()
        if fields[:1] == ["LOAD"] and "E" in fields[6:-1]:
            code_end = max(code_end, int(fields[2], 16) + int(fields[5], 16))
    return starts, names, anchor, code_end


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("elf")
    parser.add_argument("logs")
    parser.add_argument("--top", type=int, default=25)
    parser.add_argument("--thread", help="only the threads whose name has this in it")
    parser.add_argument("--callers", metavar="FUNCTION",
                        help="instead: the functions FUNCTION's samples were called from, by samples")
    args = parser.parse_args()

    starts, names, anchor, code_end = functions(args.elf)
    with open(os.path.join(args.logs, "boot.log"), errors="replace") as log:
        found = re.findall(r"\[profile\] .*handler at (0x[0-9a-f]+)", log.read())
    if not found:
        sys.exit("boot.log has no [profile] line: the profiler did not run")
    offset = int(found[-1], 16) - anchor  # where the eboot's code is

    thread_names = {}
    with open(os.path.join(args.logs, "profile-threads.txt"), errors="replace") as threads:
        for line in threads:
            parts = line.rstrip("\n").split(" ", 3)
            if len(parts) >= 3:
                thread_names[int(parts[0], 16)] = parts[3] if len(parts) > 3 and parts[3] else f"thread {parts[0]}"

    def function(runtime):
        """The function at a runtime address, or None outside the eboot's code."""
        address = runtime - offset
        if address < 0 or address >= code_end:
            return None
        index = bisect.bisect_right(starts, address) - 1
        return names[index] if index >= 0 else None

    data = open(os.path.join(args.logs, "profile.bin"), "rb").read()
    count = len(data) // SAMPLE.size
    per_thread = collections.defaultdict(lambda: {"samples": 0, "self": collections.Counter(),
                                                  "in": collections.Counter(), "regions": collections.Counter()})
    cache = {}

    def named(runtime):
        if runtime not in cache:
            cache[runtime] = function(runtime)
        return cache[runtime]

    if args.callers:
        # the first eboot function on the stack, other than FUNCTION itself, of each of its samples
        callers_of, total = collections.Counter(), 0
        for i in range(count):
            rip, thread, *callers = SAMPLE.unpack_from(data, i * SAMPLE.size)
            if args.thread and args.thread not in thread_names.get(thread, ""):
                continue
            if named(rip) != args.callers:
                continue
            total += 1
            caller = next((f for f in (named(c) for c in callers if c) if f and f != args.callers), "?")
            callers_of[caller] += 1
        print(f"{total} samples in {args.callers}, {100 * total / max(count, 1):.1f}% of the app's CPU time; called from:")
        for function_name, n in callers_of.most_common(args.top):
            print(f"  {100 * n / max(total, 1):5.1f}%  {function_name[:150]}")
        return

    for i in range(count):
        rip, thread, *callers = SAMPLE.unpack_from(data, i * SAMPLE.size)
        stats = per_thread[thread_names.get(thread, f"thread {thread:x}")]
        stats["samples"] += 1
        here = named(rip)
        called_from = [f for f in (named(c) for c in callers if c) if f]
        if here:
            stats["self"][here] += 1
        else:
            # a system library's code, or what Cemu recompiled: told apart by where it is
            stats["regions"][rip >> 24] += 1
            caller = called_from[0] if called_from else "?"
            stats["self"][f"[outside the eboot, {rip >> 24 << 24:#x}] <- {caller}"] += 1
        for f in set(([here] if here else []) + called_from):
            stats["in"][f] += 1

    # threads of one name (a pool) together
    pools = collections.defaultdict(lambda: {"samples": 0, "self": collections.Counter(), "in": collections.Counter(),
                                             "regions": collections.Counter(), "threads": 0})
    for name, stats in per_thread.items():
        pool = pools[re.sub(r"\d+$", "#", name)]
        pool["threads"] += 1
        pool["samples"] += stats["samples"]
        for key in ("self", "in", "regions"):
            pool[key].update(stats[key])

    print(f"{count} samples, eboot code at {offset:#x}")
    for name, stats in sorted(pools.items(), key=lambda item: -item[1]["samples"]):
        if args.thread and args.thread not in name:
            continue
        total = stats["samples"]
        if total < count * 0.002 and not args.thread:
            continue
        print(f"\n== {name} ({stats['threads']} thread{'s' if stats['threads'] > 1 else ''}): "
              f"{total} samples, {100 * total / count:.1f}% of the app's CPU time")
        print("  self:")
        for function_name, n in stats["self"].most_common(args.top):
            print(f"    {100 * n / total:5.1f}%  {function_name[:150]}")
        print("  in (self and what it called):")
        for function_name, n in stats["in"].most_common(args.top):
            print(f"    {100 * n / total:5.1f}%  {function_name[:150]}")
        if stats["regions"]:
            print("  outside the eboot, by 16 MB region:")
            for region, n in stats["regions"].most_common(6):
                print(f"    {100 * n / total:5.1f}%  {region << 24:#x}")


if __name__ == "__main__":
    main()
