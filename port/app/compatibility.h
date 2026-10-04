// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: how a game runs in the app, as players have reported it: the compatibility list
// (docs/COMPATIBILITY.md, bundled as assets/compatibility.md), read for the launcher's game pages.
// A game is found by its name, compared without case, spaces or punctuation ("Batman™: Arkham
// Origins" is "Batman: Arkham Origins").

#pragma once

#include <string>

namespace ps5compat
{
	struct Report
	{
		std::string name;	// as the list has it
		std::string region; // the region and version reported
		std::string status; // Playable, Issues, Crashes or Won't start
		std::string notes;
	};

	// The report for a game of the side (n3ds: the 3DS's table), or null when none is listed.
	const Report* Find(bool n3ds, const std::string& name);
	// How the launcher marks a status: "good", "warn" or "bad"; empty for none.
	const char* Kind(const std::string& status);

	// Where the list is (default: the app's assets/compatibility.md); the launcher's preview gives
	// the repository's.
	void SetPath(const std::string& path);
}
