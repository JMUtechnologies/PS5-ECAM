// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Azahar's core on the PS5, as Cemu's is (app/emulator.h): a 3DS game on Azahar's
// Vulkan renderer on VideoOut, the DualSense as the 3DS (controls.h), the sound through AudioOut,
// and installing CIA files into its storage. The launcher includes this header, so it names no
// Azahar type.

#pragma once

#include "../app/emulator.h"
#include "../app/paths.h"
#include "../frontend/settings.h"

#include <cstdint>
#include <string>

namespace ps5azahar
{
	// Where Azahar keeps its data: its settings, the 3DS's storage (sdmc, nand), its caches.
	constexpr const char* kRoot = PS5CEMU_DATA "/azahar";

	// The 3DS's screens on the TV (settings: layout)
	enum class Layout
	{
		Stacked,	 // the top screen above the bottom one
		TopOnly,	 // the top screen alone
		LargeTop,	 // the top screen large, the bottom one small beside it
		SideBySide,	 // the two side by side, the same size
	};

	// Whether this build has Azahar's core.
	bool Available();

	// Azahar's core with the launcher's settings, the renderer on VideoOut, then the game. False,
	// with a reason, when it cannot start.
	bool LaunchGame(const ps5emu::Game& game, const ps5settings::N3ds& settings, std::string& error);
	// Runs while the game does. Returns when the player asks for the library.
	void RunGame();
	// Whether the last LaunchGame got as far as starting Azahar's core: after a failure from there
	// on, only a fresh process is safe to start a game in.
	bool CoreTouched();

	// Artic Base: a game played from a 3DS on the network, which runs the Artic Base server (a
	// homebrew app) and streams its game card or installed game, saves and system data. A game whose
	// path is one of these, followed by the 3DS's address ("articbase://192.168.1.20", optionally
	// ":port", 5543 by default), is Azahar's Artic loader's. The setup ones are the Artic Setup
	// Tool's: they copy the system files and console data of an old or a new 3DS into Azahar's NAND.
	constexpr const char* kArticBase = "articbase://";
	constexpr const char* kArticSetupOld = "articinio://";
	constexpr const char* kArticSetupNew = "articinin://";
	inline bool IsArtic(const std::string& path)
	{
		return path.starts_with(kArticBase) || path.starts_with(kArticSetupOld) || path.starts_with(kArticSetupNew);
	}

	// Installing a CIA (a game, an update or DLC) into the 3DS's storage, as Azahar's "Install CIA"
	// does, on a thread of its own.
	bool StartInstall(const std::string& cia, std::string& error);
	ps5emu::InstallStatus GetInstallStatus();
	void CancelInstall();
}
