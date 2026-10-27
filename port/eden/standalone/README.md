# Standalone readers reused by ECAM

`mods.h`, `cheats.h`, `patch_library.h`, `native_directory.h` and `ryujinx_saves.h` are copies of
`headless/` in [premohq/ProsperoEdenX](https://github.com/premohq/ProsperoEdenX) at the pin in
`tools/deps.json`, with `patches/eden` applied: the program the Switch side runs (`mods.h` has
0011's, a game folder's mods for the game's own title). Derived from Swordpdf/ProsperoEden and its
contributors, they keep their GPL-3.0-or-later notices. ECAM's own LICENSE applies to the combined work.

These header-only readers keep the launcher's mod ordering, discovery, single/multiple cheat
behavior, identifiers and mutually exclusive cheat groups aligned with the emulator it launches.
They do not contain an emulator core or bundled third-party cheats. Each is Eden's file byte for
byte: CI compares them after `tools/eden-patches.sh apply`, so a patch to one of them in
`patches/eden` comes here too (copy it from `.deps/ProsperoEdenX/headless`).

`../extras.cpp` adapts the existing standalone settings/profile formats through RapidJSON,
which ECAM already uses. It does not launch the standalone UI or alter its emulator core.

`../save_transfer.cpp` reuses the standalone plain-folder/Ryujinx index discovery while adding
bounded staged copying, destination checks and recovery handling. It does not call the donor's
unrestricted CopyTree/Import/Export helpers.
