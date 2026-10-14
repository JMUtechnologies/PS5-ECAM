// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Wii U mods from a USB drive (docs/MODS.md). A Cemu mod is a graphic pack: a rules.txt
// beside the folders whose files take the game's place (content/, aoc/, code/) and its patches and
// shaders, which Cemu finds anywhere in graphicPacks/ (GraphicPack2::LoadAll). The mods in a drive's
// ps5cemu/mods folder are copied into graphicPacks/mods. A mod shared without a rules.txt (its
// content/ and aoc/ alone, as many are) is given one, for the game its folder is in (a folder named
// by the title ID) or else the game it was added from. Then Cemu loads its packs again
// (ps5emu::ReloadGraphicPacks): each mod is on its game's Graphic packs page, off until turned on.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5mods
{
	struct Status
	{
		enum class State
		{
			Idle,
			Copying, // copied of total bytes; current names the mod
			Done,	 // mods copied (0: none found), skipped left out
			Failed,	 // message says why
		} state = State::Idle;
		uint64_t copied = 0, total = 0;
		int mods = 0;
		int skipped = 0; // with no rules.txt and no game to give one
		std::string current;
		std::string message;
		uint32_t finished = 0; // the runs that ended, so the launcher answers each once
	};

	// Where mods are looked for on the drives given: each one's ps5cemu/mods folder (or mods/)
	std::vector<std::string> Folders(const std::vector<std::string>& drives);
	// The mods in those folders copied into graphicPacks/mods, on a thread of its own. titleId and
	// name: the game a mod with no rules.txt and no title ID folder is for (0: such mods are left out)
	void Start(const std::vector<std::string>& drives, uint64_t titleId, const std::string& name);
	Status GetStatus();
	// Before a game: a copy running stops (the mod it was copying is left as it was)
	void Stop();
	// graphicPacks/mods
	std::string Folder();
}
