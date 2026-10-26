# Standalone feature audit and remaining work

Scope: the 62 ideas discussed for DS, 3DS, Wii U and Switch. A discussion item is not evidence of
a missing feature. Source baseline: ECAM `abbed4c0b893f5d221e32db92915ea771f0bd4fc` and its
ProsperoEdenX pin `a26ba788222465ec7fb94dc813958fc749734e07`.

## First implementation batch (draft, host-checked, not console-qualified)

| Discussion items | Result |
|---|---|
| 1–2 | Individual Switch cheats/mods exposed using the pinned standalone readers |
| 3–4 | Global/per-game bundled ReShade preset controls with Off/Default |
| 5 | Custom .fxp names discovered in global/game folders; existing selections retained |
| 6 | Select existing players; profile creation/editing not implemented |
| 7–8 | Existing standalone per-profile storage is reused for these controls; no separate ECAM history or full settings isolation added |
| 9–10 | Per-game ECAM USB backup/restore; generic save-folder import/export still pending |
| 11 | Ryujinx import wizard pending |
| 12–16 | RomM browsing/queue/sync/conflicts/QR pairing pending |
| 17 | Audio haptics audit/console validation pending |

The implementation boundaries and required console checks are in
[SWITCH-STANDALONE-FEATURES.md](SWITCH-STANDALONE-FEATURES.md).

## Performance items 18–34: audit before changing a core

Read-only GitHub blob comparisons with Swordpdf/ProsperoEden's `main` on 26 October 2026 found
these files identical to ECAM's pinned ProsperoEdenX **before ECAM's patches**:

| File under headless/ | Identical blob SHA |
|---|---|
| main.cpp | 5b654e2a756878aea3e7b0779bd4db4dc77dbbc9 |
| mods.h | d25b29fa26804dcac4f56b28c3041c8e857e39a9 |
| cheats.h | 26084e874c5e30c5dc861e1749c17e10d30bad67 |
| settings_store.h | f5f00335810da943a9d3c4b0e592479411d39441 |
| profiles.h | 7f9fa8c9dcc682937c16216cee3b4ccfdac609b0 |
| reshade_folder.h | 5425b1a52a1c4dadbe3ff512d6affc7d1602fc5d |
| ryujinx_saves.h | 65e855bfbd66eb40230c705bdad6ac7c02a843bb |
| shader_cache_reader.inc | 825a7b49bebe06d7798fff1a2298dc78b0d6d5a9 |

This is evidence for those files only, not proof that every generated source, core/driver pin
or runtime behavior is identical. No performance fix from items 18–34 is claimed as newly ported.
For pipeline locks/cache recovery, texture reclamation/lookahead, allocation/accounting, GPU
locks/commands, JIT/fastmem, scheduling, logging, microphone crashes and allocation diagnostics:
identify the exact donor commit, compare the generated ECAM build and dependencies, reproduce
the affected scene, then measure and qualify one change at a time. Never import a whole driver
or core just to add an already included changelog feature.

## DS items 35–43: pending development

DSi and its firmware checks, local wireless, GBA-slot/save selection, microphone options,
touch calibration, state-version handling and separate DS/DSi configuration are not implemented
by this batch. ECAM's working DS implementation stays unchanged. No separately verified PS5
standalone implementation was found that could simply be copied to fulfill all these items.

## 3DS items 44–52: pending development/audit

Microphone, local wireless, online play, live camera, targeted rendering/shader fixes,
save-state compatibility, texture diagnostics and compatibility notes remain pending.
ECAM already builds PS5_Azahar with its own patches: each proposed backport needs a concrete
commit or reproducible bug, save-state format review and console testing.

## Wii U items 53–62: pending development/audit

Targeted Vulkan/cache/recompiler/audio/controller fixes, graphics-pack validation, content
identification, per-game cache controls, touch presets and long-session memory fixes remain
pending. These were investigation areas, not ten verified missing features. A matching Cemu
upstream fix and a reproducible ECAM failure are prerequisites for any core change.

This checklist intentionally distinguishes implemented code, existing upstream functionality
and unimplemented research. None of the pending rows are marked complete by adding UI stubs.
