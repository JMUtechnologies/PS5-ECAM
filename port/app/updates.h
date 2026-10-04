// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the update notice. When the app starts fresh (not coming back from a game), one
// background request asks GitHub for the latest release; when it is newer than this build, a PS5
// notification says so and where to get it. Nothing is downloaded or installed.

#pragma once

#include <cctype>
#include <string>

namespace ps5update
{
	// Starts the check (once per process; it gives up quietly without a network).
	void Start();
	// Before a game: the check is cut short and waited for (at most a moment).
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
