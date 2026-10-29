# Existing libraries and emulator files

For GameCube, Wii, N64, NES, GB, GBC and GBA, **Settings > Game files > Automatically find games** defaults to On. The configured folder is searched first, then known existing folders on internal storage, USB drives and mounted extended storage. Disable it per system to scan only your chosen folder. The original four systems retain their existing scanners and content installers.

Supported locations include each emulator's native `games`/`roms` directories, standalone homebrew title directories, and system folders beneath `games` or `roms`. N64 also reads Mupen64Plus's remembered directory; GameCube/Wii read Porpoise's saved custom folders. Loose games directly at drive roots or shared `games`/`roms` roots are recognized when their format identifies the system. Shared archives are not guessed. Raw GameCube/Wii ISO headers distinguish those systems.

No discovery operation creates, moves, deletes or rewrites user data. Automatic traversal skips saves, keys, firmware and system directories, does not follow symlinks, and uses folder, entry and depth limits. Duplicate paths are suppressed. A drive connection change refreshes the open retro library; **Look for games** also refreshes it manually. A missing drive drops its entries on the next scan and leaves saved folder preferences intact.

All eleven systems offer **Existing emulator files** on the Game files page. This inventories known `updates`, `dlc`, `firmware`, `bios` and `cheats` folders without reading their contents. It distinguishes the active emulator data folder from alternate locations. A `+` after a count means the inventory limit was reached. This is discovery, not automatic installation or cheat activation: Wii U/3DS content and firmware that require installation still use their native installers. Native key/save/settings paths are preserved. The original NES FDS `disksys.rom` location is also recognized.

## Console validation

Host regression and sanitizer tests cover combined internal/external libraries, standalone paths, duplicate suppression, unplugged drives, scan requests during scanning, protected directories, format routing and read-only asset inventory. They do not establish console compatibility.

On the PS5, confirm all eleven tabs, choose each system's Game files page, inspect the detected libraries and existing emulator files, then launch a known game. Verify video, audio, controller input, safe exit/return and save reload for each system. Test an external drive connected before startup and disconnected/reconnected while the library is open. Do not unplug a drive while its game is running or saving. Record each actual result separately; a menu or successful launch request alone is not a gameplay pass.
