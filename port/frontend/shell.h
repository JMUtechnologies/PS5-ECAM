// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the new launcher (docs/UI-REDESIGN.md), drawn on the GPU with the UI kit (port/ui):
// the Wii U and 3DS sides of one shell, switched in one press, each with Home, the Library and
// Settings, a game's hub and Game menu, and the pages the classic launcher has (graphic packs, a
// player's controls and buttons, the folder browser and installs, Artic Base, Diagnostics), the
// Setup check, the update sheet and the launch.
//
// It takes the classic launcher's place (launcher.h) and returns what it returns: the game chosen,
// with everything of its own gone first (its device, its threads, VideoOut handed back to the
// driver for the emulator's renderer: 4.2, rule 1). The classic launcher stays as the fallback:
// ps5cemu.json's ui.classic, set by holding L1 as the app starts and cleared by holding R1, or by
// the new launcher's Settings > General > Display.

#pragma once

#include "launcher.h"
#include "settings.h"
#include "../ui/gfx.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ps5shell
{
	// Where the launcher draws and finds its files: on the console VideoOut through the driver the app
	// links (host_ps5.cpp); the preview on a PC gives its own (tools/launcher-preview)
	struct Host
	{
		PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
		std::function<ui::Target()> target;
		std::string assets;						  // the app's assets/ui
		std::vector<std::string> fontFolders;	  // fonts for what Lexend lacks
		std::function<double()> clock;			  // seconds
		std::function<void()> afterFrame;		  // the preview's script
	};
	Host DefaultHost();
	void SetHost(Host host);

	// Whether this start is the new launcher's: ui.classic, changed first by L1 or R1 held as the
	// app starts (saved, and told in a notification)
	bool Wanted(ps5settings::Launcher& settings);

	enum class Outcome
	{
		Chosen,	   // a game: the choice
		Classic,   // it could not start before VideoOut was taken: the classic launcher can run instead
		Restart,   // it could not go on after VideoOut was taken: the app starts again, in the classic one
		Failed,	   // nothing can show
	};

	// As ps5launcher::Run: the launcher until a game is chosen, prepare called each time a side opens
	Outcome Run(ps5settings::Launcher& settings, ps5launcher::Status& status, const std::function<void(ps5launcher::System)>& prepare,
		std::optional<ps5launcher::Choice>& choice);
}
