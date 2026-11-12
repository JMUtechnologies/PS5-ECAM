# Roadmap

What is left for Playtendoh to do, and nothing that is done: first what stands between **6.0** and its
release, then the console checks and stability work still open from the October rebuild, a release that
gives each emulator a folder of its own, and the longer list of ideas to pick from. Written 23 October
2026; on 9 November 2026 brought up to 6.0, and what was done taken out.

## 6.0: Playtendoh

The app's first release under its new name (Playtendoh), on the working branch `integration-6.0`;
`PS5-ECAM` takes it once a build of it has been played on a console. Its code is written; nothing of it
has run on a PS5 yet.

### Before it ships, in order

1. **Switch 2 on Lichii's newest** (the owner, 9 November: "better than what is already out there; the
   PC build of Lichii runs stuff very well"). EglantinePS5's core is Lichii from 1 November with the
   port's 59 PS5 fixes over it; Lichii's `nx2` is 1,137 commits past that. Pulling Lichii's newer work
   into that base was measured (53 of its 360 changes fit) and set aside for a port onto Lichii's newest
   commit. Where it stands: the Vulkan port's 69 text anchors all match Lichii `d2eaae4`
   (`tools/prepare-vulkan-port.py`; where Lichii now does the same itself, Lichii's is kept: its compute
   read barrier, its draw submission policy with the PS5's 512-draw cap, its scratch-buffer release,
   its 16 MiB VMA blocks, its renamed pipeline cache writes). Left: the configure-time files
   (`headless/*.cmake`), the 54 of the 59 core fixes that conflict (the PS5 memory work first), a
   compile, the port's own checks, then the pin in `tools/deps.json` and `patches/eglantine` remade.
   The port's own core stays pinned until then: EglantinePS5 `972abeb` (10 November, Ocarina of Time into gameplay).
2. **Translations:** the new strings in all 27 languages; 12 are done (`tools/lang.py check` lists the
   rest).
3. **One full build**, every step of `.github/workflows/tests.yml`, and the PC preview.
4. **On a console:** the launchers and the first start's question, the Buttons on each side, the
   GameCube and Wii pages, the reports' question, a Switch 2 game (a NUP one too), and the list under
   *To check on a console* below.
5. **Then** `PS5-ECAM` takes it, the release, and the branches cut back to `PS5-ECAM` and `releases`.

### After it, as the owner set out

- **Builds for PC, Mac, Linux and Android** with this release, once a 6.0 build has been confirmed good
  on a console (the owner, 9 November): see *Later: Playtendoh beyond the PS5*.
