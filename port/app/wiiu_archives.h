// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Wii U games in archives (game_archives.h) for the Wii U side's library, which Cemu's own
// scan does not read: a title's folder (code, content and meta) in a .zip, .7z, .rar or tar archive in
// the game files folder, read on a thread of its own beside Cemu's scan. The game is the archive's first
// base title by its folder's name, named by its meta.xml and identified by its code/app.xml as Cemu
// does, its icon and boot screen written where covers.cpp writes a title's own. What a scan read of an
// archive is kept in Root()/archives.bin by the archive's size and time, so a later scan reads nothing
// out of an archive that has not changed.

#pragma once

#include "emulator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ps5emu
{
	namespace archives
	{
		// Looks for games in archives in the folders and the folders in them, on a thread of its own; a
		// scan already running goes on with the folders given once it is done
		void StartScan(const std::vector<std::string>& folders);
		bool Scanning();
		// Stops a scan running, and joins its thread (before a game)
		void Stop();
		// What the last scan found, sorted by name
		std::vector<Game> Games();
	}

	// The ID on a game's box (GameTDB's) from its meta.xml: the product code's last part and the
	// company code's last two digits (WUP-P-ALZE and 0001: ALZE01)
	std::string BoxId(const std::string& productCode, const std::string& companyCode);

	// covers.cpp's: where a title's icon (or its boot screen) is written as the launcher reads it, and
	// writing it there from the TGA a game's archive has (meta/iconTex.tga, bootTvTex.tga), once
	std::string CoverFile(uint64_t titleId, bool bootScreen);
	bool WriteCover(uint64_t titleId, bool bootScreen, const std::vector<uint8_t>& tga);
}
