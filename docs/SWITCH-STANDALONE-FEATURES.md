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
  user. It never creates, renames or deletes a profile and never rewrites profiles.dat.
- Switch Settings exposes the existing whole-save USB backup page. Per-game Saves supports
  title-specific discovery, backup, restore and move-aside across account save directories.
- Per-game export excludes other games, profile system data and launcher settings. Restoring
  uses ECAM's existing file-based restore semantics, retaining replaced files first. It does
  not provide a raw-folder or Ryujinx import wizard or cloud synchronization.
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
