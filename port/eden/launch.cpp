// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden.h"
#include "library.h"
#include "../app/lang.h"
#include "../ps5/log.h"

#include <algorithm>

namespace ps5eden
{
	namespace
	{
		constexpr const char* kProgram = "eden/eboot.bin";
		// Eden's names for the settings' values (its settings_store.h)
		constexpr const char* kResolutions[] = {"0.5x", "0.75x", "1x", "1.5x", "2x", "3x", "4x"};
		constexpr const char* kFilters[] = {"bilinear", "fsr", "bicubic", "nearest"};
		constexpr const char* kAstc[] = {"compressed", "full"};

		void Set(std::vector<std::string>& args, const std::string& setting)
		{
			args.push_back("--set");
			args.push_back(setting);
		}
	}

	std::vector<std::string> GameArguments(const ps5emu::Game& game, const ps5settings::Launcher& forGame, bool exitAfterGame)
	{
		const ps5settings::N3ds& nx = forGame.nx;
		std::vector<std::string> args = {"--rom", game.path.string()};
		if (exitAfterGame)
			args.push_back("--exit-after-game");
		Set(args, std::string("resolution=") + kResolutions[std::clamp(nx.nxResolution, 0, 6)]);
		Set(args, std::string("upscaling_filter=") + kFilters[std::clamp(nx.nxFilter, 0, 3)]);
		Set(args, std::string("astc_textures=") + kAstc[std::clamp(nx.nxAstc, 0, 1)]);
		Set(args, std::string("console_mode=") + (nx.nxDocked ? "docked" : "handheld"));
		Set(args, "volume=" + std::to_string(std::clamp(nx.volume, 0, 100)));
		Set(args, std::string("fps_overlay=") + (nx.performance ? "1" : "0"));
		Set(args, std::string("vibration=") + (forGame.rumble ? "1" : "0")); // Settings > Controls' Vibration, every side's
		return args;
	}

	void LaunchGame(const ps5emu::Game& game, const ps5settings::Launcher& forGame, bool exitAfterGame, std::string& error)
	{
		if (!Available())
		{
			error = ps5lang::Tr("This build has no Switch emulator: eden/eboot.bin is not in the app's folder.");
			return;
		}
		ps5log::Line("[switch] starting {} in Eden ({})", game.name, game.path.string());
		ps5emu::LoadExecApp(kProgram, GameArguments(game, forGame, exitAfterGame), true);
		error = ps5lang::Tr("The console did not start Eden. The boot log says what it answered.");
	}

	void OpenLauncher(std::string& error)
	{
		if (!Available())
		{
			error = ps5lang::Tr("This build has no Switch emulator: eden/eboot.bin is not in the app's folder.");
			return;
		}
		ps5log::Line("[switch] opening Eden's own launcher");
		ps5emu::LoadExecApp(kProgram, {}, true);
		error = ps5lang::Tr("The console did not start Eden. The boot log says what it answered.");
	}
}
