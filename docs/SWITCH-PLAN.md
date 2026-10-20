# A Switch side with Eden: the plan

A plan, written 20 October 2026. Nothing is built yet.

## The idea

Switch games as a fourth side of the launcher, next to Wii U, 3DS and DS. The emulator is lifted as
it is from ProsperoEden: its Eden core and its PS5 code. This app's launcher, settings and in-game
menu replace ProsperoEden's own interface. ProsperoEden already plays games on the PS5 (video, audio,
controller input and saves) on the same Mihawk Mesa/RADV driver this app uses, so the work is wiring,
not porting.

## Source and credit

| What | Project | By |
|---|---|---|
| Switch emulator | [Eden](https://github.com/eden-emulator/mirror) | the Eden team |
| PS5 port | [ProsperoEden](https://github.com/Swordpdf/ProsperoEden) (fork: [premohq/ProsperoEdenX](https://github.com/premohq/ProsperoEdenX)) | Sword ([Swordpdf](https://github.com/Swordpdf)), bawsdeep, Rockiu0 and its other contributors |
| Vulkan driver | [PS5 Mesa](https://github.com/mihawk-99/PS5_Mesa) and [PS5 Vulkan](https://github.com/mihawk-99/PS5_Vulkan) | Mihawk (already used here) |

ProsperoEden is GPL-3.0, and this app is GPL-3.0-or-later, so its code can be used as is. The GPL asks
three things: keep its copyright and licence notices, carry over its THIRD_PARTY_NOTICES.md, and keep
the source public. Sword and ProsperoEden are named in the README's Credits, on the About screen and
in the release notes. Tell Sword before the release.

## Steps

1. **Dependency.** Pin premohq/ProsperoEdenX the way Cemu and Azahar are pinned, with a
   `tools/build-eden.sh`. The fork's own `make` fetches Eden (67bada77) and the rest at pinned
   revisions. Keep the code that runs a game and leave out ProsperoEden's OpenGL launcher.
2. **`port/eden`.** A frontend beside `port/azahar` and `port/melonds` that starts a game and handles
   its frames (through the app's Vulkan), sound, the DualSense and saves. One emulator per session,
   as now.
3. **The Switch side.** The library (games, covers, continue playing), the settings that matter
   (resolution, docked or handheld, ASTC textures, controls) and the in-game menu (quit to the
   library).
4. **Files.** `/data/ps5cemu/eden`, with `keys/` (prod.keys, title.keys), `firmware/`, `games/` and
   saves. No keys, firmware or games are included: users dump their own, as for Wii U and 3DS.
5. **Credit and release.** Do the credits above, test a few games on the console, then release it as
   a beta.

## Open questions

- Memory: how much a Switch game has left beside the launcher in one process (the app restarting
  between sides helps).
- ProsperoEden's launcher drives its emulator through a contract (`launcher-runtime.json`,
  `launcher-v1.0`). This app's launcher could speak that contract instead of linking the core
  directly.
- ProsperoEden's frame generation, 120 Hz and ReShade presets: keep them, or match this app's
  options.
