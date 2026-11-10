## What this changes

<!-- One topic. What a player notices, and why. Link the issue or roadmap item it comes from. -->

## How it fits

<!-- See CONTRIBUTING.md. Tick what applies, and say why for anything left unticked. -->

- [ ] From a branch of my fork, rebased on the newest `PS5-ECAM` branch, fix-up commits squashed
- [ ] On docs/ROADMAP.md, or the owner agreed to it in an issue first
- [ ] Works the same on every side it can, not on one alone
- [ ] A new or changed emulator is the system's best, from its official upstream, pinned in `tools/deps.json`, run through Playtendoh's own frontend
- [ ] No hard-coded side counts or indices (`ps5retro::kSystems`)
- [ ] On screen: only the design tokens (`port/ui/tokens.json`), in the launcher's existing places and the in-game sheet (`port/app/side_menu.h`); PC preview pictures below
- [ ] Words in the app's voice (CONTRIBUTING.md, The voice), translated in every `port/lang/*.po`; `tools/lang.py check` passes
- [ ] No unrelated files rewritten (`git diff --stat` checked)

## Pictures

<!-- Anything on screen: PC preview (tools/preview-shell.sh) before and after, in English and with PREVIEW_LANGUAGE=qps. -->

## Built and tested

<!-- Exactly what was run and where: which build (make release, make eden...), which host tests, or a game on a PS5 (firmware, game, what worked, what did not). Say plainly what was not run. -->
