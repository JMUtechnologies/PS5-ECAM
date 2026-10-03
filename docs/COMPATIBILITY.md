# Compatibility

How games run in PS5CEMU-HAR, from reports on real consoles. It only lists what has been reported:
a game that isn't here hasn't been reported yet, which doesn't mean it doesn't work. To add a game
or correct one, [open an issue](https://github.com/premohq/PS5CEMU-HAR/issues/new/choose) with the
game's region and version, the app version, and how far you played.

| Status | Meaning |
|---|---|
| **Playable** | Can be played through with no known problem that gets in the way |
| **Issues** | Runs, with problems: drawing errors, slowdowns, or a crash in one place |
| **Crashes** | Starts, but crashes or freezes early, before it can really be played |
| **Won't start** | Doesn't get as far as the game |

On the Wii U side, games run much slower without JIT memory (Cemu's interpreter instead of its
recompiler), so the status assumes the app got JIT memory (see the README's
[Install](../README.md#install) step 3).

## Wii U (Cemu)

| Game | Region / version | Status | Notes | Reports | App version |
|---|---|---|---|---|---|
| The Legend of Zelda: Breath of the Wild | v1.5.0 (v208) | Issues | Runs at about 60 fps at 1440p. Freezes and closes when Magnesis or other runes, or the Sheikah scope (R3), highlight objects; the LWZX Crash Workaround pack doesn't help | [#10](https://github.com/premohq/PS5CEMU-HAR/issues/10) | 1.0.0 to 2.0.0 B |
| Fast Racing Neo | | Issues | About half the textures are missing during races | [#13](https://github.com/premohq/PS5CEMU-HAR/issues/13) | |
| Batman: Arkham Origins | | Crashes | Crashes as soon as gameplay loads, right after the first cutscene | [#15](https://github.com/premohq/PS5CEMU-HAR/issues/15) | |

## Nintendo 3DS (Azahar)

No 3DS game has been reported yet. A CIA that is only partly decrypted (the CIA decrypted, the game
inside it still encrypted) is refused by Azahar on any console; that is about the dump, not the
game ([#14](https://github.com/premohq/PS5CEMU-HAR/issues/14)).
