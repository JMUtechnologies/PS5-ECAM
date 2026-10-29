# Native build qualification (AI-assisted)

The workstation/native-qualification branch is based on the combined PR #91
source, which includes PR #87. It does not move either PR branch or merge them.
The workflow runs on GitHub's native x86-64 Ubuntu runner because the Mac's
emulated executable-alias test observes new RX bytes but executes old code.
The independent diagnostic must pass before compilation; production Eden
checks remain enabled. A failed or incomplete job is not a qualified build.

This first job qualifies ECAM/Cemu, Azahar, melonDS, RADV, and Eden. Additional
Porpoise/N64/NES/GB/GBC/GBA runtime toolchains and builds remain separate pending
work. Its uploaded output is a build artifact, not a complete integrated app or
a release. There is no deployment, merge, publishing, or PS5 hardware test step.

Deleted-source recovery preserves the pinned UI file SHA-256 values and license.
Those exact UI files are retained in recovery/ui from the user's local archive.
The Lapy mirror is checked against its exact pinned commit; the SDK mirror is
checked against the original SDK archive SHA-256. No replacement release eboot
is used. Upstream sources and licensing remain unchanged.

Review evidence must distinguish host checks, PS5 cross-compilation, packaging,
and attended PS5 4.50 hardware tests. None of the latter is implied by CI success.
