// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the Switch side's games, which Eden plays in a program of its own (eden/eboot.bin in
// the app's folder: ProsperoEdenX, premohq's fork of Sword's ProsperoEden, built by
// tools/build-eden.sh), for the launcher.
//
// Eden's data is where its program keeps it, /data/ps5cemu/eden: keys/ (prod.keys, title.keys),
// firmware/, roms/ (the games: .nsp and .xci files, alone or in a folder per game with its update
// and DLC), updates/, mods/, covers/ and config/prosperoeden.json. Its game files folder may be
// another (prosperoeden.json's game_files, or a standalone ProsperoEden's /data/prosperoeden while
// ours has no keys): GameFilesFolder finds it as Eden does.
//
// The launcher cannot read a Switch game itself (that takes Eden and the keys): a game is named by
// its file, and its title ID is the one in its name ("Game [0100ABCD12345000].nsp"), else one made
// from its path (kIdTag). Once Eden has read it (its own library lists it, or it started it), its
// cover and own name are in covers/, keyed by the file's path in roms/, and the launcher shows them.

#pragma once

#include "../app/emulator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ps5eden
{
	// Eden's data folder (keys/, firmware/, roms/, covers/, config/), as its program has it
	std::string DataFolder();
	// The Switch side's default games folder: Eden's roms/
	std::string DefaultGamesFolder();
	// The game files folder Eden uses (keys/, firmware/, roms/): its saved choice, else ours, else a
	// standalone ProsperoEden's when only that has keys
	std::string GameFilesFolder();
	// The games folder a scan of folder looks in: Eden's roms/ for the default one
	std::string ScanFolder(const std::string& folder);
	bool KeysFound();	  // prod.keys in Eden's keys/
	bool FirmwareFound(); // anything in Eden's firmware/
	// Whether the app has the Switch side's program (eden/eboot.bin in its folder)
	bool Available();

	// The files a Switch game can be: .nsp and .xci
	bool IsSwitchFile(const std::string& name);
	// The title IDs made for games whose names have none: their top byte, which no Switch title has
	constexpr uint64_t kIdTag = 0x5Aull << 56;
	inline bool IsMadeId(uint64_t titleId) { return (titleId >> 56) == (kIdTag >> 56); }

	// Looks for games in a folder (the files in it, and those in its folders: a folder per game), on a
	// thread of its own
	void StartScan(const std::string& folder);
	bool Scanning();
	// What the last scan found, sorted by name
	std::vector<ps5emu::Game> ListGames();
	// The games in a folder, as the scan finds them: -1 when it cannot be read
	int CountGames(const std::string& folder);

	// A game's cover as Eden keeps it (a TGA in its covers/), once it has read the game; else empty
	std::string CoverPath(const ps5emu::Game& game);
	// A picture of the game's own beside it (<its name>.png or .jpg, or cover.png or .jpg in its
	// folder), the box art the launcher shows; else empty
	std::string BoxArt(const ps5emu::Game& game);
}
