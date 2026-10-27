# Standalone Switch features in ECAM: implementation and qualification

This branch exposes existing features of ECAM's pinned ProsperoEdenX through the ECAM launcher.
It does not update an emulator, graphics driver, SDK or dependency pin. It is a draft and has
not been built for or tested on a PS5. Host tests and syntax checks cannot establish console safety.

## Implemented in the draft

- Per-game Mods and Cheats pages list the user's installed files with standalone ordering and
  precedence. Individual mod switches, single cheats and multi-cheat collection choices persist.
- Master Mods/Cheats switches retain individual choices while disabled. Mutually exclusive
  groups, such as 30/60 FPS choices, use the standalone parser's rules.
- Post-processing offers Eden's six bundled ReShade names, Off and per-game Default/inheritance.
  Custom .fxp presets are discovered by their declared name/chain in user/post_presets and the
  game's reshade folder. Discovery is bounded to 256 directories, eight levels, 1024 files per
  root and 64 KiB per preset. An already configured custom name is preserved. Refresh reloads
  installed files and settings without continuous filesystem scanning in the launcher loop.
- Player profiles lists existing Eden players and remembers selection for the signed-in PS5
  user. Create and rename use a UTF-8 name of at most 31 bytes and the standalone's eight-player
  binary layout. New players start with default settings and must be selected explicitly.
  Hold Remove to remove a player from the list; their save folders and settings remain.
  At least one player must remain. The first original profiles.dat is retained as
  profiles.dat.ecam-before; malformed files and an existing staging file block writes.
- Switch Settings exposes the existing whole-save USB backup page. Per-game Saves supports
  title-specific discovery, backup, restore and move-aside across account save directories.
- Per-game ECAM backup excludes other games, profile system data and launcher settings. Restoring
  uses ECAM's existing file-based restore semantics, retaining replaced files first. It does
  is separate from the new staged plain-folder/Ryujinx transfer described below. Cloud synchronization
  remains unimplemented.
- Unknown systems no longer fall through to DS save handling. Synthetic Switch library IDs
  cannot be used to select saves. Same-second backup requests use separate directories.
- Save tests use a temporary mount root and no longer require root or delete /mnt/usb0 content.

## Data compatibility

Choices go into Eden's existing `config/prosperoeden.json`, under the active player's settings
and the same uppercase title key the standalone uses. Other keys, games and players are retained.
The remembered PS5 user takes precedence over the console-wide current player, matching the
standalone's profile resolution. ECAM's normal per-game settings stay in ps5cemu.json unchanged.

A settings edit refuses invalid JSON, unexpected object shapes, symlinked settings and files over
4 MiB. It creates an exclusive `.ecam-new` staging file beside the settings and renames it only
after a complete write. The first original is kept as `.ecam-before`. An interrupted staging
file blocks another write; inspect it before removing it. These edits are intended only while
the launcher owns the session, before Eden runs; simultaneous external settings editing is not
supported. No settings change happens by simply opening a page.

An existing player's binary profile layout is read from the pinned standalone format. Save
selection uses `user/nand/user/save/0000000000000000/<32-hex profile>/<16-hex title>/...`.
Device saves in another space are not claimed as covered. Per-game saves cover all matching
account directories, not only the selected player. Existing whole backups still include the
whole save tree and the profile system save.

## Host validation

```sh
bash tools/tests/test-eden-extras.sh
bash tools/tests/test-saves.sh
```

The first requires `.deps/rapidjson/include` (`python3 tools/deps.py fetch rapidjson`), a C++20
compiler and no console. Save tests require fmt headers. The workflow runs both and syntax-checks
the changed settings UI. Additional Mac syntax checks use Vulkan headers and a version define.
New user-facing strings currently use the launcher's normal English fallback pending translations.

## Console qualification still required

The contributor is providing source; the ECAM owner will build and test the console implementation.
Owner testing is not a prerequisite for submitting this draft code and does not imply it has passed.

Use copies of saves/settings and keep the known-good release. On a PS5, confirm:

1. Existing profiles and per-game choices match standalone behavior; unknown JSON keys survive.
2. One mod toggle changes that mod, one cheat toggle changes that cheat, and exclusive groups
   cannot leave conflicting choices selected. Confirm master-off behavior in an actual game.
3. Each bundled ReShade name resolves in ECAM's packaged Eden; per-game Off/Default and global
   settings behave correctly. Preserve custom selections.
4. Choose each existing player and verify the game opens that player's saves without moving data.
5. Export a title across two profiles, change disposable save data, restore, and compare both
   restored and retained bytes. Verify the real filesystem's case and title-directory layout.
6. Quit/relaunch, try failed/stale settings writes and a disconnected USB drive, and check that
   errors leave original data available.
7. Launch DS, 3DS, Wii U and Switch; check audio, controls, save/load and return to library.

No merge or installation is implied by the host checks.

## Plain-folder and Ryujinx save transfers

In a Switch game's Saves page, Export selected player's save writes a new
`USB/PS5-ECAM-exports/<timestamp>/<TITLE>/account` and/or `device` tree. Choose an existing
player in Switch Settings first. These files can be transferred to another emulator's save folder.

For imports, put either `<TITLE>/account` and/or `<TITLE>/device` under
`USB/PS5-ECAM-imports`, or copy a Ryujinx data folder (with bis/ and its save index) to
`USB/Ryujinx`. The selected title's entries are detected; hold the Import row to copy them.
Raw `<TITLE>/<save files>` is accepted as an account save. The Ryujinx reader uses the indexed
account/device saves, selecting the first account for the requested title, as the donor does.
It does not offer a Ryujinx-user selector.

The transfer runs on the backup worker and Finish() waits before a game can launch. Every source
is staged before either destination changes; previous directories are retained in
`eden/save-transfer-recovery/<unique>/account.before` and/or `device.before`. A commit failure
attempts rollback and reports any incomplete recovery. This is exception/error handling, not
crash/power-loss atomicity across two directories. Do not run an emulator or edit saves externally
during a transfer. Symlinks, overlapping trees, duplicate kinds and empty saves are refused;
limits are 100000 files / 4 GiB / 32 directory levels, and a 16 MiB Ryujinx index. No games,
keys, firmware, save decryption or filesystem conversion is included.

Host test: `bash tools/tests/test-save-transfer.sh`. Owner qualification must additionally
check actual PS5 mount semantics, errors, selected profile IDs, data equality, recovery and
portable export compatibility on copies of saves.

Player management owner checks: create a disposable player, select it, launch a game, rename
it and confirm its save remains accessible. Remove it and confirm the save directory and
settings remain on disk, and selection falls back to a valid player. Check the eight-player
limit and the retained original profile list. Host tests cover binary field preservation,
invalid names, duplicate identity protection, stale staging and symlink rejection; PS5 UI
and native Eden interoperability still require owner validation.
