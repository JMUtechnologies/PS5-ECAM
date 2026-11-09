// SPDX-License-Identifier: GPL-3.0-or-later
// Porpoise-X's saves, a game at a time (its src/ui_memcard.cpp, without its pictures): the GameCube's two
// cards, each a folder of .gci files or a whole-card image, and the Wii's folders. A USB drive's are in
// Porpoise's own "Porpoise Saves" folder, so what Porpoise exported comes in here and the other way round.
#pragma once

#include "../app/title_saves.h"

namespace ps5porpoise
{
	// Every card under <saves>/User/GC, by region (USA, EUR, JAP), A before B, empty ones that are there too. With
	// no card at all an empty A in USA; each card without its other slot in its region that one, empty, with where
	// Dolphin will make it.
	std::vector<ps5titlesaves::Container> ReadCards(const std::string& savesDir);
	// Every Wii save under <saves>/User/Wii, by name
	ps5titlesaves::Container ReadWiiSaves(const std::string& savesDir);
	// The first USB drive, or empty
	std::string FirstUsbDrive();
	// Copied to the drive as a backup like the app's others, <drive>/PS5CEMU-HAR-saves/<stamp>/retro/porpoise/
	// (app/saves.h), so Restore from USB lists it; where: the copy. A whole-card image's saves aren't separate.
	bool ExportSave(const std::string& savesDir, const ps5titlesaves::TitleSave& save, const std::string& drive, const std::string& stamp, std::string& where, std::string& error);
	// A whole card, its folder or its image, the same way
	bool ExportCard(const std::string& savesDir, const ps5titlesaves::Container& card, const std::string& drive, const std::string& stamp, std::string& where, std::string& error);
	// What the drive holds, in its backups and Porpoise's own folder: GameCube saves to their cards (a Porpoise
	// export's onto cardFolder), the Wii's to their places; a save already there stays
	struct Imported { int gameCube = 0, wii = 0, skipped = 0; };
	bool ImportSaves(const std::string& savesDir, const std::string& cardFolder, const std::string& drive, Imported& imported, std::string& error);
	// What a drive's backups and Porpoise Saves folder hold, GameCube's or Wii's, newest backup first: where each is,
	// its file or folder, and for a card what its folder is in the backup
	struct UsbSave { std::string path, label, relative; };
	std::vector<UsbSave> FindUsbSaves(const std::string& drive, bool wii);
	struct UsbCard { std::string path, label; };
	std::vector<UsbCard> FindUsbCards(const std::string& drive);
	// One, onto card (its folder); or into its place for the Wii's. A save already there stays: false, with why.
	bool ImportOneSave(const std::string& savesDir, const UsbSave& save, const std::string& cardFolder, std::string& error);
	// Every save of a card's folder on the drive, onto card; those already there stay
	bool ImportCard(const UsbCard& card, const std::string& cardFolder, Imported& imported, std::string& error);
	// Never deleted: moved to <saves>/before/<stamp>/, in the same shape. Where to, or empty with why in error.
	std::string MoveSaveAside(const std::string& savesDir, const ps5titlesaves::TitleSave& save, const std::string& stamp, std::string& error);
}