- **The systems and features still missing** (the owner, 9 November: "add support for any console or
  feature named we don't support"): the Nintendo 64DD, the Sufami Turbo, DSi-only games ([D2](#d2)),
  e-Reader cards, and a look at the Color TV-Game (dedicated consoles with no game files: only if an
  emulator plays them). **Arcade is less of a concern** (the owner, same day): PlayChoice-10, Nintendo
  Super System, Aleck64, Triforce and Nintendo's own boards stay on this roadmap, after the rest.
- **Lichii's AMD work:** the PS5's GPU is AMD's; what Lichii has merged comes with its newest core.
  Its unmerged AMD branches (`capy/amd-prep-*`) to look at once the port builds.
- **The PS5 fixes the port leaves out at first**, each brought back measured on a console: the core
  fixes Lichii does not have in its own form, and the port's timers and probes that Lichii's code no
  longer has a place for (the draw phases, the shader costs, the configure phases).

## Now: finishing the rebuild

What is left of the October 2026 rebuild:

### To check on a console

None of the October rebuild has run on a PS5 yet: the PC preview draws the launcher, and the rest was
checked by reading and compiling. Before a release:

1. **3DS and DS back to the library:** quit a game, play another of the same side, quit again; then a
   Wii U game after a 3DS one (that one starts the app over, `--rom`), and Resume where you left off
   after it. The boot log says `back to the library in this process` each time.
2. **Screen gap:** the quick action on the 3DS and DS, in every layout and turned like a book; the
   touch screen still under the cursor.
3. **Graphic packs:** downloaded on the first Wii U start without them (`[packs]` in the boot log),
   and the packs a game has on still on after.
4. **An update from 5.0.1:** it installs, and `assets/graphicPacks` and the TGA borders are gone from
   the app's folder afterwards (moved into `previous-version`).
5. **Network shares:** a side on a share opens at once with "As found last time"; Look for games
   now finds a game added since.
6. **3DS borders** (now PNG) show in a game.
7. **Update timing (#78):** Check for updates answers in about a second; an update takes a minute or
   two, not 20 to 30, with no "0 of 0 MB" step; the boot log's `files unpacked in` and `moved into
   place in` give the install's own times (32.5 s and 17.7 s one at a time). Go back in Settings >
   Online still puts the release before back. Box art, the graphic packs, RetroAchievements and the
   Switch rooms still download.
8. **Play time:** a game played a few minutes shows them in the library and on its page, on each
   side; for the Switch, after the app comes back from Eden (`played for` in the boot log). Then
   **Settings > Play time** adds them up.
9. **Hide a game and Surprise me:** hide a game from its Options menu, start the app again: still
   hidden, found by Search and (with Settings > Launcher > Library filters on) the Hidden filter; Back
   in the Library. Surprise me from Sort, on a side with a filter in use.
10. **The DS's stylus stick:** Settings > Screens > Touch > The right stick as the stylus, then Metroid Prime
   Hunters: the aim follows the stick, and lets go with it. Off, the right stick's buttons work again.
11. **Light bars:** a Wii U game with two to four players, each DualSense in its own colour, and
   again after one is turned off and on. For L8, the boot log's `[pad] P1: device data` lines, with
   the battery's level as the PS5's menu shows it, to find which byte it is.
12. **The bar:** Library and Settings beside P1 and the clock, on every side and with four players.
13. **Switch updates and DLC** (`patches/eden/0018`): a game with its update and a DLC in a folder of
   its own on a USB drive chosen as the games folder; one loose in `roms/` with them beside it; one
   whose update has no title ID in its name. Each shows the update's version on its title screen and
   has its DLC, and Eden's log says `Update and DLC beside the game` for the loose ones.
14. **Everything on a USB drive** (`patches/eden/0019`): a first start of the app installed on a drive,
    with no `/data/ps5cemu` and no Switch files on the console: `boot.log`'s `[paths]` line names
    `<drive>/eden`, a Switch game starts with keys, firmware and its update there, its save is kept
    there, and it is still there after the app starts again. Then an install from before on a drive
    whose Switch files are in `/data/eden`: they stay in use.
15. **An older copy left in `/data/homebrew`** with the app mounted from a USB drive (an image too):
    leaving a Wii U game, and an update, start and update the copy on the drive.

### Stability

- **The Wii U side still starts the app over** after a game: Cemu keeps VideoOut (its GPU thread owns
  the swapchain until the process ends). Find whether `LatteThread_Exit` can hand VideoOut back, as
  Azahar and melonDS do, so the Wii U side comes back in place too; until then the restart stays,
  with its known ways of failing (#60, #68).
- **Upstream Switch fixes** (open PRs on Swordpdf/ProsperoEden, none tested on a PS5 by its author):
  taken as `patches/eden/0012` to `0014` (`21b8d87`): #287 (several updates of one game: the newest's
  version and control data), #289 (a full audio ring drops its oldest sound instead of stopping the
  game) and #298 (ExecuteProgram, for Metal Gear Solid: Master Collection's launcher). Check each on a
  console. Not taken yet: #285 (NSZ and XCZ games), #290 (a forwarded game moved into a game folder);
  take each once it is merged or tested on a console.
- **Switch games on 8.60** ([#72](https://github.com/premohq/PlayTendoh/issues/72)): one report that
  they did not start on an earlier version; no log yet.
- **Wii U unpacked games found but not listed** (Discord: meundies 5 games in the folder, 2 listed;
  Sanji, Sx): no log yet. `patches/cemu/0046` makes Cemu's log say why each title it leaves out was
  refused; a likely cause is `Code`/`Content`/`Meta` named with capitals, which the scan accepts and
  the PS5's storage then does not find.
- **Wii U account names in games** (Discord, Lycnus): Rename now writes the name into the Mii data
  games read (`patches/cemu/0045`). Tekken Tag Tournament 2 showed `player1:default`, Cemu's first
  account's name, even with a new account: check on a console which account it ran on.
### On GitHub

**The open issues on 9 November 2026**, each with what is left:

- **Fixed in 6.0's code, to close once it has run on a console:** #103 (120 Hz kept while the display
  settles), #104 (a save from standalone ProsperoEden), #105 (a stop's teardown time), #106 (update and
  DLC from any folder), #107 (the DS's RetroAchievements hash), #108 (Loader status 28 in words), #93
  (the touchpad's shortcuts with a Pro Controller), #94 (frame generation leaves the corner screen and
  menus alone), #96 (installs finish), #88 (real Wii Remotes through a DolphinBar), #82 (GameCube and Wii
  through Porpoise), #81 (Lossless Scaling's frame generation on the Switch).
- **Waiting on a console run:** #97 (a run past ten minutes, `patches/eden` 0020), #92 (Tears of the
  Kingdom in the Depths: its Eden logs).
- **Planned:** #101 (Switch games on a share: [N0](#n0)), #100 (the DS's 3D above its resolution:
  [D5](#d5)), #99 (Lossless Scaling on the other sides: [F1](#f1)), #102 (a controller profile for each
  game: [G7](#g7); 6.0's in-game Buttons set them for the side), #110 (two players on a left and a right
  Joy-Con: the Controller type is one for every player, `headless/controller_applet.h`), #95 (the second
  screen's transparency).
- **Waiting on their reporters:** #90 (3DS games' errors), #72.

### Launcher 6.0: what is left of its pictures

**The Timeline's real discs and cartridges.** The owner, 5 November 2026: the Timeline should show each
game's own disc, cartridge or card, as images from a database, instead of box art on a drawn shape
(`port/frontend/shell/timeline.cpp` draws the shape, its label the game's box art). GameTDB has discs
(`art.gametdb.com/<system>/disc/<region>/<ID>.png`, the 3D boxes in `cover3D`: checked on the Wii U, and its
Wii pages list them too), for what it covers, so the Wii U, the Wii and the GameCube can start there. The
cartridges and cards of the other systems have a source to find: a database with no account or key first,
as the screenshots below (`thumbnails.libretro.com` has box art, title screens and snaps, no media). The
drawn shape stays for a game with no picture. Done for the DS, 3DS and Switch cards (GameTDB's), and for the
libretro sides' cartridges, from the region each game file's name gives: the LaunchBox Games Database's
"Cart - Front" pictures (no account; `tools/render-carts.py` makes the table, the app fetches the pictures
from `images.launchbox-app.com`), which also gives those games their years on the ruler; a Game & Watch
game is its own unit, from the same pictures. Left: the Disk Card, the Satellaview's Memory Pak, the iQue
Card and VS. boards, which it has few or none of;
and a drawn Famicom or Super Famicom shape for a Japanese game it has no picture of.

**Orbit's background, the gaps:** the 3DS has no save-state picture yet (its frame needs reading back from
the GPU), the Wii U and the Switch have no save states to take one from, and Game & Watch has no screenshot
set anywhere without a key. Switch 2 games newer than blawar/titledb (Pokémon Pokopia, Kirby Air Riders,
Mario Tennis Fever, November 2026) have no screenshot until it lists them. IGDB has every console, its
pictures public (`images.igdb.com`), but its API wants a Twitch key, so only as a table made at build time.

### Settings for each game, picked by the app

The owner, 6 November 2026: **per-game settings on every side, with the emulator picking what suits each
game best**, so a game plays well the first time with nothing to set. The player's own choice always
wins: a row they changed stays theirs, and each game can go back to its picks.

- **What each side has to start from:** Porpoise's Picks and its per-game profiles on the GameCube and Wii
  (`recommended.ini`, `auto_picks`, a game's `game-settings/<ID>.ini`), which this can follow; a Wii U
  game's graphic packs and profile; a Switch game's own settings and performance switches; the classics'
  core options.
- **Where the picks come from:** a table in the app by game ID, as `port/app/gametdb` is, written from what
  is known to work (the compatibility list, the emulators' own per-game settings, players' reports).
- **What it has to respect:** nothing unmeasured turned on by itself (the rule for performance
  experiments), and a pick that makes a game worse has to be easy to report and take back.

### Every Nintendo system

The owner, 29 October 2026: **Playtendoh's scope is every Nintendo system there is an emulator for,
the obscure ones too, and Nintendo's arcade hardware.** The side switch is a carousel now, so the bar
takes any number of systems.

**How: libretro cores behind Playtendoh's own UI.** One libretro frontend of the app's runs every
core, and each system looks and works as the others do: its side in the carousel, its library with
box art, the game menu, the in-game menu (its screens, picture, volume, controls, save states, back
to the library) and its settings, all Playtendoh's. Adding a system is then mostly a core and a
library entry (its file types, its box art's source, its default folder), so more or less every
emulator libretro has comes in the same way. What the frontend has to do once, for all of them:

- **The core's calls:** `retro_load_game`, `retro_run` once a frame, the video it hands over
  (software frames, and a Vulkan context for the cores that render with one: ParaLLEl-RDP for the
  N64, Dolphin), the audio batches at the core's rate, `retro_get_memory_data` for the save RAM
  written to the system's saves folder, `retro_serialize` for the in-game menu's save-state slots,
  and the core options as rows of the system's settings.
- **The DualSense** mapped to the RetroPad per system, with the touchpad's shortcuts as on the other
  sides (Touchpad click + Options for the in-game menu).
- **Where a core lives:** linked into one runtime program started as Eden is (`retro/eboot.bin`,
  LoadExec, back to the launcher when the game ends), or loaded as a module if the PS5 lets the app
  load one (the payload SDK has a `dlfcn.c`: to check). A program per few cores is the fallback.
- **Its own core per system first, the best one** (below), and a second core as a choice where one
  is faster.

**Each its own look**, as the first four sides have theirs: its colour (its row in `kSystems`: the side's
accent, its tile, the libretro in-game menu's), its controller drawn on its tile and in the mark at the
top left (every console's in a ring), and a backdrop motif of its own (`port/frontend/shell/consoles.cpp`:
the GameCube's cubes, the Wii's ripples, the N64's turning triangles, the SNES's four buttons, the NES's
pad and scanlines, the GBA's streaks, the GBC's coloured pixels, the Game Boy's dot matrix, the Virtual
Boy's red floor of lines, the Pokémon mini's bolts).

**The classic systems already added** (JMUtechnologies, PR #87 and PR #91) are to run on the best
emulator there is for each, replaced where they do not, maybe as libretro cores. This is the N64, the
NES, the Game Boy, the Game Boy Color and the Game Boy Advance. Not GameCube and Wii (Dolphin, through
Porpoise), and not the four sides the app began with (Wii U, 3DS, DS, Switch).

| System | Runs now (RETRO-INTEGRATION.md, GAMEBOY-INTEGRATION.md) | The best to try |
|---|---|---|
| N64 | PS5N64X (ParaLLEl-N64), Mupen64Plus as the other choice | Mupen64Plus-Next with ParaLLEl-RDP and ParaLLEl-RSP, ParaLLEl-N64's successor |
| NES | PS5NESX (FCEUmm), FCEUmm as the other choice | Mesen, the most accurate; Nestopia UE next |
| GB, GBC | PS5GBCX (mGBA 0.10.5's GB core) | SameBoy, the most accurate; Gambatte next |
| GBA | PS5GBAX (mGBA 0.10.5) | mGBA stays, at its newest |

**Still to add**, with the emulators to look at first (to check, each, for what it runs today):

| System | Emulators |
|---|---|
| SNES / Super Famicom, with Satellaview and Sufami Turbo | bsnes (the most accurate), Mesen 2, Snes9x for speed; Sufami Turbo needs Mesen 2 (the pinned Mesen-S has none) or a core loaded with its BIOS and two carts (subsystems, which the frontend does not have yet) |
| Super Game Boy | bsnes; SameBoy's SGB mode (mGBA's plays it now) |
| Nintendo 64DD | ares, Mupen64Plus-Next: an N64 core in the frontend first (ParaLLEl-RDP wants a Vulkan context, which the frontend does not give cores yet) |
| Game & Watch | MAME's handhelds (gw-libretro's simulator plays them now) |
| e-Reader | mGBA, with the GBA: its libretro core cannot be given cards to scan |
| iQue Player | MAME, where it runs it, or the N64's core once the frontend has one |
| Color TV-Game | none known: discrete logic with no game files; to check MAME before promising it |
| Arcade: Donkey Kong, Donkey Kong Jr., Mario Bros., Popeye, Punch-Out!!, Radar Scope, Sheriff, Space Fever and the rest of Nintendo's own boards | MAME |
| Arcade: VS. System and VS. UniSystem, PlayChoice-10, Nintendo Super System | Mesen and FCEUmm (VS.), MAME |
| Arcade: Aleck64 (N64-based), Triforce (GameCube-based) | MAME (Aleck64); Dolphin's Triforce work, if any is current (upstream Dolphin has none) |

**Arcade is less of a concern** (the owner, 9 November 2026): the home systems above come first.

The arcade boards wait on **MAME** (GPL-2.0-or-later, so it can be linked; FBNeo's licence forbids that),
built with only Nintendo's drivers: its own build system and the libretro OSD made to work with the PS5's C
library, a piece of work of its own.

- **The donor apps give way to the frontend**: PR #87's and PR #91's systems move onto it, one at a
  time, as their best core runs there. Porpoise already runs Dolphin as a libretro core, so its
  frontend code is the place to start from (GameCube and Wii stay on Dolphin).
- Each core measured on the console, speed and accuracy, before it becomes a system's default
  (performance changes stay off until measured). The one it replaces stays a choice until then.
- Saves keep working: a core that writes them differently converts them once, the originals kept
  aside.
- None of PR #87's and PR #91's runtimes has been built for the PS5 or run on a console yet, so the
  first step is to build and qualify them, whichever cores they end up with.
- **Room for more systems in the code:** a system is a `ps5launcher::System` and a place in
  `kBarOrder` (`frontend/launcher.h`, `shell/shell_internal.h`); arrays sized by the side count
  (the catalogue's and the settings' lists, `lastLook`, `noticeRetro`) grow with it. Which sides
  are shown is a bit each in `ui.sides`, read with an upper limit (2047 for 11 sides) that grows too,
  and an `int` holds 31 of them: past that it needs a wider mask or a list of names.

### GameCube and Wii: Porpoise X as Eden is

The owner, 31 October 2026: Porpoise X (premohq/Porpoise-X) goes the way ProsperoEdenX went for the
Switch side: its UI taken out, Playtendoh's in its place, every one of its settings and features kept.
Its work is on its `ps5-ecam` branch (as ProsperoEdenX's `ps5`), which `tools/retro/sources.json` pins;
its `main` stays upstream's (4.0 now).

2. **None of its own screens** when Playtendoh starts it: no library, settings, memory cards, setup or
   update check; its UI's assets (fonts, images, sounds, themes) neither loaded nor packaged, only what
   the emulation reads (Dolphin's `Sys`, `recommended.ini`, the per-game profiles). **Started:** `3b36a08`
   (no menu music, sounds, jingle or cover downloads), `3a4b194` (no updates, home screen tiles or first
   start of its own, its folder kept in `/data/porpoise`), and at `196e291` the GBA link (#1), real Wii
   Remotes through a DolphinBar, the Switch 2's GameCube controller and a Swedish interface (Porpoise's
   #36, #37, #34), and at `8f42dd3` all of it on Porpoise 4.5, the owner's new base (the new look, PlayStation
   Move, the Controller Hub, Triforce, VRR). At `528557b`, Phononenom's in-game menu as Playtendoh's sheet (quick tiles, one list of
   categories, Controls with the button layouts, Customize Buttons and the live controller test), its loading
   screen, any player in any GameCube port and each GBA's own screen and button mode. Its fonts and theme stay until step 4's menu replaces its in-game one, which draws with them.
3. **Its settings in Playtendoh's Settings**, the GameCube and Wii sides' and a game's own: every row of
   its `add_game_rows` (Video, Enhancements, Graphics, Audio, Controls, System, Wii Remote), with its
   controller layouts and Wii Remote setup, written to its own files by `port/retro/porpoise_settings.cpp`
   (**done:** the files), in every language. Its menus' own (themes, library views, menu music) are
   Playtendoh's launcher's. **Started:** the two sides' Graphics, Controls and System pages and a game's
   (`port/frontend/shell/porpoise_rows.cpp`, a table of Porpoise's keys), with the rows Porpoise's own
   Settings shows before Show All Settings: internal resolution, anti-aliasing, widescreen, screen filter
   and its strength, frame generation and steady 30 fps, the frame rate counter, output resolution; button
   layout, vibration, the Wii controller and pointer; volume, Resume where you left off, the system
   language. Next: the rest of `add_game_rows` behind › rows (Enhancements' advanced options, Graphics,
   Audio, Controls, Wii Remote, Emulation, Console), then your layouts and the Wii Remote's setup; until
   then Emulator settings > Open emulator settings and saves has them in Porpoise's own menus.
4. **Playtendoh's loading screen and in-game menu** drawn by Porpoise's program over its frames, as Eden's
   (`port/eden/ingame.cpp` built into it): a `port/porpoise/ingame.cpp` with the session file the
   launcher writes (`ps5eden::WriteSession`'s), recording into Porpoise's present pass
   (`src/porpoise_vk.cpp`), its rows Porpoise's in-game actions (save states, the game's settings,
   controllers and the Wii Remote's pointer, fast forward, screenshot, cheats, back to the library).
5. **To check on a console** at each step: a GameCube and a Wii game started, played, quit back to the
   library; a setting changed in Playtendoh seen in the game.

### EmulationStation, the other frontend

**Started, 30 October 2026, not built yet.** ES-DE as a choice beside the launcher (Settings > Launcher >
Start in EmulationStation, when the app has it): Mihawk's PS5 port of ES-DE 3.5.0 (`esde-port` in
`tools/deps.json`: his patches/0001 and his PS5 glue), with Playtendoh's glue in place of his
(`port/esde/ps5`), only what Playtendoh's ES-DE uses linked into the program, and `patches/esde` (ES-DE's
scraper in its menu, its notes and log lines saying what Playtendoh does). The
launcher writes ES-DE's systems and game lists from its library before each start (`port/esde/esde.cpp`),
and every game ES-DE starts plays here and goes back to it (`port/esde/handoff.h`). Checked on the host:
the files ES-DE reads (`tools/tests/test-esde.sh`, and against ES-DE's own systems file), the handoff,
both patches applying to ES-DE 3.5.0, the glue compiling. Next:

- **`make esde` the first time**: `tools/build-esde.sh` builds it over the public PS5 OpenGL SDK 1.0.2
  (darkxex's copy of BlackBearReloaded's), where Mihawk builds it over his own OpenGL tree: the first
  build says whether that holds, then a console: the start from the launcher, a game from each kind of
  side (the Wii U's and the 3DS's in the app, the Switch's and the other sides' programs) and back,
  L1 at the start, Quit.
- **Its media from the launcher's covers**, so ES-DE has box art before its scraper runs.

### Switch 2

**Moving to Lichii's newest (the owner, 9 November 2026):** see *6.0: Before it ships*, step 1.

**The core is Lichii, as its PS5 port runs it (9 November 2026):** EglantinePS5 at its main of 10 November
(`972abeb`, past v1.000.110), whose core is zortos293/Lichii at `3140648a` with the port's 71 core patches over it (its
`tools/eglantine-patches`): NUP containers, Ocarina of Time, Mario Kart World's memory work on a PS5 without
Pro memory, and the JIT's code cache in pieces. Playtendoh adds one more from upstream Lichii until the port
has it (`patches/eglantine` 0015, its core patch 0076): Ocarina of Time's helper calls, whose rejected compute
programs stopped the GPU as the game entered a scene (10 November). The owner asked on 5 November for Lichii's all-in-one
(`9b1a32dbb1`, 956 commits on); that move was written (patches/eglantine 0010, in the history at 04c38d3) and
did not configure: the port patches the core by text anchors, and Lichii's rewritten sockets and GPU thread
moved more than fifteen of them, the multiplayer hardening's thirteen among them. Lichii gains about 200 commits a day, so a newer core comes by moving the port's pin when
its author moves his, or by a port of our own made on this base and proved on a console first. Multithreaded
recording (`EDEN_NX2_PARALLEL_RECORD`) stays off until measured on a console.

**Experimental; source integration wired, build and console verification pending.** The first Switch 2
experiment used Takoroka's
eden-nx2 support in ProsperoEdenX to identify Switch 2 NPDMs and scan ExeFS/RomFS game folders. That
code remains the library's format detector; it is not the Switch 2 runtime. Switch 2 games now have
their own side and launch through the pinned EglantinePS5 child runtime. The game page marks Switch 2
**Experimental**. The requested runtime is
[EglantinePS5](https://github.com/heydemoura/EglantinePS5), not an expansion of Porpoise-X; its
upstream commit is pinned in `tools/deps.json` and built by `make eglantine` in `.deps/eglantine`, with
`patches/eglantine` (`tools/eglantine-patches.sh`). Source work now adds a separate Switch 2 side and
catalogue, key/firmware setup checks, a child-runtime build/package step, staged game launch and
return to Playtendoh, separate settings and launch-time forwarding of the supported video, audio, control
and performance values. EmulationStation also receives the side and aliases existing Switch art.
None of this has been built or run on the console yet.

Still to do:

- **Keep the Lichii-derived core working as a PS5 child runtime:** the pinned EglantinePS5 revision's
  Eglantine submodule is [Lichii](https://github.com/zortos293/Lichii) source commit
  `3140648a668aaebf87bae2dea17512fdca125ba0`. Playtendoh
  packages the PS5-targeted EglantinePS5 port, not Lichii's desktop Linux binary. Port upstream changes
  only through the PS5 toolchain and preserve Playtendoh's launch, settings, shared in-game menu, and
  return path. Keep the core and PS5-port revisions pinned; recheck compatibility when either pin moves.
- **Build and review the child runtime:** run the pinned EglantinePS5 build and Playtendoh packaging;
  fix any upstream integration or toolchain failures. Do not describe this source work as a verified
  build until the build is actually run.
- **Shared in-game menu source path:** the child build now compiles Playtendoh's existing Switch menu
  into EglantinePS5, draws it in the Vulkan window pass, routes the DualSense menu controls, applies
  supported runtime settings (including controller style), and returns to the library from the menu.
  The session-file path is forwarded so apps using an external data root can find the same menu
  session. This is source integration only: build it and have a tester verify the menu, input,
  settings, loading display and return path on a console before calling it working. Unsupported
  Eden-only controls remain unavailable on Switch 2.
- **Console report:** testers report game-folder discovery on internal and external storage, a game
  launch, keys/firmware handling, controller behavior, settings, exit/return, logs and performance.
  Do not infer compatibility or performance from source review or a successful build.
- **Performance work follows the runtime:** measure EglantinePS5 on hardware first. Revisit
  eden-nx2-specific macro compiler and sparse-image work only if Eden-NX2 remains in use and a
  console report demonstrates the need.

### Lighter

- **`sce_sys/pic1.dds`** is a copy of `pic0.dds` (8 MB): check whether the console needs it.
- **Package size in the README** for the next release, and its notes.

### Cleaner code

- `shell_internal.h` is still one class for the whole launcher, and `shell/settings.cpp` about 2,700
  lines: split the settings pages into their own files, and the Library's, the hub's and the pages'
  state out of the class.
- Host tests for what was checked by hand: `ps5net::Resolve`'s cache; run the preview's scripts in CI
  (they need lavapipe). The catalogue's round trip (archive fields, play time), the update's unpacking
  and the DS screens' places have theirs now (`test-catalog.sh`, `test-update.sh`, `test-screens.sh`).

## Next: a folder for each emulator

A release of its own, once the rebuild has shipped and run on consoles, so that a known-good version
is there underneath it. **Effort:** M, most of it the paths saved in files and testing on each kind of
drive.

### Where it stands

The paths are in the code now, on this branch, with a layout that differs from the plan below: the
four folders sit beside `/data/ps5cemu` rather than inside a new `/data/Playtendoh`, and the
launcher's own files stay in `/data/ps5cemu`.

- **Also there:** `tools/separate-systems.py`, which copies instead, on a computer, with a full backup
  checked by hashes first; the app takes its copies by the same markers.
- **Still to do:** the `Playtendoh` names below (the data folder, `PS5CEMU-HAR-data`, `ps5cemu.json`,
  the macros). **Go back** is held back after Reorganize even to a version that has the folders too:
  once one is out, compare versions there. Whether the folders stay beside `ps5cemu` or move inside a
  `/data/Playtendoh` is still the owner's choice.
- **Checked on a PC only:** host tests (`test-reorganize.sh`, `test-paths.sh`, `test-saves-paths.sh`,
  `test-eden-paths.sh`, `test-separate-systems.py`), the launcher preview's prompt and row, and a
  compile of every port source and Eden's changed files with the PS5 build's flags. To check on a
  console: Reorganize on `/data`, on a USB drive and on extended storage; Later, then the Settings row;
  a tile made before; Put back, then Go back; a power cut halfway (pull the plug during the start).

### What changes

The data folder is named after the app, and each emulator's files are in a folder named after it.
Today Cemu's lie at the top of `/data/ps5cemu`, among the launcher's:

```
/data/PS5-ECAM/
  cemu/      settings.xml, mlc01/ (installed games and saves), games/, keys.txt, graphicPacks/,
             controllerProfiles/
  azahar/    as now
  melonds/   as now
  eden/      as now
  launcher/  its settings (ps5cemu.json now), library.json, covers/, themes/, music/, network shares
  amiibo/  cache/  logs/      shared by more than one side
```

The other old names go too: `PS5CEMU-HAR-data` on a USB drive or extended storage, `ps5cemu.json`, and
in the code the `PS5CEMU_` macros and the `ps5cemu` names.

**What stays:** the app's title ID, `PPSA99360`. Every home screen tile starts the app by it, so a new
one would break them all, and a title ID can only be four letters and five digits anyway. The name on
the home screen is already Playtendoh. With the same app, an update needs no new install and leaves no
old app to delete: the in-app update replaces the files in place as it does now.

### How it goes

1. The update installs as any other.
2. At the first start of the new version, a prompt explains the change: **Reorganize** or **Not now**.
3. **Reorganize** moves the folders. They are on the same drive, so each move is a rename, not a copy:
   about 20 of them, under a second whatever the data's size (300 GB of Wii U games moves as fast as
   one file), with no free space needed. It works the same for data on a USB drive or extended storage.
4. **Not now** keeps the old layout working, and asks again later; Settings has the same button.

### What needs care

- **Paths saved in files:** Cemu's `settings.xml` (its MLC path and game folders), Azahar's config,
  Eden's `prosperoeden.json` (`game_files`), the launcher's `library.json`, and the caches keyed by path
  (`archives.bin`, `cache/switch-games.txt`, Eden's `covers/`). Each is rewritten from the old place to
  the new. `ps5paths::Relocated` (`port/app/paths.h`) already does this for data moved to a USB drive
  (#52): the same idea, for more folders.
- **Home screen tiles:** each one stores its game's full path (`--rom`, `port/app/forwarders.h`). A
  game in `/data/ps5cemu/games` would no longer be there, so the new version translates an old path when
  a tile starts it: the tiles keep working, and nobody remakes them.
- **Eden's program has its folder built in:** `kDataDir` is `/data/ps5cemu/eden` in
  `patches/eden/0003`. It changes there, and Eden is rebuilt (`tools/build-eden.sh`) and shipped in the
  same update.
- **Go back to the previous version** (Settings > Online, `ps5update::GoBack`): the release before would
  look for `/data/ps5cemu`, find nothing and start as if new (nothing lost, but it looks lost). Go back
  moves the folders back first, or is not offered once they have moved.
- **A power cut halfway through:** each move is written down before it is made, so the next start
  finishes the reorganizing or puts everything back.
- **Backups** (`port/app/saves.h`) are stored by system, not by folder, so a backup from before
  restores into the new places; only its `settings/` names the old `ps5cemu.json`.
- **Saves** live inside these folders and move with them: nothing to convert.
- **To check on a console:** reorganizing on `/data`, on a USB drive and on extended storage; tiles made
  before; Go back after reorganizing; Not now, then Reorganize from Settings; an update from the
  release before straight to this one.

## Later: Playtendoh beyond the PS5

The owner, 4 November 2026: **builds of the app for other platforms, down the road**: the PS4, Xbox
(UWP), PC (Windows), Linux, macOS, iOS and Android, and the Switch and Switch 2 themselves. The same
app on each, its launcher, library, in-game menus, settings and saves as they are here, its emulators
the ones each platform can run. The owner, 9 November 2026: **PC, Mac, Linux and Android with the 6.0
release, if possible**, started only once a 6.0 build is confirmed good on a console. Nothing is
started yet. What it needs, to look at when it starts:

- **The launcher and menus away from the PS5's own calls:** its Vulkan drawing (port/ui), the
  DualSense, the PS5's sound, files and notifications behind one small layer per platform, as
  `port/ps5` is the PS5's.
- **Each emulator's own builds for that platform** (Cemu, Azahar, melonDS, Eden, the libretro cores,
  Dolphin), pinned as here, where the platform allows them: Xbox's UWP and iOS limit JIT and
  executable memory, the Switches have their own homebrew toolchains, and the PS4 has less memory and
  an older GPU.
- **A package for each**, and the update check and its releases per platform.

## Ideas

A list of things Playtendoh could do next: fixes, quality of life, new features and a few long shots.
Written 22 October 2026, against the code at `184f41f` (5.0.1 in progress). Nothing here is planned or
promised; it is a list to pick from.

Each idea was checked against the code first, so none of them is already in the app (what was checked
and left out is at the end); the ones done since are taken out. Where an idea comes from another emulator or launcher, it says which.

**Effort:** S is a day or two, M about a week, L more than that or needs research first.

### Stability and fixes

<a id="s3"></a>**S3. A safe start for a game that keeps crashing** (M)
After two crashes in a row with the same game, offer to start it once with nothing extra: no graphic
packs or mods, no cheats, the side's default settings and the experiments off. Several closed issues were
this: #15 (Batman's 60 FPS pack), #20 (3DS cheats), #24 (a graphic pack with `content/`).

<a id="s4"></a>**S4. Close out the A/B experiments** (M)
`port/frontend/settings.h` still has flags "on in 4.0.0's test builds, for the A/B that decides it"
(`idleSpinUs`, `schedulerSpinUs`, `idleEventsUs`, `isolateCores`, `tscClocks`, `driverClockTsc`,
`asyncSubmit`, `gpuWaitSleepUs`) and others off "until a console has measured it" (`fastTls`,
`heapThreadCache`). A hidden **Diagnostics > Benchmark** could play a game for a minute each way and
write a `[bench]` summary from the `[perf]` lines, so Discord testers can send numbers. Then settle the
defaults and delete the paths that lost.

<a id="s8"></a>**S8. Updates you can undo** (M) · **Done:** [`39c2657`](https://github.com/premohq/PlayTendoh/commit/39c2657), all but the release notes before installing
`port/app/updates.cpp` moves the new files over the old ones. Keep what it replaces in a `previous/`
folder and offer **Settings > Online > Go back to 5.0.0**. While there: **Skip this version**, and the
release notes shown before **Update now** (What's new only shows after). Dolphin has update tracks for
the same reason.

<a id="s9"></a>**S9. Pause when the PS menu opens** (M)
Nothing checks the PS5's system state (no `sceSystemServiceGetStatus` anywhere), so a game runs on under
the PS menu and into rest mode. At least: pause 3DS and DS games and make an automatic save state
(G3 (done)) when the PS menu comes up or the console goes to rest mode, as most PS5 games pause there.

<a id="s10"></a>**S10. A real pause behind the Wii U and Switch menus** (L)
A known issue in the guide: a Wii U game runs on behind its in-game menu, and now a Switch game does too.
Cemu has no pause, so its cores would have to be held at the scheduler. On the Switch side, find out first
why ProsperoEden's port keeps `system.Pause()` out.

<a id="s11"></a>**S11. A compatibility list for every side, kept fresh** (S to M)
`port/app/compatibility.cpp` reads two tables, the Wii U's and the 3DS's; the DS and Switch have none.
Fetch the list at start (as the box art is) so it changes without an app update, and after a game ends,
offer a QR code for a "how it ran" report. RPCS3 shows its compatibility list in its game list the same
way.

### Quality of life: the launcher and library

<a id="l1"></a>**L1. Your own collections** (M)
The Library's filters are All, Recently added, Favourites and Graphic packs on. Add named collections
("Couch co-op", "Finish next") and ones made from GameTDB's genre and players, which the app already has
(`port/app/gameinfo.h`). ES-DE has custom collections and Daijishou has playlists.

<a id="l4"></a>**L4. More cover styles** (S to M)
`port/app/boxart.cpp` only fetches GameTDB's 2D `coverHQ` and `cover`. GameTDB also has 3D boxes, discs
and cartridges, and full covers (front, spine and back). A theme or a setting could use them.

<a id="l5"></a>**L5. Game descriptions in your language** (M)
The app carries GameTDB's English databases only (`port/app/gameinfo.h`). GameTDB has German, French,
Spanish, Italian, Japanese and others. Download the one for the menus' language when it is chosen, to
keep the app's ZIP small.

<a id="l6"></a>**L6. Delete and uninstall from the game menu** (S to M) · **Done** for installed updates and DLC: the 3DS's (#80) and the Wii U's (`ps5emu::RemoveInstalled`); deleting a game not yet
The game menu has no delete. An installed 3DS CIA (and its update and DLC), or a Wii U update in
`mlc01`, can only be removed by FTP today (#58 was about that). Hold Cross to delete, with the saves kept
unless asked.

<a id="l8"></a>**L8. The time and the DualSense's battery in a corner** (S) · **Half done:** the time is in the bar's corner; for the battery, [`821a6e3`](https://github.com/premohq/PlayTendoh/commit/821a6e3) logs the pad state's undescribed bytes, to find it on a console
As the PS5's home screen and Daijishou show them.

<a id="l10"></a>**L10. Saves for each PS5 user on the 3DS, DS and Switch** (M)
The Wii U side can give each PS5 user a Wii U account of their own (`accountPerUser`). The other sides
share one set of saves. Eden has user profiles of its own to map them to.

<a id="l11"></a>**L11. Who's player 2** (S to M)
On the Wii U, players 2 to 4 are the other signed-in users' DualSenses, with no way to choose who is
who. A sheet before
a game (or in the in-game menu) where each player presses Cross to take their place, as the Switch's
"Change grip/order" does. It matters for Nintendo Land and Mario Party 10.

<a id="l12"></a>**L12. Get themes** (M)
Themes are JSON files in `/data/ps5cemu/themes`. A page that lists a community themes repository on
GitHub and downloads one, as ES-DE's theme downloader does.

<a id="l13"></a>**L13. Set up every emulator from a USB drive** (M to L) · asked for by the owner, 2026-11-04
The Setup check (`port/frontend/shell/setup.cpp`) says what each side is missing and where to put it,
but the files still have to be copied by hand. Instead, a **Set up from USB** action there looks through
every connected drive (`ConnectedDrives()`) for what each emulator needs and puts each where it goes:
the Wii U's `keys.txt` and online files, the Switch's `prod.keys`/`title.keys` and firmware (for Eden and
EglantinePS5 alike), the 3DS's system files and `aes_keys.txt`, the DS's BIOS and firmware, the GameCube
and Wii's IPL and DSP files, and the libretro cores' BIOS (`disksys.rom`, the Super Game Boy's, the
Satellaview's), plus each side's game folders, offered as that side's games folder. Files are told
apart by name and contents (size and checksum for BIOS dumps, the keys files' entries), never by guess;
each found file is listed with where it will go before anything is copied, a file already there is kept
unless it is chosen, and the check runs again after, so each side's status shows what changed. The
first start's Setup offers it as soon as a drive is plugged in.

### Quality of life: in a game

<a id="g2"></a>**G2. Pictures and times on the save state slots** (M)
The slots show a number and whether they are empty. A small picture of the game and when it was made, as
RetroArch, Delta and DuckStation show them. The frame can be read as the remote screen reads it.

<a id="g5"></a>**G5. Touch hotspots** (M)
A DualSense button that touches one fixed point of the touch screen: a game's map, its item menu, its
"Y" button on the bottom screen. Citra and Azahar call it "touch from button".

<a id="g7"></a>**G7. Buttons for each game** (M)
A game can have its own picture, sound and speed (`GameSettingNames`), but not its own buttons, turbo or
A/B. Dolphin and Cemu on a PC both have per-game controller profiles.

<a id="g8"></a>**G8. Screenshots** (M)
A quick action or a hotkey that saves a PNG to `/data/ps5cemu/screenshots/<game>/`, a gallery on the
game's page, and one picked as the game's background.

### The Switch side

Since ProsperoEden's launcher is no longer opened from the app (`1579f25`), these went with it.

<a id="n0"></a>**N0. Games on a network share** (M, #101)
The other sides play from an SMB or NFS share because their emulators run in the app, whose file
calls go through `port/ps5/netfs_files.cpp`. Eden is a program of its own, so a `/net/...` game is out
of its reach: the Switch side's folder picker no longer offers shares, and starting such a game says
to copy it to the PS5 or a USB drive. To play from a share: link the same netfs layer into Eden's
program (`headless/ecam.cmake` already builds Playtendoh sources into it), or have the launcher copy the
game to the PS5 before Eden starts, with its progress shown. Either needs a console to try on.

<a id="n3"></a>**N3. Updates and DLC, shown** (S to M) · **Done** for the Switch (`port/eden/addons.h`): the update's version as the game shows it and the DLC count, on its page and in Recently played
The library pairs a game with the update and DLC beside it, but does not show it. Show which version
will run and which DLC is there, on the game's page.

<a id="n5"></a>**N5. Switch games from a network share** (L)
Eden cannot read shares, so the Switch side refuses them. Either Eden reads through the app's `/net`
layer (`port/ps5/netfs_files.cpp`), or the share's game gets a **Copy to the PS5** action.

### New features for each system

<a id="w1"></a>**W1. Wii U online with Pretendo** (L)
Cemu on a PC plays online through Pretendo Network with your own Wii U's `otp.bin`, `seeprom.bin` and
account files: Mario Kart 8, Splatoon, Smash. Nothing in the app wires it up. Needs a check of Cemu's
network and TLS paths on the console.

<a id="w2"></a>**W2. Import saves from USB** (S to M)
The guide explains how to copy saves from Cemu or Citra on a PC by FTP, path by path. A game's **Saves**
could import them from a USB drive itself, in the PC emulators' layouts, SaveMii's for the Wii U and
Checkpoint's for the 3DS. Apollo Save Tool does this on the PS4.

<a id="d2"></a>**D2. DSi games** (L)
melonDS plays DSi games with a DSi's own BIOS, firmware and NAND. The app refuses them today
(`port/melonds/core.cpp`: "DSi games are not supported yet").

<a id="d3"></a>**D3. DS Wi-Fi through Wiimmfi** (L)
melonDS emulates the DS's wireless, and Wiimmfi still runs Nintendo WFC for Pokémon's GTS and Mario
Kart DS. The guide lists the DS's wireless as not there.

<a id="d4"></a>**D4. The DS's LCD look** (S to M)
The DS's screen filters are Sharp, Smooth and Square pixels. Add an LCD grid, the DS screen's own
colours, and frame blending for games that flicker sprites to fake transparency, as RetroArch's handheld
shaders do.

<a id="f1"></a>**F1. Lossless Scaling's frame generation on the other sides** (L, #99)
The Switch side generates frames with the player's own `Lossless.dll` (#81), else MihawkGen; the 3DS,
DS and Wii U sides have MihawkGen alone (`port/cemu/FrameGenPS5.cpp`, Cemu patch 0042, Azahar patch
0033). Eden's LSFG is a module of its own (`video_core/frame_gen/lossless_dll.h`, which reads the DLL's
shaders, and `renderer_vulkan/present/frame_gen.cpp`, the passes), GPL-3.0 as the app is: built into the
app once, beside MihawkGen in `FrameGenPS5`, the Vulkan sides would choose it as the Switch does
(`Lossless.dll` where the Switch side reads it). The request's order: the 3DS first, then the DS (which
needs D5's Vulkan picture of its screens to have frames to give it), the Wii U last.

<a id="d5"></a>**D5. The DS's 3D above its own resolution** (L, #100)
Mario Kart DS and the other 3D games drawn at 2x to 4x, chosen as the 3DS side's internal resolution
is (the request asks for that same selector). The DS side draws its 3D with melonDS's software renderer
(`port/melonds/core.cpp`, `SoftRenderer`), which has the DS's 256x192 alone. melonDS scales its 3D only
in its OpenGL renderer (`GLRenderer`, its scale factor) or its compute renderer (OpenGL 4.3): either
needs an OpenGL context on the console beside the screens' Vulkan picture (`port/melonds/screens.cpp`),
and its frames handed over to it, as Eden's OpenGL build does with the PS5's OpenGL SDK. To measure on a
console first: the software renderer is what runs everything today.

<a id="t2"></a>**T2. RetroAchievements hardcore mode** (M)
The DS's achievements are softcore only (`port/app/achievements.h`). Hardcore turns off states, rewind,
cheats and slow speeds, and adds leaderboards. Rich presence, and the badges' pictures in the menu, can
come with it. RetroArch, DuckStation and PCSX2 all offer it.

### The DualSense

<a id="h1"></a>**H1. The DualSense's microphone as the game's** (L, needs a test first)
The DS side has "R3 held to blow". The DualSense has a microphone: Nintendogs, Spirit Tracks' pan
flute, and the 3DS's and GamePad's microphones. First check that a HEN lets homebrew open it.

<a id="h2"></a>**H2. Adaptive triggers for ZL and ZR** (S to M)
A light stop halfway down, or resistance, set for each side or game, for racing games and shooters.
Check first that the payload SDK reaches `scePadSetTriggerEffect`.

### A phone or computer as a companion

The remote touch screen's HTTP server (`port/stream`) already reaches every phone on the network. It can
do more.

<a id="p2"></a>**P2. A phone as players 2 to 4** (M)
A page of touch buttons as a controller, for Wii U party games with more players than DualSenses, as
AirConsole does.

<a id="p3"></a>**P3. A phone's motion for the GamePad** (S to M)
The GamePad's gyroscope from the phone's own, for aiming in Splatoon from the phone's screen. Browsers
may only give motion over HTTPS, so check that first.

<a id="p4"></a>**P4. The library and the logs on the phone** (S to M)
A page listing your games, and the latest `boot.log` to attach to an issue from the phone. Goes with
S2 (done).

### Bigger and stranger

<a id="i3"></a>**I3. DS local play between two PS5s** (L)
melonDS 1.0 has local multiplayer over a LAN. Two PS5s on one network as two DSes: Mario Kart DS,
Pokémon trades and battles.

<a id="i5"></a>**I5. "Where you left off" cards** (M)
With [G2](#g2), G3 (done) and [G8](#g8): the launcher's home row shows the last picture of each game,
how long ago, and its play time, and Cross resumes it, like the PS5's own activity cards. The library
would look like your games, not their boxes.

<a id="i6"></a>**I6. Cheats you can download** (M)
3DS cheats need a hand-made `<title ID>.txt` and DS cheats a `.mch`. A **Get cheats** action could fetch
them by title ID from a public cheat collection, or read an R4 `usrcheat.dat` for the DS. RetroArch has a
cheat downloader and Apollo Save Tool has online patches.

### Checked and left out

These came up and are already in the app: play time, turbo buttons, themes and your own themes, favourites
and sorting, rewind and fast forward on the DS, five save state slots on the 3DS and DS, RetroAchievements
on the DS, the community graphic packs' updater, frame generation, FSR 1, home screen tiles, network
shares, games in archives, Wii U accounts for each PS5 user, Skylanders portals and the rest of the USB
devices, the GamePad's sound on the DualSense's speaker, Artic Base and Artic Setup, the remote touch
screen, and atomic writes of the settings and catalog files.

Left out on purpose: Wii U save states (Cemu has none), PS5 trophies (out of reach for homebrew), the
3DS's rewind (Azahar has none, and its states are large), and a real Wii U GamePad (#44, closed as not
planned).
