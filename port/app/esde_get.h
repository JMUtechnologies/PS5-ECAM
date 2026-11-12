// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: EmulationStation (ES-DE, port/esde/esde.h) as a download from inside the app, so the
// release stays small and only who wants it fetches it. Settings > Launcher (and the setup's launcher
// question) ask for it: GitHub's release tagged kTag holds ES-DE-PS5.zip (the es-de folder, made by
// `make esde-zip`); its SHA-256 is the digest GitHub gives the asset. It is unpacked beside the app's
// folder and moved into it as one folder, so the app has ES-DE whole or not at all, and an update of the
// app leaves it where it is (port/app/updates.cpp moves only the release's own files).

#pragma once

#include <cstdint>
#include <string>

namespace ps5esdeget
{
	// The release ES-DE's download is on: a new tag when ES-DE's side of the handoff changes
	constexpr const char* kTag = "es-de-1";
	constexpr const char* kAsset = "ES-DE-PS5.zip";

	struct Status
	{
		enum class State
		{
			Idle,
			Finding, // asking GitHub where the download is
			Downloading,
			Verifying,
			Installing,
			Done,	// ES-DE is in the app (ps5esde::Available)
			Failed, // message says why
		} state = State::Idle;
		uint64_t received = 0, total = 0;
		std::string message;
	};

	// Downloads and installs ES-DE in the background; nothing when one is under way. Only ever on the
	// player's word (Settings > Launcher's row held, or the setup's ES-DE card confirmed): never on focus
	void Start();
	Status GetStatus();
	// Whether a download or install is under way
	bool Busy();
	// Stops a download (not once its files are moving into place): what it wrote is removed
	bool Cancel();
	// Before a game: a download is cut short and waited for
	void Stop();
	// ES-DE out of the app (its folder; its home with what it scraped stays in the data folder). False,
	// with why, when it could not be
	bool Remove(std::string& error);
	// About how much the download is, before GitHub is asked (Settings and the setup show it before the
	// player says yes): the program and two themes (tools/build-esde.sh)
	constexpr uint64_t kAboutBytes = 92ull << 20;
}
