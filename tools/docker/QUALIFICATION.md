# Native build qualification (AI-assisted)

The workstation/native-qualification branch is based on the combined PR #91
source, which includes PR #87. It does not move either PR branch or merge them.
The workflow runs on GitHub's native x86-64 Ubuntu runner because the Mac's
emulated executable-alias test observes new RX bytes but executes old code.
The independent diagnostic must pass before compilation; production Eden
checks remain enabled. A failed or incomplete job is not a qualified build.

The base workflow builds ECAM/Cemu, Azahar, melonDS, RADV, and Eden. Separate
workflows build Porpoise/Mupen64Plus/FCEUmm and N64/NES/GB-GBC/GBA using the ECAM
source overlays. Their output is a build artifact, not a complete integrated app
or a release. All seven runtime jobs and the base job must pass before complete
bundle assembly. There is no deployment, merge, publishing, or PS5 hardware test
step. The combined workstation branch collects the build tools without moving
the PR branches or resetting the Mac's existing build directory.

Deleted-source recovery preserves the pinned UI file SHA-256 values and license.
Those exact UI files are retained in recovery/ui from the user's local archive.
The Lapy mirror is checked against its exact pinned commit; the SDK mirror is
checked against the original SDK archive SHA-256. No replacement release eboot
is used. Upstream sources and licensing remain unchanged.

Review evidence must distinguish host checks, PS5 cross-compilation, packaging,
and attended PS5 4.50 hardware tests. None of the latter is implied by CI success.
