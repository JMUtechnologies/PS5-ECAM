# PS4 proof of concept

A try at the PS5 app's idea on a jailbroken PS4 or PS4 Pro: Wii U, 3DS and DS games from one
launcher. It is a proof of concept with no promise of support, and nobody working on it has a PS4
to test with. The plan is [docs/PS4-PLAN.md](../docs/PS4-PLAN.md).

## Where it is

| Step | State |
|---|---|
| 1. The toolchain: OpenOrbis with clang 21, Crema's libc++ 21 | built |
| 2. Crema (Wii U) built as it is: Mesa's RADV, Cemu's libraries, Cemu | RADV built; the libraries and Cemu next |
| 3. This repo's launcher on the PS4 (`port/frontend`, `port/ui` behind a `port/ps4`) | not started |
| 4. Crema's Cemu started from the launcher | not started |
| 5. melonDS (DS), this repo's `port/melonds` | not started |
| 6. Azahar (3DS), this repo's `port/azahar` | not started |
| 7. A `.pkg` for GoldHEN's package installer | not started |

bramble's source is not public, so its package is not used: the DS and 3DS sides are built from
melonDS's and Azahar's source, as the PS5 app's are.

## Building

On Ubuntu 24.04, as root (it installs clang 21 and writes to `/opt/pacbrew`):

```sh
ps4/build.sh              # every stage, or one at a time:
ps4/build.sh toolchain    # clang 21; OpenOrbis in /opt/pacbrew/ps4/openorbis
ps4/build.sh sources      # Crema, DolphinPS4's Mesa patch, Mesa 26.0.8, Cemu at Crema's base
ps4/build.sh libcxx       # libc++ 21 for the PS4
ps4/build.sh mesa         # RADV for the PS4
ps4/build.sh deps         # Cemu's libraries
ps4/build.sh cemu         # Crema's Cemu
```

Sources and builds go in `~/ps4` (`PS4_WORK` to change it). Each source is pinned in `build.sh`.

- **OpenOrbis**: PacBrew's package server was down, so `assemble-openorbis.sh` lays out
  `/opt/pacbrew/ps4/openorbis` as PacBrew's packages would, from the OpenOrbis v0.5.2 release, the
  headers and PS4 musl PacBrew pins and orbis-lib-gen's stub libraries, with clang 21 where
  PacBrew has its clang 12.
- **Mesa**: Crema's `mesa-ps4-vertex-fetch-switch.patch` does not apply to Mesa 26.0.8 with
  DolphinPS4's `mesa-ps4.patch`; `patches/mesa-vertex-fetch.patch` is the same change made
  against them.
- **Cemu's libraries**: GitHub's source archives can be refused through a proxy, so the ones Crema
  downloads that way are fetched with git first.

## Credits

- **Crema** and **DolphinPS4** by iHaiDeeZ (ShiroKlein): the Wii U side, the PS4's RADV, the
  toolchain setup; Crema's own credits carry over (the Cemu team, Mesa, OpenOrbis, PacBrew,
  love-ps4, the graphic packs, Pretendo Network, GameTDB).
- **Cemu** (MPL-2.0), **Mesa** (MIT), **melonDS** (GPL-3.0), **Azahar** (GPL-2.0 or later).
- **OpenOrbis** and **PacBrew** for the PS4 toolchain.
- **Aki (blaqberry)**, whose bramble showed 3DS and DS games running on the PS4.
