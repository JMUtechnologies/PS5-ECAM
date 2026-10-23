# PS5-ECAM guide

Everything the [README](../README.md) leaves out: the launcher, each system, where games and files go,
the controls and in-game menus, the remote touch screen, saves, mods and what to do when something goes
wrong.

- [The launcher](#the-launcher) · [Wii U](#wii-u-cemu) · [3DS](#nintendo-3ds-azahar) · [DS](#nintendo-ds-melonds)
- [Game files](#game-files) · [Controls](#controls) · [Remote touch screen](#remote-touch-screen-a-phone-as-the-second-screen)
- [Amiibo, save states, cheats and mods](#amiibo-save-states-cheats-and-mods) · [Online](#online) · [Where files are stored](#where-files-are-stored)
- [Reporting problems](#reporting-problems) · [Known issues](#known-issues)

## The launcher

| Button | Action |
|---|---|
| L1 / R1 | Library and Settings |
| Touchpad click | The next side: Wii U, 3DS, DS (in the Library and Settings) |
| Cross | Play the game, or open what's focused |
| Options | A game's options: its hub, its game settings, favourite |
| Square / Triangle | Sort and search (Library); a setting's help (Settings), and Square back to Default (game settings) |
| R2 | Jump by letter (Library) |
| Circle | Back |

The app opens on the Library of the side a game came back from, or else the side you used last,
with the game you played last in focus. You can switch sides without the app restarting. Each emulator's core only starts
with one of its games, so the emulators never run at the same time, and the launcher's background
work stops before a game starts.

- **Library:** your recently played games' box art from [GameTDB](https://www.gametdb.com/) in a row
  (its icon until the cover arrives), then every game as a list, with sorting and search
- **Game pages:** a game's description, developer, publisher, release date, genre, players and
  rating from GameTDB, and how it runs from the [compatibility list](COMPATIBILITY.md); a Wii U
  game's graphic packs
- **Settings:** opens over the Library, for the side you're on and nothing else: one short list of
  pages with icons (Language, Graphics, Audio, Controls, Remote touch screen, Game files, Accounts and
  saves, Online, Launcher, Diagnostics, About, and the side's own: Screens, System, USB devices or
  Artic Base), a line under each setting saying what it does, and Triangle for more.
- **Game settings:** Options on a game, then **Game settings**: the same pages for that game alone.
  Each value shows **Default** (its side's) until you change it, Square puts it back, and the in-game
  menu saves its changes to the game's settings when it has some.
- **Music and menu sounds:** an original setup theme, or your own: put MP3 or WAV files in
  `/data/ps5cemu/music` and choose one, or *Shuffle your music*, in **Settings > Audio > Launcher
  music**, which also sets its volume or turns it off
- **The Library:** your recently played games first, then every game on the side as a list, with
  Sort (Square), Search (Triangle) and L2 and R2 to jump by letter. **Settings > Launcher > Library
  filters** adds a row of filters over the list (All, Recently added, Favourites, Graphic packs on).
- **Themes** (**Settings > Launcher > Theme**) change how the launcher looks, never what it does:
  Light (the default), Link Jack (frosted glass over each side's colours), Classic (3.0)'s deep blue,
  Eden (after ProsperoEden's look) and Midnight (true black, for OLED screens). Your own: a JSON file in
  `/data/ps5cemu/themes`; the Theme row's help (Triangle) shows the format. **Settings > Launcher**
  also hides any side you don't use.
- **What's new:** on the first start and after an update, the app opens once on what the version
  brings.
- **Screensaver:** after five minutes with no button pressed, the launcher dims, so a still menu does
  not mark an OLED TV. Any button wakes it, and does nothing else.
- **Home screen tiles:** **Add to home screen** in a game's Options menu makes the game a tile of its
  own on the PS5's home screen (the open PS5 Forwarder Format, in `/data/homebrew`), with its box art.
  ShadowMount Plus shows it; starting it opens the game straight away and closes the app after it.
  Tiles need elfldr on port 9021, which the app also uses to put a tile in `/data/homebrew` when the
  sandbox can't. **Remove from home screen**, in the same menu, takes it away again.
- **Your language:** the menus follow the PS5's language; **Settings > Language** chooses another.

## Wii U (Cemu)

- Cemu's x64 recompiler, at the PS5's 3840x2160 output, with a choice of upscaling filter (Bicubic
  by default), 120 Hz on displays that support it, and frame pacing (**Settings > Graphics**, or the
  in-game menu's Graphics) that holds a game to an even 60 fps at 120 Hz or 30 fps at 60 Hz.
- Plays WUA, WUD/WUX and unpacked games from any folder the PS5 can read, unpacked games in `.zip`,
  `.7z`, `.rar` and tar archives too ([how](#games-in-archives)).
- The Cemu community graphic packs, organized by folder like Cemu's Graphic Packs window, with a
  dropdown for each preset. They are not in the app's download: the first time the Wii U side starts
  without them, it downloads GitHub's latest release in the background (about 3 MB), and
  **Settings > Online > Community graphic packs** brings them up to date.
- Player 1's DualSense is the GamePad and other signed-in players get Pro Controllers, up to four
  players. Each player can be a GamePad, Pro Controller, Classic Controller, or Wii Remote with or
  without a Nunchuk, with motion controls, rumble, stick deadzones and button mapping.
- The TV or the GamePad screen as the main picture, with the other one in a corner if you want; the
  touchpad works as the GamePad's touch screen.
- Updates and DLC: **Settings > Game files > Updates and DLC** looks on every USB drive, extended
  storage, the games folder and `/data`, lists what it finds by game with what is installed now, and
  installs one with Cross or every new one with Square (Triangle picks a folder by hand). A game's own
  are in its Options menu too. Unpacked updates in your game folder and WUAs are picked up as they are.
- **Accounts** (**Settings > Accounts and saves**): up to twelve Wii U accounts, each with its own
  saves, or one for each PS5 user, so everyone who signs in to the PS5 plays on their own saves.
- **AMD FSR 1** as the fifth *Upscaling to 4K* filter, in Settings, a game's settings and the in-game
  menu: a sharper picture for a game drawn below 4K.
- **Frame generation** (**Settings > Graphics**, a game's settings or the in-game menu; off by
  default): while a game takes two refreshes a frame or more,
  [MihawkGen](https://github.com/mihawk-99/PS5_MihawkGen), the frame generation Eden uses, makes a
  picture between each two frames, so a game at 30 fps is shown at 60 (at 60 on a 120 Hz TV). A
  faster game is shown as it is. It costs the GPU about 2 ms a picture at 4K and adds a little delay.
- Amiibo, from the in-game menu (see [Amiibo, save states, cheats and mods](#amiibo-save-states-cheats-and-mods)).
- Text entry with Cemu's on-screen keyboard.

## Nintendo 3DS (Azahar)

- Based on Mihawk's PS5 build of Azahar, with dynarmic's ARM recompiler. A New 3DS is emulated.
- 1x to 10x internal resolution (6x, 2400x1440, by default), texture filters (Anime4K, Bicubic,
  ScaleForce, xBRZ, MMPX), and shaders compiled in the background so games don't stutter.
- **Frame generation**, as on the Wii U side (**Settings > Graphics**, a game's settings or the
  in-game menu; off by default): a 3DS game at 30 fps is shown at 60.
- **3D for red and cyan glasses** (**Settings > Graphics** or a game's settings; off by default): the
  3DS's 3D, both eyes' pictures in one as Azahar's anaglyph mode makes them, for cheap red and cyan
  glasses, with the 3D slider all the way up.
- Screen layouts: a large top screen with the bottom one beside it, the top screen only, side by
  side, or stacked; either screen can take the main spot.
- **Borders:** Midnight, Waves, Aurora, Shell or PS5-ECAM artwork around the screens in every layout, from
  **Settings > Screens** or the in-game menu.
- **Screen rotation** (**Settings > Screens** or the in-game menu's Screens and border), as on the DS
  side: upright, 90° right, upside down or 90° left, for the games held on their side like a book,
  for the side or one game. The touch screen turns with the screens.
- **Touch without the bottom screen** (**Settings > Screens** or the in-game menu, off by default, on
  the DS side too): with the top screen alone on the TV, the touchpad still touches the bottom screen,
  its cursor over the top one, for games that ask for a touch now and then (Star Fox 64 3D). A game
  can have its own.
- DualSense: A on Circle and B on Cross like a real 3DS (or swapped), circle pad and C-stick on the
  sticks, ZL/ZR on L2/R2, motion from the gyro and accelerometer, and every button remappable in
  **Settings > Controls**. The touchpad is the bottom screen.
- Save states, cheats and amiibo in the in-game menu, plus custom textures and mods.
- System region, language and CPU clock in **Settings > System**, with the 3DS Home Menu.
- Games in `.zip`, `.7z`, `.rar` and tar archives, unpacked once, the first time they start
  ([how](#games-in-archives)).
- Install CIA files (games, updates and DLC) from **Settings > Game files > Install a CIA file**.
- **Artic Base:** play a game straight from your own 3DS over the network. Start Artic Base on the
  3DS, open **Settings > Artic Base** and enter the address the 3DS shows. The same page runs the Artic Setup Tool, which copies your 3DS's system files.
  The setup needs no keys from you: Azahar's own are built in, as in its official builds. An
  `aes_keys.txt` in `/data/ps5cemu/azahar/sysdata` takes the place of all of them, so leave it out
  unless it is complete; when it lacks what the setup needs, the 3DS's screen says so.
- An on-screen keyboard for games that ask for text.
- **The cameras see a picture:** put a PNG or JPEG in `/data/ps5cemu/azahar/camera`, or send one
  from a phone with the remote touch screen's **Add files to the PS5** (**3DS camera**), and both of
  the 3DS's cameras see the newest, fitted on white: a QR code for Pokémon's QR Scanner, a Mii or an
  Animal Crossing design to scan. It is read again each time the game turns a camera on. With no
  picture there, they see black.

## Nintendo DS (melonDS)

New in 4.0.0: a side of its own, in melonDS's green, to the right of the 3DS.

- melonDS 1.1's core, with its ARM recompiler in the executable memory the PS5 gives (its
  interpreter otherwise) and its software renderer on a thread of its own.
- The two screens at 4K, with a **Sharp**, **Smooth** or **Square pixels** filter, in the same layouts
  and borders as the 3DS side's, set apart from them (**Settings > Screens**).
- **Screen rotation** (**Settings > Screens** or the in-game menu's Screens and border): upright, 90°
  right, upside down or 90° left, for Hotel Dusk and the other games held on their side, for the side
  or one game. The touchpad follows the bottom screen as the TV shows it.
- **Touch without the bottom screen**, as on the 3DS side: the touchpad touches the bottom screen
  while only the top one is shown.
- DualSense: A on Circle and B on Cross like a real DS, the left stick as a second D-pad, every
  button remappable (**Settings > Controls**), the touchpad as the touch screen, and R3 held to blow
  into the microphone.
- Save states (five slots), melonDS's `.mch` cheats and the same in-game menu as the 3DS side's.
- Games in `.zip`, `.7z`, `.rar` and tar archives play as they are, as in melonDS's own frontend.
- No BIOS needed: melonDS's own replacements run most games. Your own DS's `bios7.bin`, `bios9.bin`
  and `firmware.bin` in `/data/ps5cemu/melonds/bios` are used when they are there
  (**Settings > System**).
- Not there: DSi games, the DS's wireless and its GBA slot.

## Nintendo Switch (Eden)

New in 5.0.0: a side of its own, in the Switch's red, to the right of the DS.

- [Eden](https://github.com/eden-emu/mirror), the Switch emulator, in Sword's PS5 port,
  [ProsperoEden](https://github.com/Swordpdf/ProsperoEden) (with this repository's `patches/eden`). It
  is a program of its own in the app's `eden/` folder: Play starts it with the game, and when the
  game ends it starts PS5-ECAM again, on the Switch side. A game's home screen tile closes the app
  instead, as on the other sides.
- **Settings** on the Switch side: the internal resolution (0.5x to 4x of the game's own 720p
  handheld or 1080p docked), upscaling to 4K (FSR 1 among them), ASTC textures (compressed, or full
  quality for a game whose textures look blocky), FSR 1's sharpening, **anti-aliasing** (FXAA or
  SMAA; off by default), **frame generation** (ProsperoEden's own, with MihawkGen: a game at 30 fps
  shown at 60; off by default), the performance overlay, the volume, the sticks' deadzone, and in
  **System** docked or handheld, the **controller type** (automatic, Pro Controller, handheld, a
  Joy-Con pair or a single Joy-Con) and the **speed limit** (100% to 300%, or none: a game held to 30
  fps can reach 60 where the PS5 has the time to spare). A game can have its own of each (**Game
  settings**), as on the other sides.
- **The loading screen and the in-game menu are PS5-ECAM's**: a Switch game starts with the app's
  loading screen from its first moment (its cover, its name, what is loading), and **touchpad click
  + Options** opens the same in-game menu as on the other sides: graphics (the upscaling filter, FSR
  sharpening, anti-aliasing, frame generation when the game started with it on, the performance
  overlay), the speed limit, the volume, controls (docked or handheld, the controller type, vibration,
  the stick deadzone), **amiibo** (dumps in `amiibo/` touched to the controller when the game asks,
  as on the Wii U side), the room and who is in it, and Quit to the library. Each changes at once,
  and what you change is kept for the next games. The game runs on behind the menu with its
  controller still, as a Wii U game does.
- **Multiplayer** (**Settings > Multiplayer** on the Switch side): ProsperoEden's rooms, in the app.
  Choose one of the public rooms (ProsperoEden's own featured rooms first) or a private one by its
  address, port and password, and a nickname; each Switch game then joins it as it starts, and you
  choose Local Wireless (or local play) in the game to play with the others in it. A room that
  cannot be joined brings you back to the library with why.
- **ProsperoEden's own screens never show**: its launcher is not opened from the app. A game that
  does not start, stops with an error or crashes comes back to PS5-ECAM's library, which says why,
  a home screen tile's game too.
- **Covers**: GameTDB's box art, or each game's own icon (**Settings > Online > Switch covers**).
- Your Switch's **keys** and **firmware** are needed, dumped from your own console: see
  [Game files](#nintendo-switch).
- Eden reaches `/data` as PS5-ECAM does: through etaHEN with `PPSA99360` in its app jailbreak list
  (or OnionHEN's `exact_title_ids`), else through its own helper and the console's ELF loader on port
  9021. Without it Eden reads no keys or games, and its launcher says so.
- Saves are Eden's, in `/data/ps5cemu/eden/user/nand/user/save`. **Back up to USB** takes them, with
  Eden's user profiles, as it does every side's saves.

## Game files

A side's games folder can be on the PS5, a USB drive, or a network share (a computer or NAS over SMB
or NFS: **Settings > Game files > Network shares**; not the Switch side). Each side looks through its
folder for new games when it opens, except a folder on a network share: with a large library that is
a long wait at every start, so the side shows the games it found there last time at once ("As found
last time" beside the Library's count). **Settings > Game files > Look for games now** looks again
after you add games; **Look on network shares at every start** brings back the look at every start.

### Wii U

Put games in `/data/ps5cemu/games`, or pick another folder in **Settings > Game files** on the Wii U side.

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

An unpacked game can also stay in a `.zip`, `.7z` or `.rar` archive, or a `.tar` (`.tar.gz`,
`.tar.xz` and the like), one game to an archive: its first folder by name with `code`, `content` and
`meta` in it is the game ([how it starts](#games-in-archives)). Updates and DLC are not read from
archives: unpack them, then install them from **Settings > Game files > Updates and DLC**. In a `.7z`
or `.rar`, give the game's folder a name without accents or symbols such as ™: the app may not read
those names there. A `.zip` has no such limit.

### Nintendo 3DS

Put games in `/data/ps5cemu/azahar/games`, or pick another folder in **Settings > Game files** on the 3DS side.

- `.3ds`, `.cci`, `.cxi` and `.app` dumps, `.3dsx` and `.elf` homebrew, and Azahar's compressed
  formats (`.z3ds`, `.zcci`, `.zcxi`, `.z3dsx`) play directly. The library reads a compressed game's
  name, icon and box art as it does any other's.
- `.cia` files, and Azahar's compressed `.zcia`, are installed first from **Settings > Game files > Install a CIA
  file**; updates and DLC too. Azahar only installs fully decrypted CIAs, the game inside included.
- Dumps must be decrypted too: Azahar plays no encrypted game, whatever keys it has. Decrypt them on
  your 3DS first (GodMode9).
- A game can also stay in a `.zip`, `.7z` or `.rar` archive, or a `.tar` (`.tar.gz`, `.tar.xz` and
  the like), one game to an archive: its first ROM by name is the game, CIA files aside (they are
  installed, not played) ([how it starts](#games-in-archives)).

### Nintendo DS

Put games (`.nds`) in `/data/ps5cemu/melonds/games`, or pick another folder in **Settings > Game
files** on the DS side. A save (`.sav`) beside a game is taken the first time it starts; from then on
saves are in `/data/ps5cemu/melonds/saves`.

A game can stay in its `.zip`, `.7z` or `.rar` archive, or a `.tar` (`.tar.gz`, `.tar.xz` and the
like): its first `.nds` by name is the game, read into memory when it starts, and nothing is
extracted. Its saves are named by the archive, as a game's are by its file.

### Nintendo Switch

Put games in `/data/ps5cemu/eden/roms`, or pick another folder in **Settings > Game files** on the
Switch side (not a network share: Eden cannot read those).

```text
/data/ps5cemu/eden/
├── keys/
│   ├── prod.keys                   your Switch's keys: Eden reads no game without them
│   └── title.keys
├── firmware/                       your Switch's firmware (its .nca files): many games need it
└── roms/
    ├── Game [0100000000010000].nsp # a game, alone
    └── Another Game/               # or a game in a folder of its own,
        ├── Another Game.xci        #   with its update and DLC beside it
        └── Another Game [Update].nsp
```

- `.nsp` and `.xci` play directly. An update or DLC in a game's folder goes with that game; ones in
  `/data/ps5cemu/eden/updates` are found too.
- With your keys the library reads each game's own name and icon from its file (without them, it
  names a game by its file). Its box art and description come from GameTDB, which knows a Switch
  game by its name: the app looks it up by the game's own name, else its file's. A picture beside a
  game with its name (`Game.png` or `Game.jpg`) is its box art instead.
- A title ID in brackets in the file's name, as dumps have it, keeps the game's settings and its
  place in Recently played whatever the file is called.
- A standalone ProsperoEden's files in `/data/prosperoeden` are used while `/data/ps5cemu/eden` has
  no keys.

### Games in archives

The library lists an archive's game by its own name and icon, read where it is, and keeps what it
read (`archives.bin`, on each side), so that looking for games again reads only the archives that
changed.

A DS game is read into memory as it starts. A Wii U or 3DS game is unpacked first, the first time it
starts, into `/data/ps5cemu/cache/archives`, with its progress on screen (Circle cancels); the copy
is kept for the next start and unpacked again only when the archive changes. Unpacking needs the
game's size, and 256 MB more, free where the data folder is: to make room, the copies used longest
ago are deleted by themselves. **Settings > Diagnostics > Unpacked games** shows how much the copies
take, and holding Cross deletes them. Saves stay: they go by the game's title ID, as they do for a
game outside an archive.

No games, keys, firmware or other copyrighted console data are included. Dump them from hardware and
software you own, and don't download or share them.

## Controls

| In a Wii U game | Action |
|---|---|
| Touchpad | GamePad touch screen cursor: click to tap, hold the click to drag |
| Touchpad click + Options | Open the in-game menu |
| Touchpad click + L1 | Switch the main screen between TV and GamePad |
| Touchpad click + R1 | Show or hide the second screen in the corner |

| In a 3DS or DS game | Action |
|---|---|
| Touchpad | Bottom screen cursor: click to tap, hold the click to drag |
| Touchpad click + Options | Open the in-game menu |
| Touchpad click + L1 | Swap the screens |
| Touchpad click + R1 | Next screen layout |
| Touchpad click + Square | Save state, to the slot chosen in the in-game menu (slot 1 at first) |
| Touchpad click + Triangle, held | Load state, from that slot |

**Settings > Controls > Buttons** on the 3DS and DS sides can also give a DualSense button one of
these, held (none has a button until you give it one):

- **Touch at the cursor:** touches the touch screen where the touchpad's cursor is, as a click of
  the touchpad does. Move your finger on the touchpad and hold the button to draw.
- **Fast forward:** the game runs as fast as it can while held.
- **Fast forward, on and off:** a press runs the game as fast as it can, and the next press puts it
  back to its speed.
- **Rewind** (DS): the game runs back, up to the last 15 seconds, while held, and goes on from
  there when you let go. The 3DS has no rewind.

The in-game menu opens on the left in the launcher's look: the game's cover and badges, four quick
actions along the top, then the settings, each category showing its current value. Up and Down
move (past the list's top onto the quick actions), Right or Cross opens a category, Left and Right
change a setting, and Circle closes a category or goes back to the game. Changes are kept for your
next games. A 3DS game pauses while its menu is open.

- **Quick actions:** Wii U: Resume, Screens (swap TV and GamePad), Graphic packs, Amiibo. 3DS:
  Resume, Save state, Load state, Screens (next layout).
- **Both:** screens, picture, volume, controls, the performance overlay, and **Quit to the library**
  (hold Cross until the ring fills; unsaved progress is lost).
- **Wii U:** **Amiibo**, and a **Graphics** page with Cemu's **Accurate barriers** (on by default;
  off can be faster, but some games flicker) and **Async shader compile** (on by default; off waits
  for each new shader: stutter, but nothing drawn wrong).
- **3DS:** **Border**, **CPU clock**, **Speed limit** (100% by default; higher or None to
  fast-forward, for the current game only), and **Save states, cheats, amiibo**.

## Remote touch screen: a phone as the second screen

Your phone, tablet or computer can be the game's touch screen: the Wii U GamePad's screen, the 3DS's
bottom screen or the DS's touch screen, in its browser, with touch. Nothing to install. (dolphin78's
idea and prototype, #38.)

1. Turn on **Settings > Remote touch screen** (on any side), or **Remote touch screen** in a game's
   menu (touchpad click + Options). It stays on for your next games.
2. Put the phone on the same network as the PS5 and scan the QR code shown there, or type the address
   under it (`http://<the PS5's address>:8080/`; the first free port from 8080).
3. Start a game: the page shows the second screen, even when the TV doesn't. Tap or drag on it to
   touch: player 1's GamePad on the Wii U, the bottom screen on the 3DS and the DS. The DualSense
   stays the buttons, and the sound stays on the PS5.

- **What it costs:** nothing while no browser shows it. While one does, the PS5 copies that screen up
  to 30 times a second (on the Wii U and the 3DS from the GPU), so a game can run a little slower.
- **Privacy:** any device on your network can open the page and touch, and send files to the
  folders below. The traffic is plain HTTP on your local network. Turn it off on a network you share.
- **If the phone can't reach it:** guest Wi-Fi often keeps devices apart (client isolation); use the
  main network. The page shows "Waiting for video" until a game is running. A phone that goes to
  sleep or leaves the page lets go of its touch within a second, and reconnects when you come back.

### Adding files from a phone or computer

The same page has **Add files to the PS5** (or go to `http://<the PS5's address>:8080/files`): choose
where they go, choose the files, and **Send**. No FTP client and no USB drive. Each file shows its
progress, and goes straight into the app's folder for it:

| Where to | Goes to |
|---|---|
| Wii U, 3DS, DS and Switch games | each side's games folder, as **Settings > Game files** has it (not a network share) |
| Switch keys, Switch firmware | Eden's `keys/` and `firmware/` |
| Amiibo | `/data/ps5cemu/amiibo` |
| 3DS camera | `/data/ps5cemu/azahar/camera`: the picture the 3DS's cameras see |
| Launcher music | `/data/ps5cemu/music` |

A file of the same name that is there already is never replaced, and a file cut short (the phone
slept, the page was left) is removed, so send it again. A new game shows in the library the next time
its side opens, or at once with **Settings > Game files > Look for games now**. Wii U games go as WUA,
WUD or WUX files, or an unpacked game in an archive; a 3DS CIA sent this way is installed from
**Install a CIA file** as usual.

## Amiibo, save states, cheats and mods

- **Amiibo (both).** Put your own amiibo dumps (`.bin`) in `/data/ps5cemu/amiibo`. When a game asks
  for one, open the in-game menu: on the Wii U, **Amiibo** (Left and Right choose, Cross scans); on
  the 3DS, **Save states, cheats, amiibo > Amiibo**, then **Take the amiibo away** when the game is
  done. 3DS games can only write to an amiibo with your console's `aes_keys.txt`. No amiibo files
  are included.
- **Skylanders, Disney Infinity and LEGO Dimensions (Wii U).** **Settings > USB devices** switches on
  Cemu's emulated Skylanders Portal of Power, Disney Infinity Base or LEGO Dimensions Toypad; it is
  plugged in when a game starts. Put your figure dumps in `/data/ps5cemu/figures/skylanders`,
  `infinity` or `dimensions`, then in the game open the in-game menu's **USB devices**: Left and Right
  put the next figure on a slot, and **Empty** takes it off. The game saves its progress into the dump,
  as it would on the toy. A real portal plugged into the PS5 isn't supported.
- **Save states (3DS).** Five slots per game; loading is held (hold Cross until the ring fills).
  Touchpad click + Square saves to the slot chosen in the menu, and touchpad click + Triangle, held a
  moment, loads it, without opening the menu (on the DS too); a PS5 notification says how it went.
  After a load, **Undo the last load** (held) puts the game back as it was just before it, for a
  wrong slot loaded by mistake.
- **Resume where you left off (3DS and DS).** **Quit to the library** saves a state of its own first
  (a moment longer), and the game's Options menu then has **Resume where you left off**, which starts
  the game and goes on from there. **Play** starts it as before. Not for Artic Base games.
  States made by earlier PS5-ECAM builds load too. Keep saving in the game as well.
- **Cheats (3DS).** Put a game's cheats in `/data/ps5cemu/azahar/cheats/<title ID>.txt` (16 hex
  digits, shown in the in-game menu), in the Gateway format desktop Azahar uses, and turn them on and
  off from the menu.
- **Custom textures (3DS).** Turn on **Settings > Graphics > Custom textures** (3DS) and put a pack in
  `/data/ps5cemu/azahar/load/textures/<title ID>/`.
- **Mods (Wii U).** Cemu's mods (graphic packs that replace a game's files, and the ones shared as
  `content` and `aoc` folders alone): put each in `ps5cemu/mods` on a USB drive, then **Options** on
  the game, **Add mods from USB**, and turn it on in the game's **Graphic packs**. The
  [mods guide](MODS.md) has the folders, title IDs and what to do when a mod does not work.
- **Mods (3DS).** LayeredFS mods go in `/data/ps5cemu/azahar/load/mods/<title ID>/`, with `romfs/` and
  `exefs/` inside.

## Online

- **Box art:** the first time a game shows up, the app downloads its cover from GameTDB
  (`art.gametdb.com`) by the ID on the game's box (a Switch game's is found by its name), in the
  background, into `/data/ps5cemu/covers/boxart`. A failed download is tried again a few seconds
  later; a cover GameTDB doesn't have is asked for again after two weeks. **Settings > Online** turns
  the downloads off, and has *Download missing box art* and *Download all box art again*.
- **Updates:** when the app starts fresh, it asks GitHub for the latest release. When a newer
  PS5-ECAM is out, it asks whether to install it: **Update now** downloads the release, checks it
  against the release's SHA-256, puts its files in place of the old ones and starts the app again.
  Your games, saves and settings in `/data/ps5cemu` stay as they are. Circle stops a download, and
  one that stalls for 30 seconds stops by itself. If the PS5 doesn't start the app again, the app says
  so: close it and start it from the home screen. **Settings > Online > PS5-ECAM updates** checks
  again. **Skip this version** stops the start from asking about that release (Settings > Online
  still finds it).
- **Graphic packs (Wii U):** the Cemu community graphic packs come from GitHub
  (`cemu-project/cemu_graphic_packs`), not with the app: the first time the Wii U side starts without
  any, they are downloaded in the background into `graphicPacks/downloadedGraphicPacks` (packs you
  copied there yourself count, and are not replaced). **Settings > Online > Community graphic packs**
  fetches a newer release.
- **Going back:** an update keeps the version it replaces in `/data/ps5cemu/previous-version`, until
  the next update. **Settings > Online > Go back to** (held) puts it back and starts it, and the newer
  release is not offered at start again.

Without an internet connection, the libraries just show the game icons.

## Where files are stored

Everything the app writes goes to `/data/ps5cemu`, except your game files (or to the app's drive:
[below](#everything-on-the-apps-drive)):

```text
/data/ps5cemu/
├── ps5cemu.json                    launcher settings
├── settings.xml                    Cemu settings
├── controllerProfiles/             Cemu controller profiles
├── mlc01/                          Wii U storage: installed updates and DLC, saves
├── games/                          default Wii U game folder
├── keys.txt                        disc keys for encrypted Wii U dumps
├── graphicPacks/                   community graphic packs and your own
├── cache/                          Cemu shader and pipeline caches, and archives/: the Wii U and
│                                   3DS games unpacked from archives
├── archives.bin                    what the library read of Wii U games in archives
├── amiibo/                         your amiibo dumps (.bin), for both emulators
├── figures/                        Skylanders, Disney Infinity and LEGO Dimensions dumps (Wii U)
├── azahar/
│   ├── games/                      default 3DS game folder
│   ├── sdmc/                       3DS SD card: installed CIAs, saves, extra data
│   ├── nand/                       3DS system storage
│   ├── sysdata/                    aes_keys.txt, if you add it
│   ├── cheats/                     3DS cheats, <title ID>.txt
│   ├── load/                       3DS custom textures (textures/) and mods (mods/)
│   ├── shaders/                    Azahar shader cache
│   ├── archives.bin                what the library read of 3DS games in archives
│   └── log/azahar_log.txt          Azahar log
├── melonds/
│   ├── games/                      default DS game folder
│   ├── saves/                      DS saves, <game file>.sav
│   ├── states/                     DS save states
│   ├── cheats/                     DS cheats, <game file>.mch
│   ├── archives.bin                what the library read of DS games in archives
│   └── bios/                       your DS's bios7.bin, bios9.bin and firmware.bin, if you add them
├── eden/                           the Switch side's, always in /data (Eden's program keeps it there)
│   ├── keys/                       your Switch's prod.keys and title.keys
│   ├── firmware/                   your Switch's firmware
│   ├── roms/                       default Switch game folder
│   ├── updates/, mods/             updates and DLC, mods and cheats
│   ├── user/                       Eden's storage: saves in user/nand/user/save
│   ├── covers/                     games' icons and names, once Eden has read them
│   ├── config/prosperoeden.json    ProsperoEden's own settings
│   └── logs/                       Eden's logs (Copy logs to USB takes them)
├── covers/                         game icons and box art (boxart/)
├── music/                          your own launcher music (.mp3, .wav)
├── themes/                         your own launcher themes (.json)
├── log.txt                         Cemu log
└── logs/                           app logs: boot.log, and boot.prev.log to boot.4.log for the
                                    four sessions before, each with its Cemu log (cemu.prev.txt...)
```

**FTP and file managers:** every file and folder the app makes is open to every user (folders 0777,
files 0666), so an FTP client or a file manager can change or delete it, installed CIAs included.
Files an older version made are opened the same way as the app starts. Some may belong to another
user, usually from a session the HEN or elfldr's helper made root. Those are listed under `[files]`
in `boot.log`, and the next start asks elfldr's helper for root to open them. Without elfldr,
setting `/data/ps5cemu` to 777 from a file manager opens them.

### Everything on the app's drive

With the app installed on a USB drive or the extended storage (ShadowMountPlus mounting it from
there), a `PS5CEMU-HAR-data` folder at the top of that drive takes the place of `/data/ps5cemu`, with
the same files in it, and the app makes nothing in `/data`. A first start on a drive makes that folder
itself when there is no `/data/ps5cemu` yet.

To move an install that already keeps its data in `/data/ps5cemu`: with the app closed, copy
everything in `/data/ps5cemu` into a new `PS5CEMU-HAR-data` folder at the top of the app's drive (FTP
or a file manager), start the app, check your games and saves are there, then delete
`/data/ps5cemu`. Game folders inside `/data/ps5cemu` follow the data to the drive. The Setup check's
**Storage** shows the folder in use, as `boot.log`'s `[main] data folder` line does. An app installed
in `/data`, or mounted from an image, keeps `/data/ps5cemu`. The drive's `ps5cemu/mods` folder is
still where Wii U mods are added from.

**Backups:** **Settings > Accounts and saves > Back up to USB** copies every side's saves, save
states, the Wii U's accounts and the Switch's user profiles (never the games) to
`PS5CEMU-HAR-saves/<date and time>` on the first USB drive plugged in, with the settings:
`ps5cemu.json` (every side's and each game's own), Cemu's `settings.xml`, the controller profiles and
your themes. **Restore from USB** puts a backup back, moving what is there now to
`/data/ps5cemu/saves-before` first; when the backup has settings, PS5-ECAM then starts again to use
them. A game's own backup (its **Saves** page) holds its saves alone.

**Wii U saves from Cemu on PC:** with the game closed, copy
`mlc01/usr/save/00050000/<title ID>/user/<account>` from your PC to
`/data/ps5cemu/mlc01/usr/save/00050000/<title ID>/user/80000001`.

**3DS saves from Azahar or Citra on PC:** copy
`sdmc/Nintendo 3DS/<ID0>/<ID1>/title/00040000/<title ID>/data` to the same path under
`/data/ps5cemu/azahar/sdmc/Nintendo 3DS/`. ID0 and ID1 are all zeros in Azahar, on PC and PS5.

## Reporting problems

Check the [compatibility list](COMPATIBILITY.md) first, then
[open an issue with the bug report form](https://github.com/premohq/PS5-ECAM/issues/new/choose).
It asks for your firmware, HEN, app version and logs, which almost every problem needs.

**Settings > Diagnostics** has what a report needs:

- The app version, the firmware, and what the HEN gave the app.
- **Copy logs to USB:** puts the logs and settings in a dated `PS5-ECAM-logs-...` folder on a USB
  drive. The app keeps the last five sessions' logs, so copy them soon after a problem and attach
  them to your report.
- **Clear shader caches** (hold Cross): for a game that crashes on a bad or shared cache.
  Games build them again as they run.

When a session ends in a crash, the next start says so once, with the crash report's first line,
so its log can be copied before it is pushed out by the next few starts.

The boot log also gets a `[memory]` line once a minute, a `[perf]` (Wii U) or `[perf3ds]` (3DS) line
every 10 seconds with the frame rate, and `[crash]` lines if an emulator crashes.

## Known issues

- Going back to the library restarts the app, and a Wii U game keeps running behind the in-game
  menu.
- Batman: Arkham Origins needs its 60 FPS graphic pack turned off; with it on, it glitches and
  crashes once gameplay loads ([#15](https://github.com/premohq/PS5-ECAM/issues/15)).
- The 3DS's microphone, local wireless and online play aren't supported, and its cameras see a
  picture, not the room ([3DS camera](#nintendo-3ds-azahar)).
- 3DS texture filters are expensive at high internal resolutions; if a game stutters, try Texture
  filter: None first.
- The first start with a large Wii U shader cache takes a few minutes while Cemu builds the
  pipelines.
- etaHEN is the most tested HEN; OnionHEN and others have had fewer reports so far.
