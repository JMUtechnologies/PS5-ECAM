// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: how a game runs in the app, for the launcher's badges and game pages: Playable, Issues or
// Not playable. One feed for every system (tools/compat_feed.py): the owner's own statuses
// (tools/compat-list.json) and what players' reports say, which the site makes every
// hour (premohq.github.io/PlayTendoh/status.json). The console fetches it a few seconds after the
// launcher is up (compat_fetch.cpp), keeps it (cache/compatibility.json, with its ETag), and reads that
// copy until a newer one comes; with none yet, the one the app came with (assets/compatibility.json, the
// owner's statuses alone). A game is found by its side and title ID, else by its name, compared without case,
// spaces or punctuation ("Batman™: Arkham Origins" is "Batman: Arkham Origins").

#pragma once

#include <cstdint>
#include <string>

namespace ps5compat
{
	// what the console fetches, and where it keeps it
	constexpr const char* kUrl = "https://premohq.github.io/PlayTendoh/status.json";

	struct Report
	{
		std::string name;	// as the feed has it
		std::string region; // the region and version reported, or empty
		std::string status; // Playable, Issues or Not playable, as the launcher shows it (TrC "status")
		std::string notes;	// the list's, or empty
		int consoles = 0, sessions = 0, ended = 0; // players' reports: consoles, sessions that said, ended well
	};

	// The report for a game of a side (launcher.h's SideName: "wiiu", "nes"), by its title ID (0: none)
	// or its name; null when the feed has none. Reports stay where they are for the app's life: a newer
	// feed adds its own (Generation), it never frees the old.
	const Report* Find(const std::string& side, uint64_t titleId, const std::string& name);
	// How the launcher marks a status: "good", "warn" or "bad"; empty for none.
	const char* Kind(const std::string& status);
	// Goes up each time a newer feed is read: the launcher finds its games' reports again.
	int Generation();

	// A feed's text, read and made the one Find uses when it parses; false when it does not (kept: the
	// one before). The fetch's, and the tests'.
	bool Take(const std::string& text);
	// The kept copy (cache/compatibility.json) and its ETag, for the fetch
	std::string CachePath();
	std::string CachedTag();
	// A fresh feed from the site: kept with its ETag, then read (Take). False when it does not parse.
	bool Keep(const std::string& text, const std::string& etag);

	// Fetched on a thread of its own, once a launch, a few seconds after the launcher is up (offline: the
	// kept copy, then the bundled one). StopFetch first, before a game: nothing runs beside one.
	void StartFetch();
	void StopFetch();

	// Where the bundled feed and the kept copy are (default: assets/compatibility.json and
	// cache/compatibility.json); the launcher's preview and the tests give their own. Empty cache: none.
	void SetPaths(const std::string& bundled, const std::string& cache);
}
