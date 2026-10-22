// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden.h"
#include "ingame.h"
#include "library.h"
#include "../app/lang.h"
#include "../app/paths.h"
#include "../ps5/log.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sys/stat.h>

namespace ps5eden
{
	namespace
	{
		constexpr const char* kProgram = "eden/eboot.bin";
		// Eden's names for the settings' values (its settings_store.h)
		constexpr const char* kResolutions[] = {"0.5x", "0.75x", "1x", "1.5x", "2x", "3x", "4x"};
		constexpr const char* kFilters[] = {"bilinear", "fsr", "bicubic", "nearest"};
		constexpr const char* kAstc[] = {"compressed", "full"};
		constexpr const char* kSpeedLimits[] = {"100", "150", "200", "300", "unlimited"};
		// the lines of Eden's trace a boot log takes, the last ones
		constexpr size_t kTraceLines = 60;

		void Set(std::vector<std::string>& args, const std::string& setting)
		{
			args.push_back("--set");
			args.push_back(setting);
		}

		// Eden's trace (its boot_trace.h, "[ProsperoEden diag] +123ms ..." lines), its last lines into
		// the boot log
		void LogTrace(const std::string& trace)
		{
			std::ifstream in(trace);
			std::deque<std::string> lines;
			for (std::string line; std::getline(in, line);)
			{
				lines.push_back(std::move(line));
				if (lines.size() > kTraceLines)
					lines.pop_front();
			}
			for (const std::string& line : lines)
				ps5log::Line("[switch] eden: {}", line);
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
		// ProsperoEden's frame generation with MihawkGen, whose network it ships (Lossless Scaling would
		// need the player's own copy), and Eden's speed limit (patches/eden 0006)
		Set(args, std::string("frame_gen=") + (nx.nxFrameGeneration ? "1" : "0"));
		if (nx.nxFrameGeneration)
			Set(args, "frame_gen_method=mihawkgen");
		Set(args, std::string("speed_limit=") + kSpeedLimits[std::clamp(nx.nxSpeedLimit, 0, 4)]);
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
		ps5emu::LoadExecApp(kProgram, GameArguments(game, forGame, exitAfterGame), true, RequestFile());
		error = ps5lang::Tr("The console did not start Eden. The boot log says what it answered.");
	}

	void WriteSession(const Session& session)
	{
		// a line each, key=value, its line breaks taken out
		auto line = [](std::ofstream& out, const char* key, std::string value) {
			std::replace(value.begin(), value.end(), '\n', ' ');
			out << key << '=' << value << '\n';
		};
		const std::string file = ps5ingamenx::SessionFile(), staged = file + ".new";
		{
			std::ofstream out(staged, std::ios::trunc);
			line(out, "title", session.title);
			line(out, "details", session.details);
			line(out, "cover", session.cover);
			line(out, "language", ps5lang::Current());
			line(out, "font", ps5lang::GetMenuFont().path);
			line(out, "face", std::to_string(ps5lang::GetMenuFont().face));
			line(out, "exit", ps5emu::ExitAfterGame() ? "1" : "0"); // a home screen tile's game: its menu quits to the home screen
			if (!out)
			{
				ps5log::Line("[switch] {} could not be written: Eden shows its own loading screen", staged);
				return;
			}
		}
		std::rename(staged.c_str(), file.c_str());
	}

	std::string RequestFile()
	{
		return DataFolder() + "/launch-request.txt";
	}

	std::string TakeStaleRequest()
	{
		const std::string request = RequestFile();
		struct stat asked{};
		if (stat(request.c_str(), &asked) != 0)
			return {};
		std::remove(request.c_str());
		// Eden starts its trace in the app's sandbox before it asks for /data, so a trace newer than the
		// request is that launch's: Eden ran, but could not read what it was started with
		const std::string trace = ps5paths::SandboxDownload() + "/boot-trace.txt";
		struct stat traced{};
		if (stat(trace.c_str(), &traced) == 0 && traced.st_mtime >= asked.st_mtime)
		{
			ps5log::Line("[switch] Eden started but did not take its launch request ({}): it reads it from /data. Its trace, {}:", request,
				trace);
			LogTrace(trace);
			return {};
		}
		ps5log::Line("[switch] Eden never took its launch request ({}), and has no trace since: the console did not run "
					 "eden/eboot.bin and started this app again instead (boot.prev.log has the LoadExec it was asked for)",
			request);
		return ps5lang::Tr("The console did not start Eden. The boot log says what it answered.");
	}
}
