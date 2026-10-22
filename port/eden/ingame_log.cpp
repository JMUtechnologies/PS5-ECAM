// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the port's log (ps5/log.h) in Eden's program, for the Switch side's loading screen and in-game
// menu (ingame.h): its lines go to Eden's own output, which its log keeps, never to the app's boot log.
#include "../ps5/log.h"

#include <cstdio>

namespace ps5log
{
	void Write(std::string_view line)
	{
		std::printf("%.*s\n", (int)line.size(), line.data());
		std::fflush(stdout);
	}
}
