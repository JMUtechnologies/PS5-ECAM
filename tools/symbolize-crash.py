#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A crash report from the boot log, in function names and source lines.

    symbolize-crash.py ELF LOG [LOG...]

ELF is the unstripped build/cemu/ps5cemu.elf of the build that crashed (`make release` keeps each
release's as dist/ps5cemu-vVERSION.elf); LOG is a boot log (logs/boot.log, or a report's attached
one) or Cemu's log.txt, "-" for standard input. Every crash report in them is printed with its
addresses looked up: the instruction that crashed (RIP), and the code addresses found on the crashed
thread's stack, the return addresses of the calls it was in, innermost first.

The eboot is not loaded at the ELF's addresses: the report gives its crash handler's address, so the
load offset is that minus the handler's address in the ELF (Cemu's handlerDumpingSignal, or the
port's own handler in port/ps5/crash.cpp when Cemu did not run). Needs llvm-nm-18 and
llvm-symbolizer-18 (or the unversioned ones).
"""

import re
import shutil
import subprocess
import sys

# the console's machine context, as qwords (port/ps5/crash.cpp): FreeBSD's fields six qwords on
WORDS = {7: "rdi", 8: "rsi", 9: "rdx", 10: "rcx", 11: "r8", 12: "r9", 13: "rax", 14: "rbx", 15: "rbp",
         16: "r10", 17: "r11", 18: "r12", 19: "r13", 20: "r14", 21: "r15", 23: "fault address", 25: "error",
         26: "rip", 29: "rsp"}
HANDLERS = {"cemu": "handlerDumpingSignal(int, __siginfo*, void*)",
            "port": "(anonymous namespace)::Handler(int, __siginfo*, void*)"}


def tool(name):
    for candidate in (f"{name}-18", name):
        if shutil.which(candidate):
            return candidate
    sys.exit(f"{name}-18 (or {name}) is needed: apt install llvm-18")


def handler_addresses(elf):
    """The crash handlers' addresses in the ELF."""
    out = subprocess.run([tool("llvm-nm"), "-C", elf], capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) < 3:
            continue
        for kind, name in HANDLERS.items():
            # the port's handler is local to its file, and may carry an LTO suffix
            if kind not in found and (parts[2] == name or parts[2].startswith(name + " (")):
                found[kind] = int(parts[0], 16)
    return found


def reports(lines):
    """Each crash report's lines: from a "signal N at" line to the next one, or from the first
    "PS5:" line of Cemu's log.txt."""
    current = None
    for raw in lines:
        line = raw.rstrip("\n")
        text = line.split("[crash] ", 1)[1] if "[crash] " in line else line.strip()
        if re.match(r"signal \d+ at", text) or (current is None and text.startswith("PS5: fault address")):
            if current:
                yield current
            current = [text]
        elif current is not None and ("[crash] " in line or text.startswith("PS5:")):
            current.append(text)
    if current:
        yield current


def symbolize(elf, addresses):
    """Address (in the ELF) -> [(function, file:line)], inlined frames first."""
    if not addresses:
        return {}
    request = "\n".join(f"0x{a:x}" for a in addresses) + "\n"
    out = subprocess.run([tool("llvm-symbolizer"), f"--obj={elf}", "--functions=linkage", "--demangle", "--inlines",
                          "--relativenames"], input=request, capture_output=True, text=True, check=True).stdout
    results, blocks = {}, out.strip("\n").split("\n\n")
    for address, block in zip(addresses, blocks):
        lines = block.splitlines()
        frames = [(lines[i], lines[i + 1] if i + 1 < len(lines) else "") for i in range(0, len(lines), 2)]
        results[address] = [(function, location) for function, location in frames if function != "??"]
    return results


def show(elf, handlers, report, number):
    text = "\n".join(report)
    print(f"=== crash report {number}: {report[0]}")
    own = "PS5CEMU-HAR's own handler" in text
    handler = re.search(r"handler at (0x[0-9a-f]+)", text)
    kind = "port" if own else "cemu"
    if not handler or kind not in handlers:
        print("  no handler address in this report (or its symbol in the ELF): cannot place the eboot")
        return
    offset = int(handler[1], 16) - handlers[kind]
    print(f"  {'the port' if own else 'Cemu'}'s handler at {handler[1]}: the eboot is loaded {offset:#x} above the ELF's addresses")
    words = re.search(r"machine context:((?: [0-9a-f]+)+)", text)
    context = [int(w, 16) for w in words[1].split()] if words else []
    if context:
        print("  registers: " + ", ".join(f"{name} {context[i]:#x}" for i, name in WORDS.items() if i < len(context)))
    rip = context[26] if len(context) > 26 else None
    rip_line = re.search(r"rip (0x[0-9a-f]+)", text)
    if rip is None and rip_line:
        rip = int(rip_line[1], 16)
    stack = re.search(r"code addresses on the stack \(([^)]*)\):((?: [0-9a-f]+)*)", text)
    frames = [int(w, 16) for w in stack[2].split()] if stack else []
    wanted = ([rip] if rip is not None else []) + frames
    looked_up = symbolize(elf, [a - offset for a in wanted if a >= offset])

    def describe(runtime):
        address = runtime - offset
        found = looked_up.get(address)
        if not found:
            return [f"{runtime:#x}: not in the eboot's code"]
        first, *inlined = found
        out = [f"{runtime:#x} ({address:#x}): {first[0]}  [{first[1]}]"]
        out += [f"    inlined into {function}  [{location}]" for function, location in inlined]
        return out

    if rip is not None:
        print("  crashed in:")
        for line in describe(rip):
            print("    " + line)
    if stack:
        print(f"  on the stack ({stack[1]}), innermost first; a scan, so some are stale return addresses:")
        for runtime in frames:
            for line in describe(runtime):
                print("    " + line)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    elf, logs = sys.argv[1], sys.argv[2:]
    handlers = handler_addresses(elf)
    lines = []
    for log in logs:
        if log == "-":
            lines += sys.stdin.read().splitlines()
        else:
            with open(log, errors="replace") as file:
                lines += file.read().splitlines()
    found = list(reports(lines))
    if not found:
        print("no crash report in " + ", ".join(logs))
        return
    for number, report in enumerate(found, 1):
        show(elf, handlers, report, number)


if __name__ == "__main__":
    main()
