// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the Switch side's games start in Eden's own program, eden/eboot.bin in the app's
// folder (library.h), which takes this process's place (sceSystemServiceLoadExec). It starts the game
// at once (--rom), with the Switch side's settings and the game's own over them (--set key=value), and
// starts this app's eboot.bin again when the game ends (--return), which opens on the Switch side
// (ps5cemu.json's side, saved before); a home screen tile's game closes the app instead
// (--exit-after-game). Eden's own launcher, with all it has besides (mods, cheats, updates and DLC,
// profiles, rooms, its per-game settings), starts the same way without a game.
//
// On the console LoadExec does not carry a program's arguments (MihawkOS saw argc 1 on every
// restart, and 5.0.0's Switch games never started, #67): they go in RequestFile too, which Eden takes
// as it starts (patches/eden), once it has /data. A request still there when this app starts again
// was never taken.

#pragma once

#include "../app/emulator.h"
#include "../frontend/settings.h"

#include <string>
#include <vector>

namespace ps5eden
{
	// The arguments a game starts with: its path, and the side's settings with the game's own over them
	// (ps5settings::ForGame's), as Eden names them
	std::vector<std::string> GameArguments(const ps5emu::Game& game, const ps5settings::Launcher& forGame, bool exitAfterGame);
	// Eden's program in place of this one, the game started at once. Returns only when it could not
	// start, with why
	void LaunchGame(const ps5emu::Game& game, const ps5settings::Launcher& forGame, bool exitAfterGame, std::string& error);
	// Eden's own launcher, the same way
	void OpenLauncher(std::string& error);

	// Where the arguments Eden is started with also go, one a line: Eden's data folder's
	// launch-request.txt
	std::string RequestFile();
	// At the app's start: a request Eden never took, said in the boot log (with Eden's trace from the
	// app's sandbox, when it started without /data) and taken away. Why the Switch side's last game did
	// not start when the console started this app again instead of Eden; empty otherwise
	std::string TakeStaleRequest();
}
