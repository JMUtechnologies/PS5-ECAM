// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: updating the app from inside it. When the app starts fresh (not coming back from a
// game), one background request asks GitHub for the latest release; when it is newer than this build,
// the launcher asks whether to install it. Installing downloads the release's ZIP from the
// repository's releases branch, checks it against the SHA256SUMS beside it, unpacks it next to the
// app's folder and moves its files into place, eboot.bin last; the app then starts again as the new
// version. Games, saves and settings (in /data/ps5cemu) are not touched.

#pragma once

#include <cctype>
#include <cstdint>
#include <string>

namespace ps5update
{
	struct Status
	{
		enum class State
		{
			Idle,
			Checking,
			UpToDate,
			Available, // a newer release: latest
			Downloading,
			Verifying,
			Installing,
			Ready,	// installed: the app starts again to run it
			Failed, // message says why
		} state = State::Idle;
		std::string latest;				   // the newest release's version, as its tag has it ("v3.0.1")
		uint64_t received = 0, total = 0; // the download's bytes
		std::string message;
		bool installing = false; // Install() was asked for (the launcher shows how it goes)
	};

	// Starts the check (once per process; it gives up quietly without a network).
	void Start();
	// Asks GitHub again (Settings).
	void Check();
	// Downloads and installs the newer release the check found.
	void Install();
	Status GetStatus();
	// Whether the launcher should ask about an update now (one found and not answered, or an
	// install under way or finished); Dismiss is "later": not asked again in this process.
	bool Prompting();
	void Dismiss();
	// Starts the app again, as the version just installed. Returns only if that did not take.
	void Restart();
	// Before a game: the check or download is cut short and waited for.
	void Stop();

	// The version as people read it: "2.0.0d" is "2.0.0 D".
	inline std::string Readable(const std::string& version)
	{
		std::string text = version;
		if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
			text.erase(0, 1);
		if (!text.empty() && std::isalpha((unsigned char)text.back()))
			text = text.substr(0, text.size() - 1) + " " + (char)std::toupper((unsigned char)text.back());
		return text;
	}
}
