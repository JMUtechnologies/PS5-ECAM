# Roadmap

What PS5-ECAM is working towards: first the rebuild and stability work still open (the launcher
rewrite of October 2026 and the issues around it), then the longer list of ideas to pick from.
Written 23 October 2026.

## Now: finishing the rebuild

The launcher is one layout now (themes are skins over it), a 3DS or DS game comes back to the library
without the app starting over, the graphic packs download on demand, a network share's library is
kept between looks, and every network call has a timeout. What is left:

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

### Stability

- **The Wii U side still starts the app over** after a game: Cemu keeps VideoOut (its GPU thread owns
  the swapchain until the process ends). Find whether `LatteThread_Exit` can hand VideoOut back, as
  Azahar and melonDS do, so the Wii U side comes back in place too; until then the restart stays,
  with its known ways of failing (#60, #68).
- **Updates and downloads that wait for minutes** ([#78](https://github.com/premohq/PS5-ECAM/issues/78),
  the real cause of [#73](https://github.com/premohq/PS5-ECAM/issues/73) and #72's "update loop"):
  dolphin78 timed every step on a console. Each request finishes in a second, then
  `curl_easy_cleanup` sits on the idle keep-alive connection until the server drops it: 30 s for
  api.github.com (the check's "is out" 30 s after start), many minutes for raw.githubusercontent.com
  (the "0 of 0 MB" and the "70 of 70 MB" waits). The cause is probably curl making the socket
  non-blocking with `fcntl`, which the PS5's sockets ignore. Their fix, confirmed on a console, sends
  `Connection: close` with every request: the update takes 74 s instead of 20 to 30 minutes, and the
  check answers in under a second. To do:
  - Apply their patch (in the issue's comment) to `port/app/updates.cpp`, onto this branch's
    version, which has the cancel and the stall timeout already. Only the ZIP's transfer moves the
    dialog's figures, so `SHA256SUMS` no longer shows as "0 of 0 MB".
  - Send the same header from every other curl user, which waits the same way after each request:
    the graphic packs (`pack_updates.cpp`: the packs "at 100%" for 8 minutes in #73), box art
    (`boxart.cpp`), RetroAchievements (`achievements.cpp`) and the Switch rooms (`eden/rooms.cpp`).
    Better: one place, beside `ps5curl::Setup`, that every session takes it from.
  - Correct the comments that blame the resolver for the check's 30 s (`ps5update::Stop`, this
    branch's `ps5/net.h`). The shared resolver and its timeouts stay, but the wait was the cleanup.
  - The install's remaining minute is unpacking 1,852 files one at a time and moving each into
    place: unpack in parallel, as `port/app/install.cpp` does (fewer files now, with no graphic
    packs).
- **A fresh build fails to fetch the boilerplate:** `tools/deps.json` points at
  `blackbearreloaded/ps5-native-app-boilerplate`, which no longer exists (noted in PR #70).
  [drakmor's copy](https://github.com/drakmor/ps5-native-app-boilerplate) has the pinned commit
  (`dd44bbdc`, checked): change the URL.
- **Switch games with a mods folder do not start** ([#77](https://github.com/premohq/PS5-ECAM/issues/77)):
  nothing of the launcher's reaches Eden's mods, so it is in Eden's program. Needs the reporter's boot
  log; compare ProsperoEdenX's mod loading with upstream ProsperoEden's newest (its new folder
  structure), port the fix and move the pin.
- **Switch games on 8.60** ([#72](https://github.com/premohq/PS5-ECAM/issues/72)): one report that
  they did not start on an earlier version; no log yet.
- **Play time is never counted:** `ps5catalog::AddMinutes` has no caller, so "played" stays at 0.
  Count a game's minutes when it ends (in place now for the 3DS and DS; across the restart for the
  Wii U and Switch, by a record the next start reads).

### On GitHub

- **PR #70** (Artic Setup: Azahar patches 0030 to 0032, fmt's prints to the boot log,
  `symbolize-crash.py --ps5`) and **PR #71** (Cyrillic and Greek from SST-Roman, #69) are already in
  the code: close both, thanking dolphin78.
- **#76**: dolphin78 traced the stripes on empty screens to the grain's float hash, the cause fixed in
  `baf9f71`; this branch also gives each list's labels one size. Close it with the release.
- **#74, #75**: done on this branch (packs downloaded when missing; a share's games kept between
  looks). Close them with the release.

### Lighter

- **Eden's `ui/` folder** (about 25 MB) goes into the app whole: find which of its files Eden's
  program still reads now that PS5-ECAM's launcher and menus replace its own, and package only those.
- **`sce_sys/pic1.dds`** is a copy of `pic0.dds` (8 MB): check whether the console needs it.
- **Package size in the README** for the next release, and its notes.

### Cleaner code

- `shell_internal.h` is still one class for the whole launcher, and `shell/settings.cpp` about 2,700
  lines: split the settings pages into their own files, and the Library's, the hub's and the pages'
  state out of the class.
- Host tests for what was checked by hand: the catalogue's round trip (archive fields), the DS
  screens' placement with the gap and rotation, `ps5net::Resolve`'s cache; run the preview's scripts
  in CI (they need lavapipe).

## Ideas

A list of things PS5-ECAM could do next: fixes, quality of life, new features and a few long shots.
Written 22 October 2026, against the code at `184f41f` (5.0.1 in progress). Nothing here is planned or
promised; it is a list to pick from.

Each idea was checked against the code first, so none of them is already in the app (what was checked
and left out is at the end). Where an idea comes from another emulator or launcher, it says which.

**Effort:** S is a day or two, M about a week, L more than that or needs research first.

### Progress

Each idea done is marked **Done** where it is listed below, with its commit.

| Idea | Commit | What it changed |
|---|---|---|
| [S1](#s1) | [`1b497d3`](https://github.com/premohq/PS5-ECAM/commit/1b497d3) | An unreadable `ps5cemu.json` is kept as `ps5cemu.json.broken`, and the launcher says so once |
| [S6](#s6) | [`6536915`](https://github.com/premohq/PS5-ECAM/commit/6536915) | **Back up to USB** takes the Switch's saves and Eden's user profiles, and restores them |
| [S5](#s5) | [`a7c6115`](https://github.com/premohq/PS5-ECAM/commit/a7c6115) | GitHub Actions runs the string table check and the PC tests at every push and pull request |
| [G1](#g1) | [`6f8dcd8`](https://github.com/premohq/PS5-ECAM/commit/6f8dcd8) | Touchpad click + Square saves a state, + Triangle held loads it (3DS and DS) |
| [P1](#p1) | [`86fff8a`](https://github.com/premohq/PS5-ECAM/commit/86fff8a) | **Add files to the PS5** on the remote touch screen's page: games, Switch keys and firmware, amiibo and music from a phone's browser |
| [S2](#s2) | [`b482092`](https://github.com/premohq/PS5-ECAM/commit/b482092) | The start after a crash says so once, with the crash's first line and Copy logs to USB |
| [G9](#g9) | [`8777160`](https://github.com/premohq/PS5-ECAM/commit/8777160) | **Fast forward, on and off** in Controls > Buttons (3DS and DS) |
| [G4](#g4) | [`4f0a5af`](https://github.com/premohq/PS5-ECAM/commit/4f0a5af) | **Undo the last load** in the in-game menu's Save states (3DS and DS) |
| [T1](#t1) | [`0f30424`](https://github.com/premohq/PS5-ECAM/commit/0f30424) | The 3DS's cameras see the newest picture in `azahar/camera` (or sent from a phone), for QR codes |
| [L7](#l7) | [`6d374e0`](https://github.com/premohq/PS5-ECAM/commit/6d374e0) | The launcher dims after five minutes with no button pressed; the waking press does nothing else |
| [S7](#s7) | [`0891a28`](https://github.com/premohq/PS5-ECAM/commit/0891a28) | Backups hold the settings too (each game's own, Cemu's, controller profiles, themes); a restore starts the app again to use them |
| [S8](#s8) | [`b436783`](https://github.com/premohq/PS5-ECAM/commit/b436783) | An update keeps the version it replaces; **Go back to** in Settings > Online; **Skip this version** on the prompt |
| [I4](#i4) | [`759f8e3`](https://github.com/premohq/PS5-ECAM/commit/759f8e3) | **3D for red and cyan glasses** in the 3DS side's Graphics (and each game's) |
| [G3](#g3) | [`1d54168`](https://github.com/premohq/PS5-ECAM/commit/1d54168) | Quit to the library saves a state; **Resume where you left off** in the game's Options menu (3DS and DS) |

Being worked on separately (not here): the Switch side's in-game menu, its settings and button
remapping ([N1](#n1)), its rooms ([I2](#i2)), ReShade on the Wii U side ([I1](#i1)), and the box art
downloader.

### Start here

The ten with the most value for the work, in order:

1. [S1](#s1) A broken `ps5cemu.json` is kept, not wiped
2. [N1](#n1) Buttons and controllers for the Switch side
3. [S2](#s2) Say so when the last session crashed
4. [G1](#g1) Quick save and quick load hotkeys
5. [G3](#g3) Resume where you left off
6. [S6](#s6) Switch saves in the USB backups
7. [P1](#p1) Add games, keys and saves from a phone or computer's browser
8. [N2](#n2) Mods and cheats for Switch games
9. [S5](#s5) CI on every push
10. [I1](#i1) Turn on the ReShade code that is already in the tree

### Stability and fixes

<a id="s1"></a>**S1. A broken `ps5cemu.json` is kept, not wiped** (S) · **Done:** [`1b497d3`](https://github.com/premohq/PS5-ECAM/commit/1b497d3)
`ps5settings::Load` returns the defaults when the file does not parse (`port/frontend/settings.cpp:200`),
and the next `Save` writes over it. Many settings are "only in ps5cemu.json", so people do edit it by
hand, and one missing comma then loses every game's own settings, the accounts by PS5 user, the theme
and the rest. Move it to `ps5cemu.json.broken`, start on the defaults, and say so once.

<a id="s2"></a>**S2. Say so when the last session crashed** (S to M) · **Done:** [`b482092`](https://github.com/premohq/PS5-ECAM/commit/b482092), without the QR code for now
`ps5/crash.cpp` writes `[crash]` lines to the boot log, but the next start says nothing. When
`boot.prev.log` ends in a crash, offer **Copy logs to USB** and a QR code (`ui/qr.cpp` makes them already)
for a GitHub issue filled in with the version, firmware, HEN, game and the crash's first lines. Dolphin
and PCSX2 both tell you after a crash; most reports here need the logs anyway.

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

<a id="s5"></a>**S5. CI on every push** (S to M) · **Done:** [`a7c6115`](https://github.com/premohq/PS5-ECAM/commit/a7c6115)
`.github` has only the issue templates. A GitHub Actions job on Ubuntu 24.04 could run what already runs
on a PC: `tools/tests` (archives, keyboard, music, netfs, stream), `tools/reshade-check.sh`,
`tools/lang.py check` and the launcher preview's screenshots. The PS5 build itself can stay manual.

<a id="s6"></a>**S6. Switch saves in the USB backups** (S) · **Done:** [`6536915`](https://github.com/premohq/PS5-ECAM/commit/6536915)
**Back up to USB** (`port/app/saves.cpp`) takes the Wii U's, 3DS's and DS's saves, but not Eden's
(`eden/user/nand/user/save`). The guide says it does not "yet".

<a id="s7"></a>**S7. Settings in the backups too** (S) · **Done:** [`0891a28`](https://github.com/premohq/PS5-ECAM/commit/0891a28)
Add `ps5cemu.json` (each game's own settings), Cemu's `controllerProfiles/` and `themes/` to a backup, so
a reinstall or a second PS5 starts where you were. They are small.

<a id="s8"></a>**S8. Updates you can undo** (M) · **Done:** [`b436783`](https://github.com/premohq/PS5-ECAM/commit/b436783), all but the release notes before installing
`port/app/updates.cpp` moves the new files over the old ones. Keep what it replaces in a `previous/`
folder and offer **Settings > Online > Go back to 5.0.0**. While there: **Skip this version**, and the
release notes shown before **Update now** (What's new only shows after). Dolphin has update tracks for
the same reason.

<a id="s9"></a>**S9. Pause when the PS menu opens** (M)
Nothing checks the PS5's system state (no `sceSystemServiceGetStatus` anywhere), so a game runs on under
the PS menu and into rest mode. At least: pause 3DS and DS games and make an automatic save state
([G3](#g3)) when the PS menu comes up or the console goes to rest mode, as most PS5 games pause there.

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

<a id="l2"></a>**L2. Hide a game** (S)
For demos, homebrew and the clutter of a big folder. The game menu has no way to hide one. ES-DE has a
hidden flag.

<a id="l3"></a>**L3. Surprise me** (S)
A random game from the side (or a collection). ES-DE and Daijishou both have one.

<a id="l4"></a>**L4. More cover styles** (S to M)
`port/app/boxart.cpp` only fetches GameTDB's 2D `coverHQ` and `cover`. GameTDB also has 3D boxes, discs
and cartridges, and full covers (front, spine and back). A theme or a setting could use them.

<a id="l5"></a>**L5. Game descriptions in your language** (M)
The app carries GameTDB's English databases only (`port/app/gameinfo.h`). GameTDB has German, French,
Spanish, Italian, Japanese and others. Download the one for the menus' language when it is chosen, to
keep the app's ZIP small.

<a id="l6"></a>**L6. Delete and uninstall from the game menu** (S to M)
The game menu has no delete. An installed 3DS CIA (and its update and DLC), or a Wii U update in
`mlc01`, can only be removed by FTP today (#58 was about that). Hold Cross to delete, with the saves kept
unless asked.

<a id="l7"></a>**L7. A screensaver for OLED TVs** (S) · **Done:** [`6d374e0`](https://github.com/premohq/PS5-ECAM/commit/6d374e0), as a dim (no slideshow yet)
Nothing happens when the launcher sits idle. After a few minutes, dim it or show covers slowly. Midnight
is made for OLED screens, but a still launcher burns in all the same. ES-DE has dim, slideshow and video
screensavers.

<a id="l8"></a>**L8. The time and the DualSense's battery in a corner** (S)
As the PS5's home screen and Daijishou show them.

<a id="l9"></a>**L9. Play time you can look at** (S)
Each game's minutes are counted already (`port/app/catalog.h`). A page with totals by side, the most
played, and this week's, as Playnite and Steam have.

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

### Quality of life: in a game

<a id="g1"></a>**G1. Quick save and quick load hotkeys** (S) · **Done:** [`6f8dcd8`](https://github.com/premohq/PS5-ECAM/commit/6f8dcd8)
The guide's shortcuts are touchpad click + Options, L1 and R1; Square and Triangle look free. Touchpad click + Square
saves to the current slot and + Triangle loads it (held, as loading in the menu is), on the 3DS and DS.
RetroArch, DuckStation and DraStic all have this.

<a id="g2"></a>**G2. Pictures and times on the save state slots** (M)
The slots show a number and whether they are empty. A small picture of the game and when it was made, as
RetroArch, Delta and DuckStation show them. The frame can be read as the remote screen reads it.

<a id="g3"></a>**G3. Resume where you left off** (M) · **Done:** [`1d54168`](https://github.com/premohq/PS5-ECAM/commit/1d54168)
On **Quit to the library**, a 3DS or DS game makes an automatic save state, and its page offers
**Resume** beside **Play**. RetroArch's auto save state and Delta's resume work this way.

<a id="g4"></a>**G4. Undo the last load** (S) · **Done:** [`4f0a5af`](https://github.com/premohq/PS5-ECAM/commit/4f0a5af)
Keep the state from just before a load, so loading the wrong slot is not the end of an hour. RetroArch
and DuckStation have "Undo load state".

<a id="g5"></a>**G5. Touch hotspots** (M)
A DualSense button that touches one fixed point of the touch screen: a game's map, its item menu, its
"Y" button on the bottom screen. Citra and Azahar call it "touch from button".

<a id="g6"></a>**G6. The right stick as the stylus** (S to M)
On the DS the right stick can only be given to buttons (`port/melonds/input.cpp`). As a stylus it makes
Metroid Prime Hunters playable, and on the 3DS it does the same for Kid Icarus: Uprising. DraStic does
this.

<a id="g7"></a>**G7. Buttons for each game** (M)
A game can have its own picture, sound and speed (`GameSettingNames`), but not its own buttons, turbo or
A/B. Dolphin and Cemu on a PC both have per-game controller profiles.

<a id="g8"></a>**G8. Screenshots** (M)
A quick action or a hotkey that saves a PNG to `/data/ps5cemu/screenshots/<game>/`, a gallery on the
game's page, and one picked as the game's background.

<a id="g9"></a>**G9. Fast forward as a toggle** (S) · **Done:** [`8777160`](https://github.com/premohq/PS5-ECAM/commit/8777160)
Today it works only while held. An option to press once for on and again for off, as RetroArch offers.

### The Switch side

Since ProsperoEden's launcher is no longer opened from the app (`1579f25`), these went with it.

<a id="n1"></a>**N1. Buttons and controllers** (M)
The Switch side's **Controls** page has only Vibration (`port/frontend/shell/settings.cpp`, the
`!IsSwitch()` checks). Needed: the button map, A/B on Circle or Cross like the other sides, the
controller type (Pro Controller, Joy-Con pair, handheld), and players 2 to 4.

<a id="n2"></a>**N2. Mods and cheats** (M)
`/data/ps5cemu/eden/mods` is there, but nothing in the app lists or turns on a mod or a cheat now. A
game's menu could list them with a switch each, as the Add-ons tab of Eden on a PC does.

<a id="n3"></a>**N3. Updates and DLC, shown** (S to M)
The library pairs a game with the update and DLC beside it, but does not show it. Show which version
will run and which DLC is there, on the game's page.

<a id="n4"></a>**N4. Saves on the Switch side** (S)
The game menu leaves **Saves** out for Switch games (`OpenGameMenu`, `!IsSwitch()`). Back up and restore
one game's save, and take one from a JKSV or Checkpoint dump made on a real Switch.

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

<a id="d1"></a>**D1. The GBA slot** (M)
melonDS can put a GBA game and its save in the DS's slot 2: Pokémon's Pal Park, and the DS games that
read a GBA cartridge for extras. The guide lists the GBA slot as not there.

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

<a id="t1"></a>**T1. The 3DS camera from a picture** (M) · **Done:** [`0f30424`](https://github.com/premohq/PS5-ECAM/commit/0f30424)
Azahar's cameras are set to `blank` (`port/azahar/core.cpp:429`). Azahar can show a still image as the
camera instead: a picture from USB, or one sent from the phone page ([P1](#p1)), for the QR codes
of Pokémon Sun and Moon, Miis, and Animal Crossing's designs.

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

<a id="h3"></a>**H3. A light bar that says something** (S)
`ui/feedback.cpp` lights it in the side's colour. In a game it could show the player's number (Wii U,
players 1 to 4), flash on an achievement, or take the cover's colour.

### A phone or computer as a companion

The remote touch screen's HTTP server (`port/stream`) already reaches every phone on the network. It can
do more.

<a id="p1"></a>**P1. Add games, keys and saves from a browser** (M) · **Done:** [`86fff8a`](https://github.com/premohq/PS5-ECAM/commit/86fff8a), without the PIN or saves: the same network switch as the touch screen, and fixed folders
An upload page, behind a PIN shown on the TV, that puts each file where it goes: a game in its side's
folder, `prod.keys` in `eden/keys`, a CIA to install. No FTP client and no USB drive. Provenance on iOS
has a built-in web server for the same thing.

<a id="p2"></a>**P2. A phone as players 2 to 4** (M)
A page of touch buttons as a controller, for Wii U party games with more players than DualSenses, as
AirConsole does.

<a id="p3"></a>**P3. A phone's motion for the GamePad** (S to M)
The GamePad's gyroscope from the phone's own, for aiming in Splatoon from the phone's screen. Browsers
may only give motion over HTTPS, so check that first.

<a id="p4"></a>**P4. The library and the logs on the phone** (S to M)
A page listing your games, and the latest `boot.log` to attach to an issue from the phone. Goes with
[S2](#s2).

### Bigger and stranger

<a id="i1"></a>**I1. Turn on the ReShade code that is already in the tree** (M)
`port/cemu/ReShadeEffects.cpp` and `ReShadeRuntime.cpp`, about 2,200 lines, compile ReShade `.fx`
effects to Vulkan and run them on a picture, and `tools/reshade-check` tests them on a PC, but nothing in
the app calls them. A **Picture effects** page (CRT, sharpening, colour) for the Wii U first, then the
3DS and DS screens, with presets per game. RetroArch's shaders and ReShade on a PC are what people know.

<a id="i2"></a>**I2. Switch rooms over the internet** (L)
Eden emulates the Switch's local wireless through rooms. ProsperoEden's launcher had them (the guide
listed "rooms" until `184f41f`). Hosting and joining a room by a code, from the app, would let two PS5s,
or a PS5 and a PC, play local wireless games together.

<a id="i3"></a>**I3. DS local play between two PS5s** (L)
melonDS 1.0 has local multiplayer over a LAN. Two PS5s on one network as two DSes: Mario Kart DS,
Pokémon trades and battles.

<a id="i4"></a>**I4. The 3DS's 3D** (S to M) · **Done:** [`759f8e3`](https://github.com/premohq/PS5-ECAM/commit/759f8e3), red and cyan only (side by side for 3D TVs not yet)
Azahar can draw the 3DS's stereo picture as red and cyan anaglyph, or side by side for a 3D TV. Cheap
glasses, and the 3D slider back.

<a id="i5"></a>**I5. "Where you left off" cards** (M)
With [G2](#g2), [G3](#g3) and [G8](#g8): the launcher's home row shows the last picture of each game,
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
