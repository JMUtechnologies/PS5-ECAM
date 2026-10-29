# Upstream references for the local console fixes

Reviewed 2026-10-29 against the PR discussions, open issue reports and this checkout. This is a local review; no upstream PR is merged, edited or published by this work. Preserve the owner's 5.0.1 theme/layout and existing saved theme selection.

| Reference | Local evidence and action |
|---|---|
| [PR 87](https://github.com/premohq/PS5-ECAM/pull/87), [PR 91](https://github.com/premohq/PS5-ECAM/pull/91) | Their source heads are ancestors of this checkout. Retain their native data roots, exact ROM/request handoff, backend-specific saves and runtime provenance. Their discussion explicitly distinguishes build qualification from pending console tests. |
| [PR 86](https://github.com/premohq/PS5-ECAM/pull/86) | The maintainer incorporated this as `2aef5d4`, with subsequent fixes, rather than GitHub marking the PR merged. Those changes are already in this checkout. In particular, `56c0d62` introduced `OwnStatus` for PS5 mounts that reject `lstat` while answering `stat`. Apply that existing compatibility rule to the retro scanner and file inventory. Do not apply the contribution twice. |
| [PR 70](https://github.com/premohq/PS5-ECAM/pull/70), [PR 71](https://github.com/premohq/PS5-ECAM/pull/71) | Maintainer discussions identify the incorporated Artic/crash-report and font commits. They are already in local history; retain those fixes and the existing fonts. |
| [Issue 78](https://github.com/premohq/PS5-ECAM/issues/78) | The existing `ps5/curl_socket.h` supplies console nonblocking sockets and `Connection: close`; updater/graphic-pack callers already use it. Avoid duplicating an already incorporated fix. |
| [Issue 96](https://github.com/premohq/PS5-ECAM/issues/96) | New Wii U installation-at-0% report. The existing installer already has four copiers and background cleanup from an earlier fix. That does not prove the new report resolved. Obtain the stall logs and reproduce with disposable content before changing installation transactions. Automatic discovery must not install, delete or reorganize the owner's updates/DLC/MLC. |
| [Issue 93](https://github.com/premohq/PS5-ECAM/issues/93) | Pro Controller menu shortcut report. Add an original-system qualification case: test touchpad+Options with both Wii U GamePad and Pro Controller profiles. Shortcut filtering currently runs through the controller reader and consumption through the emulation loop; no console fix is claimed here. |
| [Issue 97](https://github.com/premohq/PS5-ECAM/issues/97) | Reported repeat-fault counter failure in Eden. The described address/repeat logic exists in the cached pinned `guest_memory_service.inc`. It belongs to the separate Eden runtime, not the launcher. A launcher-only replacement cannot fix it. Any backport needs a targeted repeated-promotion regression, full Eden rebuild/inspection and a sustained real-console game test. The report's proposed memory-pressure tuning is not automatically accepted as a safe default. |
| [Issue 90](https://github.com/premohq/PS5-ECAM/issues/90), [Issue 92](https://github.com/premohq/PS5-ECAM/issues/92) | 3DS and Switch game-specific failures. Keep these in the console test matrix; symptoms alone do not establish a shared cause or a local fix. |
| [Issue 94](https://github.com/premohq/PS5-ECAM/issues/94), [Issue 95](https://github.com/premohq/PS5-ECAM/issues/95), [Issue 81](https://github.com/premohq/PS5-ECAM/issues/81) | Frame-generation compositing/quality and second-screen-opacity work are separate rendering changes. They are not resolved by game discovery and are not a reason to restyle the owner's theme. |
| [Issue 88](https://github.com/premohq/PS5-ECAM/issues/88), [Issue 82](https://github.com/premohq/PS5-ECAM/issues/82), [Issue 72](https://github.com/premohq/PS5-ECAM/issues/72) | DolphinBar driver request, Dolphin integration request and broad firmware-specific feedback. Bundled Porpoise covers the integration architecture; it does not establish DolphinBar support or compatibility on every firmware. |

## Emulator repositories

The per-emulator review also checked the pinned source repositories and their current default branches:

| System/backend | Primary repository | Comparison to the local pin on review date |
|---|---|---|
| Wii U | [Cemu](https://github.com/cemu-project/Cemu) | 37 newer commits; assess individual fixes against ECAM's PS5 patches |
| 3DS | [PS5_Azahar](https://github.com/mihawk-99/PS5_Azahar) | Default branch matches pin |
| DS | [melonDS](https://github.com/melonDS-emu/melonDS) | 77 newer commits; broad cartridge/renderer/API changes require compatibility review |
| Switch | [ProsperoEdenX](https://github.com/premohq/ProsperoEdenX) | Default branch matches pin |
| GameCube/Wii | [Porpoise-X](https://github.com/premohq/Porpoise-X) | Default branch matches pin |
| N64 default | [PS5N64X](https://github.com/Yahballon/PS5N64X) | Default branch matches pin |
| NES default | [PS5NESX](https://github.com/Yahballon/PS5NESX) | Default branch matches pin |
| GB/GBC | [PS5GBCX](https://github.com/Yahballon/PS5GBCX) | Default branch matches pin; [issue 1](https://github.com/Yahballon/PS5GBCX/issues/1) reports settings access crashing with a loaded ROM, with no posted fix |
| GBA | [PS5GBAX](https://github.com/Yahballon/PS5GBAX) | Default branch matches pin |
| Optional N64 | [Mupen64Plus PS5](https://github.com/TheRealRetro/mupen64plus-ps5) | Default branch matches pin; [issue 5](https://github.com/TheRealRetro/mupen64plus-ps5/issues/5) proposes libdragon/RSP/DP/cache fixes with renderer-specific testing limits |
| Optional NES | [FCEUmm PS5](https://github.com/MisterTemaki/fceummPS5) | Default branch matches pin |

Cemu references include [PR 2061](https://github.com/cemu-project/Cemu/pull/2061) (GamePad render-target masking) and [PR 2104](https://github.com/cemu-project/Cemu/pull/2104) (texture readback). melonDS references include [PR 2781](https://github.com/melonDS-emu/melonDS/pull/2781) (macOS JIT allocation), whose platform-specific change must not replace PS5 executable-memory handling, and [issue 2789](https://github.com/melonDS-emu/melonDS/issues/2789) (a newer DSi firmware-boot regression). These references are reviewed candidates or test cases, not newly applied or console-verified fixes. No matching search results in a donor repository is not evidence that it has no bugs. Exact pins and query results are recorded in `build/discovery-app/emulator-github-review.json`.

## Local hardware evidence

The first direct Super Mario 64 launch correctly selected the N64 side, but stopped at “It is not in the library”; the native N64 runtime did not start. Payload probes independently enumerated all 301 files in its game folder. These are filesystem/launch observations, not gameplay passes. The existing four N64 saves were backed up locally before testing.

The second console attempt, after the `lstat` fix, found all 296 N64 ROMs, but successful canonical-path results collapsed the merged library to one game. The next build uses stat device/inode identities, with lexical path fallback, for deduplication and lexical containment after no-link checks. It avoids canonical-path resolution for discovery and asset inventory. Tests cover refused `lstat`, collapsed `realpath`, hard-link aliases and ordinary storage. The corrected build is prepared under `build/discovery-pathfix`; installation and N64/NES retries require closing the currently running ECAM app. Record results in `build/discovery-app/hardware-results.json`; leave untested video/audio/input/save/return checks pending.

The shared MIT request bridge also uses the same fallback and directly unlinks a successfully consumed request, avoiding a second `lstat`. This source fix passes replay/symlink checks and refused-`lstat` tests. It requires rebuilding each bundled native runtime to update its embedded copy; a launcher-only upload does not install that bridge change.
