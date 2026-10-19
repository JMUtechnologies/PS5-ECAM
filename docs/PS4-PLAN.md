# A PS4 and PS4 Pro edition: the plan

A plan, written 19 October 2026, answering [#27](https://github.com/premohq/PS5CEMU-HAR/issues/27).
What is built so far, and how, is in [ps4/README.md](../ps4/README.md): a proof of concept, with no
promise of support. Nobody working on it has a PS4 to test with.

## The idea

The PS5 app's launcher (its library, settings, in-game menus and look) over emulator cores that
already run on a jailbroken PS4, each one credited, as a separate download with fewer features than
the PS5 build. Most of the hard part, getting each emulator to run on the PS4 at all, has been done
by others; what this project adds is one app for all of them, in its own interface.

## What runs on a PS4 already

Checked on 19 October 2026.

| System | Project | Built on | By | Source | Licence |
|---|---|---|---|---|---|
| Wii U | [Crema](https://github.com/iHaiDeeZ/Crema) | Cemu | iHaiDeeZ (ShiroKlein) | public | MPL-2.0, as Cemu |
| 3DS and DS | bramble ([announcement](https://www.youtube.com/watch?v=9hUpqI8Sf_A), [release post](https://www.patreon.com/blaqberry/posts/bramble-3ds-and-171739076)) | not said | Aki (blaqberry) | none found | not said |
| GameCube and Wii | [DolphinPS4](https://github.com/iHaiDeeZ/DolphinPS4) | Dolphin | iHaiDeeZ | public | GPL-2.0 |

- **Crema** installs as a `.pkg` with GoldHEN's package installer and keeps its files in
  `/data/Crema`. Cemu's recompiler turns the Wii U's PowerPC code into x86-64 for the PS4's CPU, and
  it draws through Vulkan with Mesa's RADV, adapted to submit to the PS4's GPU: the same way the PS5
  app runs Cemu. Its README credits OpenOrbis and PacBrew as the PS4 toolchains, and its launcher and
  in-game menu come from DolphinPS4. It was made on a launch PS4; the PS4 Pro is untested (its README
  suggests Boost Mode). Its own figures: Breath of the Wild at 20 to 24 fps with tuned settings, Mario
  Kart 8 at 45 to 52 in races, New Super Mario Bros. U and Super Mario 3D World at about 30, the CPU
  being what holds them back. Not there: both screens at once, save states, Extended Memory graphic
  packs.
- **bramble** is announced as 3DS and DS emulation for the PS4, with save states, screen layouts and
  the touchpad as the touch screen; `Bramble-PS4-v2.84.pkg` came out on its author's Patreon about a
  week before this was written. No source and no word of what it is built on were found. If it is built on Azahar
  (GPL-2.0 or later) and melonDS (GPL-3.0), its source can be asked of its author under those
  licences: ask before planning around it.
- The same author as Crema also has PS4SX2 (PCSX2, built on PS5SX2: a PS5 port brought back to the
  PS4), Resonance (Flycast) and M33 (PPSSPP). No other public PS4 port of Azahar or melonDS was found;
  a DS emulator shown running on a PS4 on GBAtemp, about 200 days ago, has not been released.

## How it would fit together

As on the PS5: one emulator per session, the launcher choosing, the app starting over between sides.

- **The launcher.** `port/frontend` and `port/ui` draw with Vulkan, on RADV; Crema shows RADV runs on
  the PS4's GPU. The PS5's system calls the launcher leans on (VideoOut, AudioOut, the pad, the
  system service) have PS4 counterparts of the same names, so `port/ps5` gets a `port/ps4` beside it:
  the screen, the DualShock 4 (its touchpad as the touch screen, its motion sensors), sound, paths,
  and getting out of the sandbox with GoldHEN.
- **Wii U.** Crema's Cemu and RADV behind the launcher, in place of this repo's: its tree already
  builds for the PS4, with its patches for the console. The PS5 patches worth keeping (patches/cemu)
  are rebased on it one by one.
- **DS and 3DS.** bramble's cores if its source is available; otherwise this repo's own frontends
  (`port/melonds`, `port/azahar`), built for the PS4. They are written for a FreeBSD console with
  RADV and executable memory, which is what the PS4 is too; melonDS first, being the lightest.
- **Toolchain and package.** OpenOrbis (or PacBrew's PS4 SDK) in place of the PS5 payload SDK; a
  `.pkg` for GoldHEN's package installer, with a title ID of its own; data in a folder of its own on
  `/data`. Starting from Crema's tree saves finding out what it already found: its toolchain, its
  RADV and its packaging.
- **JIT memory.** Every core here recompiles. Crema has already solved where its code goes on the
  PS4: the others take the same.

## What the PS4 can't do that the PS5 does

The PS4 has eight Jaguar cores at 1.6 GHz (2.13 GHz on the Pro), around 5 GB of memory for an app (a
little more on the Pro) and a GPU with a fraction of the PS5's power. So:

- **Wii U:** held back by the CPU, as Crema's figures show; one screen at a time to begin with.
- **3DS:** Azahar leans on one fast core, which the Jaguar is not. 2D and lighter games are likely
  fine, many 3D ones below full speed; resolution at 1x or 2x by default.
- **DS:** melonDS's software renderer at the DS's own resolution is light: the side most likely to
  run well.
- **Picture:** 1080p, no 4K or 120 Hz; on the Pro, perhaps more.
- **Left out of a first version:** the remote touch screen, network shares, RetroAchievements, most
  themes, Wii U mods from USB and anything else that is not needed to pick a game and play it. Kept:
  the library, settings, controls, save states (3DS and DS) and the in-game menus.

## Testing it without a PS4

- **The launcher** on a PC, as now (`tools/preview-shell.sh`): every screen can be checked without a
  console.
- **A build check** like `make check`: the whole app built and packaged with the PS4 toolchain on
  every change, so a beta at least starts.
- **Testers.** #27's reporter offered to wait for it; a beta `.pkg` with **Copy logs to USB** and a
  short checklist (starts, shows the library, starts a game on each side, comes back to the library).
- **shadPS4** runs some PS4 homebrew on a PC, but not RADV's direct submission to the GPU or a JIT:
  at most a way to look at the launcher. Not tried.

## Licences and credits

- This repo is GPL-3.0-or-later, Cemu and Crema MPL-2.0 (combined with GPL code as the PS5 app
  already does), Azahar GPL-2.0-or-later, melonDS GPL-3.0. DolphinPS4 is listed as GPL-2.0;
  Dolphin itself is GPL-2.0-or-later, which a GPL-3.0 app can take. If DolphinPS4's own changes are
  version 2 only, a GameCube side stays a separate app.
- Each core's authors in the README and the About page, as the PS5 app credits Cemu's, Azahar's and
  melonDS's, with the porters' own credits carried over (Crema's: the Cemu team, Mesa, OpenOrbis,
  PacBrew, love-ps4, the graphic packs, Pretendo Network, GameTDB).
- Before any of it: ask Crema's author and bramble's. They have done the work, may rather help than
  be forked, and bramble's source is not public.

## Steps

1. Ask iHaiDeeZ (Crema) and Aki (bramble); have bramble's source, or know there is none. (Its
   source is not public: the DS and 3DS sides are built from melonDS's and Azahar's.)
2. Crema's tree building here, and its `.pkg` installing (by a tester).
3. The launcher on the PS4: `port/frontend` and `port/ui` on Crema's RADV, behind a `port/ps4`
   platform layer, checked in the preview first.
4. The Wii U side: Crema's Cemu started from the launcher, and back to the library.
5. The DS side: melonDS, bramble's or this repo's.
6. The 3DS side: Azahar, bramble's or this repo's, with cautious defaults.
7. A beta for testers, then a release beside the PS5 app's, under a name of its own.

## Still to decide

- A repository of its own, or a branch of this one. A repository of its own, sharing `port/frontend`
  and `port/ui` with this one (a subtree, say), keeps the PS5 build as simple as it is.
- Its name and title ID.
- What a first version has, past picking a game and playing it.
