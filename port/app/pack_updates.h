// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the Cemu community graphic packs, as Cemu's "Download latest community graphic packs"
// gets them (gui/wxgui/DownloadGraphicPacksWindow.cpp): GitHub's latest release of
// cemu-project/cemu_graphic_packs, when it is newer than the packs in graphicPacks/
// downloadedGraphicPacks (version.txt), downloaded and unpacked in their place. The app does not carry
// them (#74): the first time Cemu starts with none there they are downloaded in the background
// (StartIfMissing), and Settings brings them up to date. On a thread of its own; Cemu reloads its packs
// when it is done (ps5emu::ReloadGraphicPacks).

#pragma once

#include <cstdint>
#include <string>

namespace ps5packs
{
	struct Status
	{
		enum class State
		{
			Idle,
			Checking,	 // asking GitHub for the latest release
			Downloading, // received of total bytes
			Installing,	 // unpacking it
			UpToDate,	 // the installed packs are the latest
			Done,		 // installed: version
			Failed,		 // message says why
		} state = State::Idle;
		uint64_t received = 0, total = 0;
		std::string version; // the release's ("Github990")
		std::string message;
	};

	// The installed packs' version ("Github987"), from their version.txt; empty when there are none.
	std::string InstalledVersion();
	// Whether a release name is newer than another (by the number in it: Github990 over Github987).
	bool Newer(const std::string& candidate, const std::string& than);

	// Starts a check and, when GitHub has newer packs, their download and install.
	void Start();
	// Whether downloadedGraphicPacks holds nothing (packs copied there by hand, with no version.txt, count)
	bool Missing();
	// Start, once a session, when Missing: the packs come with the first start that has none.
	void StartIfMissing();
	Status GetStatus();
	// Before a game: a download running is stopped (what was installed stays as it was).
	void Stop();
}
