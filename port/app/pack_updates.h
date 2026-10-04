// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the Cemu community graphic packs brought up to date, as Cemu's "Download latest
// community graphic packs" does (gui/wxgui/DownloadGraphicPacksWindow.cpp): GitHub's latest release
// of cemu-project/cemu_graphic_packs, when it is newer than the packs in graphicPacks/
// downloadedGraphicPacks (version.txt), downloaded and unpacked in their place. The app's bundled
// packs (emulator.cpp installs them on a first start) stay the fallback: a newer download is never
// replaced by them. From the launcher, on a thread of its own; Cemu reloads its packs when it is done
// (ps5emu::ReloadGraphicPacks).

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
	Status GetStatus();
	// Before a game: a download running is stopped (what was installed stays as it was).
	void Stop();
}
