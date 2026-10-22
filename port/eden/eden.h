// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the Switch side's games start in Eden's own program, eden/eboot.bin in the app's
// folder (library.h), which takes this process's place (sceSystemServiceLoadExec). It starts the game
// at once (--rom), with the Switch side's settings and the game's own over them (--set key=value), and
// starts this app's eboot.bin again when the game ends (--return), which opens on the Switch side
// (ps5cemu.json's side, saved before); a home screen tile's game closes the app instead
// (--exit-after-game). ProsperoEden's own launcher is never shown: Eden draws PS5-ECAM's loading screen
// and in-game menu over the game (ingame.h), from what WriteSession leaves it.
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

	// What Eden's drawing of the loading screen and the in-game menu needs of the game (ingame.h): its
	// name, GameTDB's publisher and year, its cover's TGA, and the menus' language and second font
	// (ps5lang's, as the launcher hands over), in ps5ingamenx::SessionFile
	struct Session
	{
		std::string title, details, cover;
		uint64_t titleId = 0; // the game's settings are kept under it
	};
	void WriteSession(const Session& session);

	// What Eden's program left as it ended (ps5ingamenx::ResultFile), taken away as the app starts again:
	// whether its game ended, failed (and why) or never ended (a crash), and what its in-game menu changed
	struct Result
	{
		bool found = false;
		std::string state, error;
		uint64_t titleId = 0;
		bool changed = false;
		struct Settings
		{
			int filter = 0, antiAliasing = 0, sharpening = 25, speedLimit = 0, volume = 100, controller = -1, deadzone = 8;
			bool frameGeneration = false, overlay = false, docked = true, vibration = true;
		} settings;
	};
	Result TakeResult();
	// What the Switch side says of it, in the player's language; empty when all went well
	std::string ResultMessage(const Result& result);
	// What the in-game menu changed, kept as the other sides' menus keep theirs (ps5settings::SaveChanges)
	void KeepMenuChanges(const Result& result);
	// The nickname shown in a room: Settings > Multiplayer's, else ProsperoEden's own
	std::string Nickname(const ps5settings::N3ds& nx);

	// The Switch's twelve buttons on the DualSense, as Eden maps them (its button_mapping.h): each on
	// one of the DualSense's buttons, the touchpad's click included, kept by name in the Switch side's
	// settings (N3ds::buttons) where it is not ProsperoEden's default (A on Circle, B on Cross, + on
	// Options, - on the touchpad's click). Setting one swaps it with the button that had that input,
	// so no DualSense button presses two; None puts its default back.
	std::vector<ps5emu::ButtonMapping> ListMappings(const ps5settings::N3ds& nx);
	void SetMapping(ps5settings::N3ds& nx, size_t index, ps5emu::PadInput input);
	// Whether an input can be a Switch button: the DualSense's buttons, not its D-pad or sticks
	bool Mappable(ps5emu::PadInput input);
	// Every button's, as the game's --set mapping= hands them to Eden ("a:circle,b:cross,...")
	std::string MappingValue(const ps5settings::N3ds& nx);

	// The Switch's system languages, as Eden names them (its settings_store.h's kLanguageKeys), each in
	// its own language
	struct Language
	{
		const char* key;
		const char* name;
	};
	extern const Language kLanguages[18];
	// The one a game sees: nxLanguage's, or for -1 the menus' (ps5lang) where the Switch has it, else
	// English (US)
	int LanguageFor(const ps5settings::N3ds& nx);

	// Where the arguments Eden is started with also go, one a line: Eden's data folder's
	// launch-request.txt
	std::string RequestFile();
	// At the app's start: a request Eden never took, said in the boot log (with Eden's trace from the
	// app's sandbox, when it started without /data) and taken away. Why the Switch side's last game did
	// not start when the console started this app again instead of Eden; empty otherwise
	std::string TakeStaleRequest();
}
