# Mods for Wii U games

PS5-ECAM runs Cemu's mods. A Cemu mod is a **graphic pack**: a folder with a `rules.txt` that
says which game it is for, and the files it brings:

- `content/`: files that take the place of the game's own (models, textures, text, levels);
- `aoc/`: the same for the game's DLC (the DLC must be installed);
- `code/`: files that take the place of the game's program files;
- patches (`patch_*.asm`) and shaders, as Cemu's community graphic packs have.

Most mods made for Cemu on a PC work as they are. Mods for the Wii U itself (SDCafiine, Aroma
plugins) don't: look for the Cemu version of a mod.

## Adding mods from a USB drive

1. On a computer, unzip each mod (a `.zip`, `.7z` or `.rar` from a mod site) into its own folder.
2. Copy those folders into a folder named `ps5cemu/mods` on a USB drive (exFAT or FAT32), so it looks
   like one of these:

   ```
   USB drive
   └── ps5cemu
       └── mods
           ├── Second Wind                ← a mod with a rules.txt (a graphic pack)
           │   ├── rules.txt
           │   └── content/...
           └── 00050000101C9400           ← a mod without a rules.txt, in a folder named
               └── Linkle Mod                by its game's title ID
                   └── content/...
   ```

3. Plug the drive into the PS5 and start PS5-ECAM on the Wii U side.
4. Either:
   - press **Options** on the game and choose **Add mods from USB**: a mod without a `rules.txt`
     and outside a title ID folder is given that game; or
   - open **Settings > Game files > Mods from USB**: every mod on every drive is copied, and a mod
     without a `rules.txt` needs its title ID folder.
5. Turn the mod on: **Options** on the game, then **Graphic packs**, and press **Cross** on the mod
   (its folder there is *Mods*). Mods start **off**. A mod can also be turned on from the in-game menu
   (Touchpad + Options, **Graphic packs**); one that replaces files applies from the game's next start.

The copy can take a few minutes for a large mod: the launcher says how far it is. Copying a mod
again replaces the copy made before, so updating a mod is the same as adding it.

### Finding a game's title ID

**Options** on the game, then **Show where it is**: the title ID is the 16-digit number there (it
can be written with or without the dash, `00050000-101C9400`). Breath of the Wild's are
`00050000101C9400` (US), `00050000101C9500` (Europe) and `00050000101C9300` (Japan).

## Where mods are kept

Mods added from a drive are copied to `/data/ps5cemu/graphicPacks/mods/`. Cemu finds a pack anywhere
in `/data/ps5cemu/graphicPacks`, so mods copied there with FTP or a file manager work too, as long as
each has its `rules.txt` (only mods added from a drive get one written for them).

To remove a mod, turn it off, or delete its folder from `/data/ps5cemu/graphicPacks/mods/` with FTP
or a file manager.

## Writing a rules.txt

A mod shared without a `rules.txt` gets this one when it is added from a drive. Write the same by
hand to add such a mod with FTP:

```ini
[Definition]
titleIds = 00050000101C9400
name = Linkle Mod
path = "The Legend of Zelda: Breath of the Wild/Mods/Linkle Mod"
description = Replaces Link with Linkle.
version = 6
```

`titleIds` can list several (`00050000101C9300,00050000101C9400,00050000101C9500` for every region).
`path` is where it shows in the Graphic packs list. Cemu's own documentation of graphic packs
describes the rest (presets, patches, shaders).

## When a mod does not work

- **It is not on the game's Graphic packs page:** its `rules.txt` names another title ID (another
  region of the game), or is missing. `/data/ps5cemu/log.txt` names the packs Cemu could not read.
- **The game does not change:** the folders inside the mod must be spelt `content`, `aoc` and `code`
  in lower case (mods added from a drive are fixed for you), and directly in the mod's folder.
- **The game crashes:** the mod may be for another version of the game (update it in **Updates and
  DLC**, or use the mod's version for yours), need the DLC, or clash with another mod or with a
  community pack that changes the same files: turn the others off and try again.
- **Large mods** (whole overhauls of several GB) need the room on the PS5's storage: the copy says so
  when it runs out.

## 3DS mods

3DS games take LayeredFS mods, as Azahar on a PC does: put a mod's `romfs/` and `exefs/` folders in
`/data/ps5cemu/azahar/load/mods/<title ID>/`. Texture packs go in
`/data/ps5cemu/azahar/load/textures/<title ID>/`, with **Settings > Graphics > Custom textures** on.
With the data on the app's drive, these folders are in its `PS5CEMU-HAR-data` folder instead.

- **Names as Azahar writes them.** The PS5 tells capitals from small letters: `load`, `mods`,
  `romfs` and `exefs` in small letters, the title ID in 16 digits with capital letters (it starts
  `00040000`). A folder named otherwise is not read, and the game starts without the mod.
- **A mod for your game's region and version.** A mod's `exefs/` (`code.ips`, `code.bps`,
  `code.bin`, `exheader.bin`) patches the game's code, which differs between regions and versions:
  one made for another stops the game as it starts, and the app comes back to the library saying the
  game stopped. Use the mod's build for your copy (and its update, installed from a CIA, when the mod
  needs it). Without its `exefs/` folder, a mod's `romfs/` changes still apply.
