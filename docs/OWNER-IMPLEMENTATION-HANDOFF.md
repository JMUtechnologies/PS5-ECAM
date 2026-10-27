# Four-stage contribution: owner build and test handoff

This is source development for the ECAM owner to build and test. No PS5 device access is needed
to submit the code. Host checks cannot establish runtime compatibility. Do not interpret this
document as all discussed features being implemented.

| Stage | Implemented source in this branch | Still absent |
|---|---|---|
| 1: evidence | Single/repeated-run comparisons, JSON/Markdown/HTML reports, console-check manifest, bounded frame buffer, opt-in DS CPU frame-interval CSV export | Capture hooks for other systems, automatic hardware/effective-settings sidecars, actual console data |
| 2: Switch controls | Mods/cheats/master switches, preset discovery and inheritance, profile selection/create/rename/remove, per-game ECAM backups | Full ECAM profile separation, translations for new strings |
| 3: integration | Plain-folder transfer and indexed Ryujinx import with account-save selection, staged directory replacement/recovery, shared worker/UI integration | RomM browsing/download/sync, QR pairing, multiplayer changes |
| 4: system features | Optional DS Slot-2 cartridge attachment and separate working saves, backup support | DSi boot/NAND/DSP, DS/3DS wireless, live camera/microphone, unverified core backports |

## Host checks

Fetch the test dependencies with the existing dependency tool (`pacbrew`, `pugixml`, `dr_mp3`,
`rapidjson`). Use a C++20 compiler, Python 3.10+ and fmt headers:

```sh
make test-contribution
python3 -B tools/lang.py check
```

The workflow includes the host tests and UI syntax check. Core Slot-2 changes were also
syntax-checked against melonDS's pinned 1.1 headers. The source dependency pins are unchanged.
The standard [BUILDING.md](BUILDING.md) remains the full PS5 build procedure, including its Linux
toolchain and Eden's pinned OpenGL SDK archive requirement. No PS5 binary or successful full
cross-build is claimed by this contribution. Keep compiler and linker output with the test report.

## DS Slot-2

Off by default, enabled globally or per game in DS System settings. For `Game.nds`, supply:

```text
melonds/slot2/Game.gba
melonds/slot2/Game.sav   optional starting save
```

The working save goes to `melonds/slot2-saves/Game.sav`, loaded on subsequent launches. The original
ROM/save is not written. A compressed archive uses its DS save stem (Game.tar.gz uses Game).
This exposes a GBA cartridge to DS software, including cartridge/save interactions supported by
melonDS. It does not emulate GBA execution, add a GBA library side or promise compatibility with
a particular game. The supplied ROM is capped at 32 MiB and save at 1 MiB. Enabled but unreadable
input aborts before starting the DS core. Test cartridge detection, transfers, savestates, quit,
relaunch and backup/restore with copies of inputs. Review the actual GBA save callback lifetime.

## DS capture

Create an empty `melonds/capture-frames` marker before starting a session. After the emulation
thread stops, a new CSV is written under `melonds/captures`. Remove the marker to disable.
There are at most 12000 intervals, allocated only when enabled; Record performs no allocation
or file I/O. A pause, rewind or invalid clock ordering ends the uninterrupted capture segment.
Quitting through a paused menu still exports samples already collected. Abrupt crashes/watchdog
restarts can lose the capture; this is not a crash recorder.

These are CPU timestamps after the DS frontend's Present returns, not GPU timestamps, actual
display flips, input latency or proof of emulated speed. For the comparison tools use
`frame_kind: presented`, describe `capture_method` accurately, and supply real metadata with
the exact build, scene, effective settings and device setup. Do not infer 1% lows from periodic
FPS logs or label synthetic host-test samples as console measurements. There is no automatic
metadata sidecar yet.

## Remaining research

The missing systems features require implementations, not just owner testing. For example,
DSi needs valid BIOS/NAND setup, persistence decisions, DSP/AAC and camera behavior; DS/3DS
wireless needs platform transport and synchronization; live input needs PS5 capture APIs;
RomM needs a client, authentication/configuration, queue and save conflict policy. No UI stub
or nonfunctional switch is offered for them. Implement and review those separately rather
than making this branch appear to support them.

For the performance audit and exact implemented/pending items see
[STANDALONE-PORTING-STATUS.md](STANDALONE-PORTING-STATUS.md). For Switch operations see
[SWITCH-STANDALONE-FEATURES.md](SWITCH-STANDALONE-FEATURES.md).
