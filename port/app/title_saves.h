// SPDX-License-Identifier: GPL-3.0-or-later
// One game's saves among many on a card or in a folder, for any side whose emulator keeps them that way:
// what a side's reader lists and its Settings page shows. The GameCube's and Wii's: port/retro/porpoise_saves.h.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5titlesaves
{
	// RGBA8
	struct Picture
	{
		int width = 0, height = 0;
		std::vector<uint8_t> rgba;
	};
	struct TitleSave
	{
		std::string path;		// its file or folder
		std::string code;		// the game's code (GZLE01)
		std::string title;		// its own name for itself
		std::string detail;		// its second line
		int blocks = 0;			// on a card; 0 where a save isn't counted so
		uint64_t bytes = 0;
		int64_t changed = 0;	// seconds since 1970
		bool separate = false;	// a file or folder of its own, so it can be copied or moved alone
		Picture icon, banner;	// either may be empty
		uint32_t iconTexture = 0, bannerTexture = 0; // where a page has made them textures
	};
	// A memory card, or all of a console's saves
	struct Container
	{
		std::string id;			// "A", "B", "wii"
		std::string region;		// a card's: "USA", "EUR", "JAP"; its saves are only that region's games'
		std::string folder;		// where a save copied in goes; empty for a whole-card image
		int totalBlocks = 0, freeBlocks = 0;
		std::vector<TitleSave> saves;
	};
}
