// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: what the launcher (shell.h) and main_ps5.cpp share: the four sides, what the launcher
// shows of their state, and the game it returns.

#pragma once

#include "../app/emulator.h"
#include "settings.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ps5launcher
{
	enum class System
	{
		WiiU,	// Cemu
		N3ds,	// Azahar
		Nds,	// melonDS
		Switch, // Eden, in a program of its own (eden/eboot.bin: port/eden)
	};
	// the sides, in the bar's order (their numbers)
	constexpr int kSideCount = 4;

	// The side's name in ps5cemu.json ("wiiu", "3ds", "ds", "switch") and the one players read
	inline const char* SideName(System side)
	{
		return side == System::N3ds ? "3ds" : side == System::Nds ? "ds" : side == System::Switch ? "switch" : "wiiu";
	}
	inline System SideNamed(const std::string& name)
	{
		return name == "3ds" ? System::N3ds : name == "ds" ? System::Nds : name == "switch" ? System::Switch : System::WiiU;
	}
	inline const char* SideTitle(System side)
	{
		return side == System::N3ds ? "3DS" : side == System::Nds ? "DS" : side == System::Switch ? "Switch" : "Wii U";
	}

	struct Status
	{
		bool coreReady = false;	 // Cemu started: its library can open
		bool coreStarting = false; // Cemu's library loading beside the launcher (ps5emu::StartCore), which polls it
		std::string notice;		 // a problem to show on the Wii U side's Library (empty: none)
		std::string notice3ds;	 // and on the 3DS side's
		std::string noticeDs;	 // and on the DS side's
		std::string noticeSwitch; // and on the Switch side's
		std::string launchError; // why the last Wii U game did not start, said once (empty: it did)
		std::string launchError3ds; // and the last 3DS game
		std::string launchErrorDs;  // and the last DS game
		std::string launchErrorSwitch; // and the last Switch game
		std::vector<std::string> diagnostics; // the lines Settings > Diagnostics shows
		// a home screen tile's game (--rom, app/forwarders.h): started as soon as its side can, on that
		// side, by its path (or its file's name); empty: none
		std::string autoLaunch;
		System autoLaunchSide = System::WiiU;
	};

	struct Choice
	{
		System system;
		ps5emu::Game game;
		// an archived game's unpacked copy, which its emulator starts (app/game_archives.h); empty: the
		// game's own path
		std::string launchPath;
	};
}
