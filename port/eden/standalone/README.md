# Standalone readers reused by ECAM

`mods.h`, `cheats.h`, `patch_library.h` and `native_directory.h` are copied from
[premohq/ProsperoEdenX](https://github.com/premohq/ProsperoEdenX) at
`a26ba788222465ec7fb94dc813958fc749734e07`, under `headless/`. That is the exact Eden input
already pinned by ECAM, derived from Swordpdf/ProsperoEden and its contributors.
They retain their GPL-3.0-or-later notices. ECAM's own LICENSE applies to the combined work.

These header-only readers keep the launcher's mod ordering, discovery, single/multiple cheat
behavior, identifiers and mutually exclusive cheat groups aligned with the emulator it launches.
They do not contain an emulator core or bundled third-party cheats. No upstream logic was
intentionally changed. Check them against ECAM's Eden pin when changing that dependency.

`../extras.cpp` adapts the existing standalone settings/profile formats through RapidJSON,
which ECAM already uses. It does not launch the standalone UI or alter its emulator core.
