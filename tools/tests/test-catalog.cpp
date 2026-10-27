// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the library's catalogue (port/app/catalog.cpp) on a PC, run by test-catalog.sh in three
// processes, as the catalogue is read once a process: "write" fills every side and saves it, "read" finds
// all of it again, a game in an archive and its play time included, and what a new scan keeps; "broken"
// reads a file that is not JSON as an empty catalogue. The data folder is PS5CEMU_DATA, a folder of the
// test's.
#include "app/catalog.h"

#include <cstdio>
#include <cstring>
#include <string_view>

using namespace ps5catalog;

static int failures = 0;
static void Check(bool ok, const char* what)
{
	std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

static ps5emu::Game Make(uint64_t titleId, const char* name, const char* path)
{
	ps5emu::Game game;
	game.titleId = titleId;
	game.name = name;
	game.path = path;
	return game;
}

constexpr uint64_t kZelda = 0x0005000010143500, kPokemon = 0x0004000000055d00, kMario = 0x00048000000a5d00, kKirby = 0x01007ef00011e000;
constexpr int64_t kStarted = 1760000000;

static void Write()
{
	ps5emu::Game zelda = Make(kZelda, "Zelda BOTW", "/data/ps5cemu/games/Zelda.zip");
	zelda.archiveFolder = true;
	zelda.archiveMember = "Zelda BOTW/";
	zelda.version = 208;
	zelda.hasUpdate = true;
	zelda.dlcCount = 2;
	zelda.format = "WUA";
	zelda.gameId = "ALZE01";
	Replace(System::WiiU, {zelda});
	ps5emu::Game pokemon = Make(kPokemon, "Pok\xc3\xa9mon X", "/mnt/usb0/3ds/\"quoted\" games.7z");
	pokemon.archiveIndex = 3;
	pokemon.archiveMember = "Pokemon X.3ds";
	pokemon.archiveSize = 5000000000ull; // over 4 GB
	pokemon.publisher = "Nintendo";
	Replace(System::N3ds, {pokemon});
	ps5emu::Game mario = Make(kMario, "New Super Mario Bros.", "/data/ps5cemu/melonds/roms/nsmb.nds");
	mario.nds = true;
	Replace(System::Nds, {mario});
	ps5emu::Game kirby = Make(kKirby, "Kirby", "/data/ps5cemu/eden/roms/kirby.nsp");
	kirby.hasUpdate = true;
	kirby.versionName = "1.1.0";
	kirby.dlcCount = 1;
	Replace(System::Switch, {kirby});

	SetFavourite(System::N3ds, kPokemon, true);
	SetHidden(System::Nds, kMario, true);
	const uint32_t ambient[2] = {0x11223344, 0xaabbccdd};
	SetAmbient(System::WiiU, kZelda, ambient);
	Check(Ended(System::N3ds, kPokemon, kStarted) == 0, "play time: nothing for a game never started");
	Played(System::N3ds, kPokemon, kStarted);
	Check(Ended(System::N3ds, kPokemon, kStarted + 29) == 0, "play time: under half a minute counts none");
	Check(Ended(System::N3ds, kPokemon, kStarted + 90) == 2, "play time: a minute and a half counts two");
	Check(Ended(System::N3ds, kPokemon, kStarted + 3600) == 60, "play time: an hour counts 60");
	Check(Ended(System::N3ds, kPokemon, kStarted - 5) == 0, "play time: a clock that went back counts none");
	Check(Ended(System::N3ds, kPokemon, kStarted + 86401) == 0, "play time: over a day counts none");
	Check(Ended(System::WiiU, kPokemon, kStarted + 600) == 0, "play time: another side's game is not this one");
	Check(Save(), "saved");
}

static void Read()
{
	const Entry* zelda = Find(System::WiiU, kZelda);
	Check(zelda && zelda->game.name == "Zelda BOTW" && zelda->game.version == 208 && zelda->game.hasUpdate && zelda->game.dlcCount == 2 &&
			zelda->game.format == "WUA" && zelda->game.gameId == "ALZE01",
		"a Wii U game's facts");
	Check(zelda && zelda->game.archiveFolder && zelda->game.archiveMember == "Zelda BOTW/" && zelda->game.archiveIndex == -1 &&
			zelda->game.Archived(),
		"a Wii U title's folder in an archive");
	Check(zelda && zelda->ambient[0] == 0x11223344 && zelda->ambient[1] == 0xaabbccdd, "its ambient colours");
	const Entry* pokemon = Find(System::N3ds, kPokemon);
	Check(pokemon && pokemon->game.name == "Pok\xc3\xa9mon X" && pokemon->game.path == "/mnt/usb0/3ds/\"quoted\" games.7z" &&
			pokemon->game.publisher == "Nintendo",
		"a 3DS game: a name and a path that need escaping");
	Check(pokemon && pokemon->game.archiveIndex == 3 && pokemon->game.archiveMember == "Pokemon X.3ds" && pokemon->game.archiveSize == 5000000000ull &&
			!pokemon->game.archiveFolder,
		"a ROM in an archive: its index, name and size over 4 GB");
	Check(pokemon && pokemon->favourite && pokemon->lastPlayed == kStarted && pokemon->minutesPlayed == 62, "favourite, last played, 62 minutes");
	const Entry* mario = Find(System::Nds, kMario);
	Check(mario && mario->game.nds && !mario->game.Archived(), "a DS game");
	Check(mario && mario->hidden && pokemon && !pokemon->hidden, "a hidden game stays hidden, the others not");
	const Entry* kirby = Find(System::Switch, kKirby);
	Check(kirby && Games(System::Switch).size() == 1 && kirby->game.hasUpdate && kirby->game.versionName == "1.1.0" && kirby->game.dlcCount == 1,
		"a Switch game, its update's version as the game shows it and its DLC");
	Check(zelda && zelda->game.versionName.empty(), "no version name for a game that has none");
	Check(!Find(System::Nds, kPokemon), "each side its own");

	// a new scan: what is known of each game kept by title ID, a game first found now dated now
	ps5emu::Game moved = Make(kPokemon, "Pokemon X", "/mnt/usb0/3ds/Pokemon X.3ds");
	ps5emu::Game added = Make(0x0004000000030800, "Animal Crossing", "/mnt/usb0/3ds/ac.3ds");
	const int64_t before = std::time(nullptr);
	Replace(System::N3ds, {moved, added});
	const Entry* kept = Find(System::N3ds, kPokemon);
	Check(kept && kept->favourite && kept->minutesPlayed == 62 && kept->lastPlayed == kStarted && kept->game.path == moved.path &&
			!kept->game.Archived(),
		"a new scan keeps what is known, with the game as found");
	const Entry* fresh = Find(System::N3ds, added.titleId);
	Check(fresh && fresh->added >= before && fresh->minutesPlayed == 0, "a game the scan found first is dated");
	Check(Games(System::N3ds).size() == 2 && Games(System::N3ds)[0].game.name == "Animal Crossing", "sorted by name");
}

int main(int argc, char** argv)
{
	const std::string_view mode = argc > 1 ? argv[1] : "";
	if (mode == "write")
		Write();
	else if (mode == "read")
		Read();
	else if (mode == "broken")
	{
		Check(Games(System::WiiU).empty() && Games(System::N3ds).empty(), "a file that is not JSON: an empty catalogue");
		Check(Ended(System::N3ds, kPokemon, kStarted) == 0, "no game to count");
	}
	else
		return 2;
	return failures ? 1 : 0;
}
