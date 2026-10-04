<p align="center">
  <img src="docs/banner.svg" alt="PS5CEMU-HAR: Cemu and Azahar, the Wii U and Nintendo 3DS emulators, on PlayStation 5 homebrew" width="100%">
</p>

<p align="center">
  <strong>Wii U and Nintendo 3DS emulation in one PlayStation 5 homebrew app</strong><br>
  Latest release: <a href="https://github.com/premohq/PS5CEMU-HAR/raw/releases/PS5CEMU-HAR-v2.0.0d.zip"><strong>2.0.0 D</strong></a> ·
  <a href="#whats-new-in-200-d">What's new</a><br>
  <a href="#install">Install</a> · <a href="#wii-u-cemu">Wii U</a> · <a href="#nintendo-3ds-azahar">3DS</a> ·
  <a href="#controls">Controls</a> · <a href="docs/COMPATIBILITY.md">Compatibility</a> ·
  <a href="docs/BUILDING.md">Building</a> · <a href="#credits">Credits</a>
</p>

**PS5CEMU-HAR** is a homebrew app for jailbroken PS5 consoles. It bundles two emulators, both
rendering with Vulkan through Mihawk's PS5 port of the RADV driver and played with the DualSense:

- [Cemu](https://github.com/cemu-project/Cemu) for Wii U games
- [Azahar](https://github.com/azahar-emu/azahar) for Nintendo 3DS games

The app opens on a start screen where you pick PS5 CEMU (Wii U) or PS5 AZAHAR (3DS). Each has its
own launcher, game library, settings and in-game menu.

This is an unofficial project, not affiliated with or endorsed by the Cemu or Azahar teams, Nintendo
or Sony. All credit for the emulators goes to their developers.

> [!NOTE]
> **Status:** most Wii U games tried so far are playable, with working video, controls, sound and
> saves. 3DS support is newer and has had less testing on real hardware. If something goes wrong,
> please [report it with your logs](#reporting-problems).

## Install

1. Download [PS5CEMU-HAR-v2.0.0d.zip](https://github.com/premohq/PS5CEMU-HAR/raw/releases/PS5CEMU-HAR-v2.0.0d.zip)
   ([all releases](https://github.com/premohq/PS5CEMU-HAR/releases)) and extract it, or
   [build it yourself](docs/BUILDING.md).
2. Copy the `PPSA99360` folder to `/data/homebrew/PPSA99360` on your PS5.
3. Load your HEN and let it jailbreak `PPSA99360`:
   - **etaHEN:** add `PPSA99360` to its app jailbreak list.
   - **OnionHEN:** add `PPSA99360` to the end of `exact_title_ids` in `config.ini`, with **no comma
     after it** (a trailing comma makes OnionHEN ignore the whole list), then reload OnionHEN.
   - **Any other HEN:** have an ELF loader (elfldr) listening on port 9021. The app then opens
     `/data` itself with its bundled helper.

   No HEN has to grant JIT memory: the recompilers make their own when it isn't granted. The
   [HEN setup guide](docs/HEN-SETUP.md) has the details, and **Settings > Diagnostics** shows what
   the app got.
4. Add your games (see [Game files](#game-files)).
5. Start PS5CEMU-HAR from the home screen, pick an emulator and open the Library.

**Updating:** copy the new `PPSA99360` folder over the old one; everything in `/data/ps5cemu` is
kept. When a newer release is out, the app says so in a notification as it starts. The PS5 keeps the
app's name, icon and background from when it was first registered; register it again in your loader
to see new ones.

## Start screen and launchers

| Button | Action |
|---|---|
| Left / Right, Cross | Choose PS5 CEMU or PS5 AZAHAR |
| Circle (on a launcher's home screen) | Go back to the start screen |

You can switch sides without the app restarting. Each emulator's core only starts with one of its
games, so Cemu and Azahar never run at the same time, and the launcher's background work stops
before a game starts. After a game, the app opens on the side you played on.

Both launchers have:

- **Home:** your last game, recent games, and a **Coming soon** row of planned features
- **Library:** all your games with their icons, plus box art from [GameTDB](https://www.gametdb.com/)
  when it's available
- **Settings:** video, audio, controls (remap any button by pressing it), game folder, installs,
  online options and diagnostics
- **About:** credits and where the app stores its files
- **Music and menu sounds:** two original pieces in the spirit of a Nintendo console's shop and
  setup screens; **Settings > Audio** picks the piece and its volume, or turns it off

## Wii U (Cemu)

- Cemu's x64 recompiler, at the PS5's 3840x2160 output, with a choice of upscaling filter (Bicubic
  by default) and 120 Hz on displays that support it.
- Plays WUA, WUD/WUX and unpacked games from any folder the PS5 can read.
- The Cemu community graphic packs, organized by folder like Cemu's Graphic Packs window, with a
  dropdown for each preset.
- Player 1's DualSense is the GamePad and other signed-in players get Pro Controllers, up to four
  players. Each player can be a GamePad, Pro Controller, Classic Controller, or Wii Remote with or
  without a Nunchuk, with motion controls, rumble, stick deadzones and button mapping.
- The TV or the GamePad screen as the main picture, with the other one in a corner if you want; the
  touchpad works as the GamePad's touch screen.
- Updates and DLC: installed from **Settings > Install updates and DLC**, or picked up automatically
  from your game folder or a WUA.
- Amiibo, from the in-game menu (see [Amiibo, save states, cheats and mods](#amiibo-save-states-cheats-and-mods)).
- Text entry with Cemu's on-screen keyboard.

## Nintendo 3DS (Azahar)

- Based on Mihawk's PS5 build of Azahar, with dynarmic's ARM recompiler. A New 3DS is emulated.
- 1x to 10x internal resolution (6x, 2400x1440, by default), texture filters (Anime4K, Bicubic,
  ScaleForce, xBRZ, MMPX), and shaders compiled in the background so games don't stutter.
- Screen layouts: a large top screen with the bottom one beside it, the top screen only, side by
  side, or stacked; either screen can take the main spot.
- **Borders:** Midnight, Waves, Aurora or Shell artwork around the screens in every layout, from
  **Settings > Borders** or the in-game menu.
- DualSense: A on Circle and B on Cross like a real 3DS (or swapped), circle pad and C-stick on the
  sticks, ZL/ZR on L2/R2, motion from the gyro and accelerometer, and every button remappable in
  **Settings > Controls**. The touchpad is the bottom screen.
- Save states, cheats and amiibo in the in-game menu, plus custom textures and mods.
- System region and language in **Settings > System**.
- Install CIA files (games, updates and DLC) from **Settings > Install CIA files**.
- **Artic Base:** play a game straight from your own 3DS over the network. Start Artic Base on the
  3DS, choose **Artic Base** on the 3DS home screen (or press Triangle) and enter the address the
  3DS shows. The same page runs the Artic Setup Tool, which copies your 3DS's system files.
- An on-screen keyboard for games that ask for text.

## Game files

### Wii U

Put games in `/data/ps5cemu/games`, or pick another folder in **Settings > Game files**.

```text
<game folder>/
├── Game.wua                        # Wii U archive: game, update and DLC in one file
├── Game.wux                        # or Game.wud
└── Game/                           # unpacked game
    ├── code/
    ├── content/
    └── meta/
```

Encrypted WUD and WUX dumps also need their disc keys in `/data/ps5cemu/keys.txt`.

### Nintendo 3DS

Put games in `/data/ps5cemu/azahar/games`, or pick another folder on the 3DS side.

- `.3ds`, `.cci`, `.cxi` and `.app` dumps, `.3dsx` and `.elf` homebrew, and Azahar's compressed
  formats (`.z3ds`, `.zcci`, `.zcxi`, `.z3dsx`) play directly.
- `.cia` files are installed first from **Settings > Install CIA files**; updates and DLC too. Azahar
  only installs fully decrypted CIAs, the game inside included.
- Encrypted dumps need `aes_keys.txt` from your own console in `/data/ps5cemu/azahar/sysdata`.

No games, keys, firmware or other copyrighted console data are included. Dump them from hardware and
software you own, and don't download or share them.

## Controls

| In a Wii U game | Action |
|---|---|
| Touchpad | GamePad touch screen cursor: click to tap, hold the click to drag |
| Touchpad click + Options | Open the in-game menu |
| Touchpad click + L1 | Switch the main screen between TV and GamePad |
| Touchpad click + R1 | Show or hide the second screen in the corner |

| In a 3DS game | Action |
|---|---|
| Touchpad | Bottom screen cursor: click to tap, hold the click to drag |
| Touchpad click + Options | Open the in-game menu |
| Touchpad click + L1 | Swap the screens |
| Touchpad click + R1 | Next screen layout |

In the in-game menu, the D-pad moves, Cross selects, Left and Right change a setting, and Circle goes
back to the game. Changes are kept for your next games.

- **Both:** screens, picture, volume, controls, the performance overlay, and **Back to the library**
  (press Cross twice; unsaved progress is lost).
- **Wii U:** **Amiibo**, and a **Graphics** page with Cemu's **Accurate barriers** (on by default;
  off can be faster, but some games flicker) and **Async shader compile** (on by default; off waits
  for each new shader: stutter, but nothing drawn wrong).
- **3DS:** **Border**, **CPU clock**, **Speed limit** (100% by default; higher or None to
  fast-forward, for the current game only), and **Save states, cheats, amiibo**.

## Amiibo, save states, cheats and mods

- **Amiibo (both).** Put your own amiibo dumps (`.bin`) in `/data/ps5cemu/amiibo`. When a game asks
  for one, open the in-game menu: on the Wii U, **Amiibo** (Left and Right choose, Cross scans); on
  the 3DS, **Save states, cheats, amiibo > Amiibo**, then **Take the amiibo away** when the game is
  done. 3DS games can only write to an amiibo with your console's `aes_keys.txt`. No amiibo files
  are included.
- **Save states (3DS).** Five slots per game; loading asks twice. Save states are tied to the app
  version that made them, so keep saving in the game too.
- **Cheats (3DS).** Put a game's cheats in `/data/ps5cemu/azahar/cheats/<title ID>.txt` (16 hex
  digits, shown in the in-game menu), in the Gateway format desktop Azahar uses, and turn them on and
  off from the menu.
- **Custom textures (3DS).** Turn on **Settings > Video > Custom textures** and put a pack in
  `/data/ps5cemu/azahar/load/textures/<title ID>/`.
- **Mods (3DS).** LayeredFS mods go in `/data/ps5cemu/azahar/load/mods/<title ID>/`, with `romfs/` and
  `exefs/` inside.

## Online

- **Box art:** the first time a game shows up, the app downloads its cover from GameTDB
  (`art.gametdb.com`) by the ID on the game's box, in the background, into
  `/data/ps5cemu/covers/boxart`. A cover GameTDB doesn't have leaves a `.none` file there; delete it
  to try again. **Settings > Online and updates** turns the downloads off.
- **Update notice:** when the app starts fresh, it asks GitHub once for the latest release and shows
  a notification if a newer PS5CEMU-HAR is out. Nothing is downloaded or installed.

Without an internet connection, the libraries just show the game icons.

## Where files are stored

Everything the app writes goes to `/data/ps5cemu`, except your game files:

```text
/data/ps5cemu/
├── ps5cemu.json                    launcher settings
├── settings.xml                    Cemu settings
├── controllerProfiles/             Cemu controller profiles
├── mlc01/                          Wii U storage: installed updates and DLC, saves
├── games/                          default Wii U game folder
├── keys.txt                        disc keys for encrypted Wii U dumps
├── graphicPacks/                   community graphic packs and your own
├── cache/                          Cemu shader and pipeline caches
├── amiibo/                         your amiibo dumps (.bin), for both emulators
├── azahar/
│   ├── games/                      default 3DS game folder
│   ├── sdmc/                       3DS SD card: installed CIAs, saves, extra data
│   ├── nand/                       3DS system storage
│   ├── sysdata/                    aes_keys.txt, if you add it
│   ├── cheats/                     3DS cheats, <title ID>.txt
│   ├── load/                       3DS custom textures (textures/) and mods (mods/)
│   ├── shaders/                    Azahar shader cache
│   └── log/azahar_log.txt          Azahar log
├── covers/                         game icons and box art (boxart/)
├── log.txt                         Cemu log
└── logs/                           app logs: boot.log, and boot.prev.log to boot.4.log for the
                                    four sessions before, each with its Cemu log (cemu.prev.txt...)
```

**Wii U saves from Cemu on PC:** with the game closed, copy
`mlc01/usr/save/00050000/<title ID>/user/<account>` from your PC to
`/data/ps5cemu/mlc01/usr/save/00050000/<title ID>/user/80000001`.

**3DS saves from Azahar or Citra on PC:** copy
`sdmc/Nintendo 3DS/<ID0>/<ID1>/title/00040000/<title ID>/data` to the same path under
`/data/ps5cemu/azahar/sdmc/Nintendo 3DS/`. ID0 and ID1 are all zeros in Azahar, on PC and PS5.

## Reporting problems

Check the [compatibility list](docs/COMPATIBILITY.md) first, then
[open an issue with the bug report form](https://github.com/premohq/PS5CEMU-HAR/issues/new/choose).
It asks for your firmware, HEN, app version and logs, which almost every problem needs.

**Settings > Diagnostics** has what a report needs:

- The app version, the firmware, and what the HEN gave the app.
- **Copy logs to USB:** puts the logs and settings in a dated `PS5CEMU-HAR-logs-...` folder on a USB
  drive. The app keeps the last five sessions' logs, so copy them soon after a problem and attach
  them to your report.
- **Clear shader caches** (press Cross twice): for a game that crashes on a bad or shared cache.
  Games build them again as they run.

The boot log also gets a `[memory]` line once a minute, a `[perf]` (Wii U) or `[perf3ds]` (3DS) line
every 10 seconds with the frame rate, and `[crash]` lines if an emulator crashes.

## What's new in 2.0.0 D

- **Breath of the Wild's runes fixed.** Magnesis, the other runes and the Sheikah scope no longer
  freeze the game: Cemu is now past upstream's fix for shaders with loops
  ([#10](https://github.com/premohq/PS5CEMU-HAR/issues/10)).
- **Batman: Arkham Origins plays**, with its 60 FPS graphic pack turned off
  ([#15](https://github.com/premohq/PS5CEMU-HAR/issues/15)).
- **3DS borders:** Midnight, Waves, Aurora and Shell, around the screens in every layout.
- **3DS save states, cheats and amiibo**, and **Wii U amiibo**, in the in-game menus.
- **More settings:** Async shader compile (Wii U), Custom textures and Language (3DS).
- **No restart when switching** from the Wii U side to the 3DS side.
- **Update notice** when a newer release is out.
- **Any HEN:** the [HEN setup guide](docs/HEN-SETUP.md) for etaHEN, OnionHEN and others
  ([#12](https://github.com/premohq/PS5CEMU-HAR/issues/12)).
- **Logs from the last five sessions** are kept.
- The version shows on the start screen and in both launchers.

<details>
<summary><strong>Older releases</strong></summary>

- **2.0.0 C:** a new launcher look with music and menu sounds; Cemu's recompiler without etaHEN's
  JIT; USB and extended drives opened with the bundled helper; one emulator at a time; a 3DS
  on-screen keyboard, region setting and speed limit; Wii U Graphics settings; Diagnostics with logs
  to USB and shader cache clearing; a bug report form and compatibility list.
- **2.0.0 B:** 3DS sound at the right rate, a 3DS memory leak fixed, Artic Base, clearer CIA
  installs, a 3DS CPU clock setting, shared Wii U shader caches no longer crashing, and Accurate
  barriers in the Wii U menu.
- **2.0.0:** Azahar for 3DS games, a start screen to choose Wii U or 3DS, game icons and box art,
  per-player controls in the in-game menu, and memory leak fixes.
- **1.0.0:** dark blue launcher, graphic pack presets, controller settings, and update and DLC
  installs.
- **0.2.0:** games run at the right speed, GamePad screen options, touchpad cursor, in-game menu and
  on-screen keyboard.
- **0.1.0:** first version that ran on a PS5.

</details>

## Known issues

- Going back to the library restarts the app, and the game keeps running behind the in-game menu.
- Batman: Arkham Origins needs its 60 FPS graphic pack turned off; with it on, it glitches and
  crashes once gameplay loads ([#15](https://github.com/premohq/PS5CEMU-HAR/issues/15)).
- 3DS camera, microphone, local wireless and online play aren't supported.
- A CIA install can't be canceled once it starts.
- 3DS texture filters are expensive at high internal resolutions; if a game stutters, try Texture
  filter: None first.
- The first start with a large Wii U shader cache takes a few minutes while Cemu builds the
  pipelines.
- etaHEN is the most tested HEN; OnionHEN and others have had fewer reports so far.

## Building

Run `make release` on Linux (Ubuntu 24.04; WSL works too). It downloads every dependency at its
pinned version, builds Azahar, Cemu and the launcher, and writes the app to `build/app/PPSA99360` and
a ZIP to `dist/`. See [docs/BUILDING.md](docs/BUILDING.md), or run `make help` for the other targets.

- `port/`: the PS5 platform layer, both emulators' PS5 frontends, the launcher and the in-game menus
- `patches/`: changes to Cemu and Azahar
- `tools/`: build, packaging and artwork scripts
- `extras/ps5mfr`: Alex Free's
  [PS5 Make FSELF Recursive](https://github.com/alex-free/ps5-make-fself-recursive), with its own
  readme and license

## Credits

- [Cemu](https://github.com/cemu-project/Cemu) by the Cemu team and contributors (MPL-2.0)
- [Azahar](https://github.com/azahar-emu/azahar) by the Azahar team and the Citra contributors
  before them (GPL-2.0-or-later), with dynarmic by MerryMage and contributors
- **Mihawk** for RADV on PS5 ([PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa),
  [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), the payload SDK fork and its platform layer)
  and the PS5 ports of Azahar and dynarmic ([PS5_Azahar](https://github.com/mihawk-99/PS5_Azahar)),
  with contributions from **mpereiraesaa**
- **BlackBearReloaded** for [ProsperoEden](https://github.com/blackbearreloaded/ProsperoEden), which
  the launchers' layout, artwork and software renderer are based on, the
  [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) for
  the app runtime, packaging and sandbox elevation, and the
  [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) designs the home screen
  follows
- The [Lexend](https://github.com/googlefonts/lexend) authors for the launchers' font (SIL Open Font
  License 1.1)
- **Dimok** for the Wii U Homebrew Launcher, whose background is used on the Wii U side
- The authors of the **3DS Homebrew Launcher**, which inspired the 3DS side's background
- **John Törnblom** for the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk), and
  **pacbrew** for their PS5 libraries
- **Swordpdf** for the etaHEN jailbreak request from PS5SX2
- [GameTDB](https://www.gametdb.com/) and its contributors for the box art
- The authors of the [Cemu community graphic packs](https://github.com/cemu-project/cemu_graphic_packs)

## License

The app's own code is licensed under GPL-3.0-or-later (see [LICENSE](LICENSE)). Files derived from
Cemu keep Cemu's MPL-2.0 license, as noted in their headers. Azahar is GPL-2.0-or-later, and the app
that includes it is distributed under GPL-3.0-or-later. Other third-party components keep their own
licenses.

## Disclaimer

- **No affiliation.** This is an independent homebrew project. It is not affiliated with, endorsed
  by, or sponsored by Sony Interactive Entertainment, Nintendo, the Cemu project or the Azahar
  project. "PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment Inc.; "Wii U" and
  "Nintendo 3DS" are trademarks of Nintendo.
- **No proprietary material.** No Sony or Nintendo SDK, firmware, encryption keys, games or
  decrypted system modules are included.
- **No warranty.** This project is provided "as is", without warranty of any kind, to the extent
  permitted by law. See sections 15 and 16 of the GPL.
- **Use at your own risk.** Running homebrew requires a modified console, which may void its
  warranty, breach the platform's terms of service, or cause data loss.
- **Legal use only.** Only use it with hardware, accounts and content you own. This project does not
  support or enable piracy.
