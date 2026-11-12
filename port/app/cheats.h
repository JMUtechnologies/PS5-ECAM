// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: cheats, for every side whose emulator has them, in the file that emulator reads, each
// cheat off until the player turns it on (and kept so for the game):
//   NES, SNES, Game Boys, GBA, N64 and the  <side's data>/cheats/<game file>.cht   RetroArch's format, its
//   sides on their cores (libretro)                                                cheatN_enable the switch
//   DS (melonDS)                            melonds/cheats/<game file>.mch          CODE 1 on, CODE 0 off
//   3DS (Azahar)                            azahar/cheats/<title ID>.txt            Gateway codes, *citra_enabled on
//   Switch (Eden)                           <game files>/cheats/<title ID>/<build ID>.txt   chosen one by one in
//                                                                                    the game's Cheats (eden/extras.h)
//   GameCube and Wii (Porpoise X)           porpoise/cheats/<disc ID>.txt           Gecko codes; Porpoise turns its
//                                                                                    folder's on, so each is written
//                                                                                    off in the game's own file
// None for the Wii U (Cemu has no cheat engine: its community graphic packs are its mods), the Switch 2
// (Lichii's side reads none from the app yet), Virtual Boy, Pokémon mini and Game & Watch (their cores
// take none).
//
// Downloads (cheat_fetch.cpp), one game at a time on a thread of their own, never while a game runs:
//   libretro sides, DS   libretro-database's cht/<console>/<No-Intro name>.cht, by the game file's name
//                        (its folder listed once, kept for two weeks, the best name taken as box art's)
//   3DS                  FlagBrew's Sharkive, 3ds/<title ID>.txt
//   Switch               HamletDuFromage's switch-cheats-db, cheats/<title ID>.json (every build ID's)
//   GameCube, Wii        codes.rc24.xyz, the Gecko code database Dolphin's own Download Codes reads
// A game a source has nothing for is asked again after two weeks, or at once by Get cheats; a file the
// player made is never written over.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps5cheats
{
	// A game, as the launcher and the in-game menu know it: its side (ps5launcher::System), its title ID
	// (the 3DS's and the Switch's) and its file
	struct Game
	{
		int side = 0;
		uint64_t titleId = 0;
		std::string file;
	};

	// How a side keeps its cheats
	enum class Kind
	{
		None,
		Retro,	  // a libretro core's .cht
		Nds,	  // melonDS's .mch
		N3ds,	  // Azahar's Gateway .txt
		Switch,	  // Eden's per-build files, listed and chosen in the game's Cheats page (switch_extras.cpp)
		Porpoise, // Porpoise X's Gecko .txt, listed in the game's Cheats and patches page (porpoise_rows.cpp)
	};
	Kind KindOf(int side);
	inline bool Supported(int side) { return KindOf(side) != Kind::None; }

	struct Cheat
	{
		std::string name;
		std::string code; // a .cht's: the codes run together with '+'; empty for a heading
		bool on = false;
	};

	// The file the game's emulator reads its cheats from (a folder for the Switch); empty for none
	std::string FileFor(const Game& game);
	// The game's cheats as its emulator lists them, in that order (Retro, Nds, N3ds); headings left out
	std::vector<Cheat> List(const Game& game);
	// One of them (List's index) turned on or off, in its file
	bool SetOn(const Game& game, size_t index, bool on, std::string& error);
	// The same, of a file of that kind
	std::vector<Cheat> ListFile(Kind kind, const std::string& path);
	bool SetOnInFile(Kind kind, const std::string& path, size_t index, bool on, std::string& error);
	// How many cheats the game has (Switch: its files' and every build's; Porpoise: the downloaded file's)
	size_t Count(const Game& game);

	// RetroArch's .cht (cheats = N; cheatN_desc, cheatN_code, cheatN_enable), as text
	std::vector<Cheat> ParseCht(const std::string& text);
	std::string FormatCht(const std::vector<Cheat>& cheats);
	// A DS .cht as melonDS's .mch: a heading (a cheat with no code) a CAT, each code's words in pairs
	std::string ChtToMch(const std::string& text);
	// A Gateway file (Azahar's, Sharkive's): each [name] and its lines; on where *citra_enabled
	std::vector<Cheat> ParseGateway(const std::string& text);
	// codes.rc24.xyz's text (its ID, title, "Version" headings, names, codes, notes) as Porpoise's .txt,
	// each name with its version when the file has several; empty when the answer is not the game's
	std::string GeckoTxt(const std::string& text, const std::string& id);

	// -- downloads (cheat_fetch.cpp) ---------------------------------------------------------------
	// The games' cheats looked for, those already here or looked for lately left out unless again (Get
	// cheats: looked for now, a download merged with what is here, the player's choices kept)
	void Fetch(const std::vector<Game>& games, bool again = false);
	size_t Pending();
	// What became of the game's last look: 1 found, 0 none there, -1 not looked yet, -2 failed (no network)
	int Looked(const Game& game);
	// Cheats arrived since the start: the launcher shows the new state when it changes
	uint32_t Arrivals();
	// Before a game starts: nothing more is fetched, and one being fetched is waited for a few seconds
	void Stop();
}
