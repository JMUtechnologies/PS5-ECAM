// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a libretro core's game on the console, as the DS side's is (port/melonds/melonds.h): the
// side's core (plays.h) with the side's settings and the game's own over them, its picture on VideoOut
// through the DS side's screens (port/melonds/screens.h, its one picture), its sound through AudioOut,
// the DualSense as its RetroPad (the side's controls), and the handhelds' in-game menu
// (ps5ingame3ds::Console::Retro) over it. The launcher includes this header, so it names no libretro type.
//
// Its files are under the side's data folder (ps5retro::kSystems' data):
//   saves/<game file>.srm     the game's save RAM, RetroArch's name (session.h)
//   states/<game file>.state1-5, .state11  save states; the one quitting saves (ps5saves::kResumeSlot)
//   system/                   the core's own files: a BIOS where its system has one
#pragma once

#include "plays.h"
#include "../app/emulator.h"
#include "../frontend/settings.h"

#include <string>
#include <utility>
#include <vector>

namespace ps5libretro
{
	// The side's core with its settings (the game's own over them), the picture on VideoOut, then the
	// game. False, with a reason, when it cannot start.
	bool LaunchGame(int side, const ps5emu::Game& game, const ps5settings::N3ds& settings, std::string& error);
	// Runs while the game does. Returns when the player asks for the library, or the core stops.
	void RunGame();
	// Before LaunchGame: whether the game goes on from the state its last quit saved (Resume where you
	// left off). Either way, quitting to the library saves one.
	void SetResume(bool resume);
	// Whether the last LaunchGame took VideoOut: after a failure from there on, only a fresh process
	// can show the launcher again.
	bool CoreTouched();

	// A core option as Settings > Emulator shows it: what the core declares (in the launcher's language
	// where the core has its words), and the value the app gives it when the player has chosen none
	struct CoreOption
	{
		std::string key, label, info, category;
		std::vector<std::pair<std::string, std::string>> values; // the value, and its label
		std::string defaultValue;
	};
	// The options of the side's core in the app, asked of it once a language; empty when the side has none
	// (an emulator of its own, or no core in this copy) or a game is running
	const std::vector<CoreOption>& CoreOptions(int side);
}
