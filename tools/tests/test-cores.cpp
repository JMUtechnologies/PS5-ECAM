// SPDX-License-Identifier: GPL-3.0-or-later
// Every libretro core the app links, for real, through the app's own frontend (port/libretro/session.cpp,
// cores.cpp): built for the PC by tools/build-libretro.py (LIBRETRO_HOST=1) and given the tiny games
// make-test-games.py writes. For each: its options listed with no game (Settings > Emulator), the game
// loaded, ten seconds of frames run with a picture and sound, a save state saved and loaded, and its memory
// mapped for RetroAchievements. Game images that no tiny file can stand for (the N64's needs its boot chip's
// code, the Game & Watch's its artwork) check that the core starts and turns a bad file down cleanly.
// tools/tests/test-cores.sh builds and runs it; PASS and FAIL lines, exit 1 on a failure.

#include "libretro/core.h"
#include "libretro/session.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace
{
	int failures = 0;

	void Check(bool ok, const std::string& what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		failures += ok ? 0 : 1;
	}

	struct Case
	{
		const char* core;
		const char* game;
		const char* system;
		unsigned cheevos; // rc_consoles.h's id, 0: none
		bool runs = true; // false: a bad file it should refuse
		bool states = true;
		bool frames = true; // false: loaded and unloaded only (the N64's test image boots, but draws no frame)
	};
}

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		std::fprintf(stderr, "test-cores GAMES_DIR WORK_DIR\n");
		return 2;
	}
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const fs::path games = argv[1], work = argv[2];
	ps5libretro::SetLog([](int level, const std::string& line) {
		if (level >= 2)
			std::fprintf(stderr, "  [core] %s\n", line.c_str());
	});
	const Case cases[] = {
		{"mesen", "test.nes", "NES", 7},
		{"mesens", "test.sfc", "SNES", 3},
		{"mgba", "test.gb", "Game Boy", 4},
		{"mgba", "test.gbc", "Game Boy Color", 6},
		{"mgba", "test.gba", "Game Boy Advance", 5},
		{"beetlevb", "test.vb", "Virtual Boy", 28},
		{"pokemini", "test.min", "Pokémon mini", 24},
		{"paralleln64", "broken.z64", "Nintendo 64", 2, false},
		{"paralleln64", "test.z64", "Nintendo 64", 2, true, false, false},
		{"gw", "broken.mgw", "Game & Watch", 0, false},
	};
	for (const Case& c : cases)
	{
		const std::string name = std::string(c.system) + " (" + c.core + ")";
		std::printf("-- %s\n", name.c_str());
		const ps5libretro::Core* core = ps5libretro::Find(c.core);
		Check(core != nullptr, name + ": linked");
		if (!core)
			continue;
		const auto options = ps5libretro::Describe(*core, RETRO_LANGUAGE_ENGLISH);
		std::printf("     %zu options\n", options.size());
		Check(std::string(c.core) == "gw" || !options.empty(), name + ": its options listed with no game");
		ps5libretro::Setup setup;
		const fs::path folder = work / c.core / c.game;
		fs::create_directories(folder / "system");
		fs::create_directories(folder / "saves");
		fs::create_directories(folder / "states");
		setup.system = (folder / "system").string();
		setup.saves = (folder / "saves").string();
		setup.states = (folder / "states").string();
		// the options the app gives the N64's core on a console (port/libretro/game.cpp)
		if (std::string(c.core) == "paralleln64")
			setup.options = {{"parallel-n64-cpucore", "cached_interpreter"}, {"parallel-n64-gfxplugin", "angrylion"},
				{"parallel-n64-angrylion-multithread", "6"}};
		std::string error;
		const bool loaded = ps5libretro::Load(*core, (games / c.game).string(), setup, error);
		if (!c.runs)
		{
			Check(!loaded && !ps5libretro::Loaded(), name + ": a bad file turned down (" + error + ")");
			if (loaded)
				ps5libretro::Unload();
			continue;
		}
		Check(loaded, name + ": the game loads" + (loaded ? "" : " (" + error + ")"));
		if (!loaded)
			continue;
		if (!c.frames)
		{
			ps5libretro::Unload();
			Check(!ps5libretro::Loaded(), name + ": unloaded");
			continue;
		}
		ps5libretro::Pad pads[ps5libretro::kPlayers] = {};
		size_t sound = 0;
		const int frames = (int)(ps5libretro::FrameRate() * 10);
		for (int i = 0; i < frames; i++)
		{
			pads[0].buttons = (i / 30) % 2 ? (1 << RETRO_DEVICE_ID_JOYPAD_START) : 0;
			ps5libretro::Run(pads);
			sound += ps5libretro::Sound().size();
		}
		const auto& picture = ps5libretro::LastPicture();
		Check(picture.width > 0 && picture.height > 0 && picture.pixels.size() == size_t(picture.width) * picture.height,
			name + ": " + std::to_string(frames) + " frames, a " + std::to_string(picture.width) + "x" + std::to_string(picture.height) + " picture");
		Check(sound > 0, name + ": sound (" + std::to_string(sound / 2) + " samples)");
		if (c.states)
		{
			const bool saved = ps5libretro::SaveState(1, error);
			Check(saved, name + ": a save state saved" + (saved ? "" : " (" + error + ")"));
			if (saved)
			{
				for (int i = 0; i < 30; i++)
					ps5libretro::Run(pads);
				const bool back = ps5libretro::LoadState(1, error);
				Check(back, name + ": and loaded" + (back ? "" : " (" + error + ")"));
			}
		}
		if (c.cheevos)
		{
			const bool mapped = ps5libretro::CheevosMemory(c.cheevos);
			uint8_t byte = 0;
			Check(mapped && ps5libretro::CheevosRead(0, &byte, 1) == 1, name + ": its memory mapped for RetroAchievements");
		}
		ps5libretro::Unload();
		Check(!ps5libretro::Loaded(), name + ": unloaded");
	}
	// Mesen's HD packs (the NES side's texture packs): a pack in the system folder's HdPacks/<the game file's
	// name>/ (make-test-games.py's: one tile replaced, at twice the size) draws the game at twice its size
	if (const ps5libretro::Core* mesen = ps5libretro::Find("mesen"))
	{
		const fs::path folder = work / "mesen-hdpack";
		fs::create_directories(folder / "system/HdPacks");
		fs::remove_all(folder / "system/HdPacks/test");
		fs::copy(games / "hdpack", folder / "system/HdPacks/test", fs::copy_options::recursive);
		ps5libretro::Setup setup;
		setup.system = setup.saves = setup.states = (folder / "system").string();
		setup.options["mesen_hdpacks"] = "enabled";
		std::string error;
		const bool loaded = ps5libretro::Load(*mesen, (games / "test.nes").string(), setup, error);
		Check(loaded, "NES HD pack: the game loads with a pack" + (loaded ? "" : " (" + error + ")"));
		if (loaded)
		{
			ps5libretro::Pad pads[ps5libretro::kPlayers] = {};
			for (int i = 0; i < 10; i++)
				ps5libretro::Run(pads);
			const auto& picture = ps5libretro::LastPicture();
			Check(picture.width == 512 && picture.height == 480,
				"NES HD pack: the pack's scale (" + std::to_string(picture.width) + "x" + std::to_string(picture.height) + ", 512x480 wanted)");
			ps5libretro::Unload();
		}
	}
	// the DualSense's buttons reach the game: a program that copies its joypad register to RAM, read back
	// through the memory RetroAchievements sees
	struct Input
	{
		const char* core;
		const char* game;
		const char* system;
		unsigned cheevos;
		unsigned button;	// RETRO_DEVICE_ID_JOYPAD_*
		uint32_t address;	// where the program keeps the register
		uint8_t rest, held; // that byte with nothing pressed, and with the button
	};
	const Input inputs[] = {
		{"mesens", "input.sfc", "SNES", 3, RETRO_DEVICE_ID_JOYPAD_A, 0, 0x00, 0x80},
		{"mesens", "input.sfc", "SNES", 3, RETRO_DEVICE_ID_JOYPAD_B, 1, 0x00, 0x80},
	};
	for (const Input& in : inputs)
	{
		const ps5libretro::Core* core = ps5libretro::Find(in.core);
		if (!core)
			continue;
		ps5libretro::Setup setup;
		const fs::path folder = work / "input" / in.core;
		fs::create_directories(folder);
		setup.system = setup.saves = setup.states = folder.string();
		std::string error;
		if (!ps5libretro::Load(*core, (games / in.game).string(), setup, error))
		{
			Check(false, std::string(in.system) + " input: the program loads (" + error + ")");
			continue;
		}
		ps5libretro::CheevosMemory(in.cheevos);
		ps5libretro::Pad pads[ps5libretro::kPlayers] = {};
		for (int i = 0; i < 30; i++)
			ps5libretro::Run(pads);
		uint8_t rest = 0x5A, held = 0x5A;
		ps5libretro::CheevosRead(in.address, &rest, 1);
		pads[0].buttons = uint16_t(1u << in.button);
		for (int i = 0; i < 30; i++)
			ps5libretro::Run(pads);
		ps5libretro::CheevosRead(in.address, &held, 1);
		char what[160];
		std::snprintf(what, sizeof(what), "%s input: button %u read by the game (%02x at rest, %02x held)", in.system, in.button, rest, held);
		Check(rest == in.rest && held == in.held, what);
		ps5libretro::Unload();
	}
	// the NES's special controllers (port/libretro/devices.h): Mesen plugs in what an NES 2.0 header asks for and
	// says so (patches/libretro/Mesen 0002); the Arkanoid paddle turned by the mouse's movement and fired by its
	// button, as the program reads it from the second port; another controller given to a port
	if (const ps5libretro::Core* mesen = ps5libretro::Find("mesen"))
	{
		const unsigned arkanoid = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 0), zapper = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_POINTER, 0);
		const fs::path folder = work / "devices";
		fs::create_directories(folder);
		ps5libretro::Setup setup;
		setup.system = setup.saves = setup.states = folder.string();
		std::string error;
		ps5libretro::Pad pads[ps5libretro::kPlayers] = {};
		auto knob = [&](int16_t move, bool fire, uint8_t& bits, uint8_t& last) {
			pads[0].mouse[0] = move;
			pads[0].mouseButtons = fire ? 1 << RETRO_DEVICE_ID_MOUSE_LEFT : 0;
			for (int i = 0; i < 40; i++)
				ps5libretro::Run(pads);
			ps5libretro::CheevosRead(2, &bits, 1);
			ps5libretro::CheevosRead(0, &last, 1);
		};
		if (ps5libretro::Load(*mesen, (games / "arkanoid.nes").string(), setup, error))
		{
			ps5libretro::CheevosMemory(7);
			Check(ps5libretro::PortDevice(1) == arkanoid && ps5libretro::PortDevice(0) == RETRO_DEVICE_JOYPAD,
				"NES: an Arkanoid game's header plugs the paddle into the second port, and Mesen says so");
			uint8_t right = 0, left = 0, last = 0, fired = 0;
			knob(10, false, right, last);
			knob(-10, true, left, fired);
			char what[200];
			std::snprintf(what, sizeof(what), "NES: the paddle's knob turned all the way right then left by the mouse's movement (%02x, %02x read, "
				"0b and ab wanted), fired by its button (%02x, %02x)", right, left, last, fired);
			Check(right == 0x0b && left == 0xab && !(last & 0x08) && (fired & 0x08), what);
			ps5libretro::Unload();
		}
		else
			Check(false, "NES: the Arkanoid test program loads (" + error + ")");
		if (ps5libretro::Load(*mesen, (games / "zapper.nes").string(), setup, error))
		{
			Check(ps5libretro::PortDevice(1) == zapper, "NES: a Zapper game's header plugs the Zapper into the second port");
			ps5libretro::Unload();
		}
		if (ps5libretro::Load(*mesen, (games / "test.nes").string(), setup, error))
		{
			const bool pad = ps5libretro::PortDevice(1) == RETRO_DEVICE_JOYPAD;
			ps5libretro::SetDevice(1, arkanoid);
			for (int i = 0; i < 2; i++)
				ps5libretro::Run(pads);
			Check(pad && ps5libretro::PortDevice(1) == arkanoid, "NES: a game with no such header has a pad, and the paddle when given it");
			ps5libretro::SetDevice(1, RETRO_DEVICE_JOYPAD);
			Check(ps5libretro::PortDevice(1) == RETRO_DEVICE_JOYPAD, "NES: and Auto again");
			ps5libretro::Unload();
		}
	// cheats (app/cheats.h): every core that takes them has them linked; a code given through the frontend changes
	// what the game sees (Mesen's and Mesen-S's as the CPU reads the byte, which the program copies, and no more once
	// taken back; mGBA's GameShark code written to RAM each frame)
	struct CheatCase
	{
		const char* core;
		const char* game;
		const char* system;
		unsigned cheevos; // the memory read as RetroAchievements maps it; 0: the core's system RAM
		const char* code;
		uint32_t address; // where the change shows, 0xffffffff: only given (the N64's test image draws no frame)
		uint8_t value;
	};
	const CheatCase cheatCases[] = {
		{"mesen", "cheat.nes", "NES", 7, "0300:AB", 0x0301, 0xAB},
		{"mesens", "cheat.sfc", "SNES", 3, "7E0100AB", 0x0101, 0xAB},
		{"mgba", "test.gb", "Game Boy", 0, "01AB01C1", 0x0101, 0xAB}, // $C101: its work RAM's byte 0x101
		{"mgba", "test.gba", "Game Boy Advance", 5, "32000100 000000AB", 0xffffffff, 0},
		{"paralleln64", "test.z64", "Nintendo 64", 2, "8033B21E 0008", 0xffffffff, 0},
	};
	for (const CheatCase& c : cheatCases)
	{
		const std::string name = std::string(c.system) + " cheats (" + c.core + ")";
		const ps5libretro::Core* core = ps5libretro::Find(c.core);
		if (!core)
			continue;
		Check(core->cheatReset && core->cheatSet, name + ": retro_cheat_reset and retro_cheat_set linked");
		ps5libretro::Setup setup;
		const fs::path folder = work / "cheats" / c.core / c.game;
		fs::create_directories(folder);
		setup.system = setup.saves = setup.states = folder.string();
		if (std::string(c.core) == "paralleln64")
			setup.options = {{"parallel-n64-cpucore", "cached_interpreter"}, {"parallel-n64-gfxplugin", "angrylion"}};
		std::string error;
		if (!ps5libretro::Load(*core, (games / c.game).string(), setup, error))
		{
			Check(false, name + ": the game loads (" + error + ")");
			continue;
		}
		Check(ps5libretro::TakesCheats(), name + ": the frontend gives it cheats");
		ps5libretro::Pad pads[ps5libretro::kPlayers] = {};
		const bool frames = c.address != 0xffffffff;
		if (frames)
		{
			if (c.cheevos)
				ps5libretro::CheevosMemory(c.cheevos);
			for (int i = 0; i < 10; i++)
				ps5libretro::Run(pads);
		}
		uint8_t before = 0, with = 0, after = 0;
		const auto read = [&](uint8_t& byte) {
			if (c.cheevos)
				ps5libretro::CheevosRead(c.address, &byte, 1);
			else if (const auto* ram = static_cast<const uint8_t*>(core->getMemoryData(RETRO_MEMORY_SYSTEM_RAM));
				ram && c.address < core->getMemorySize(RETRO_MEMORY_SYSTEM_RAM))
				byte = ram[c.address];
		};
		if (frames)
			read(before);
		ps5libretro::SetCheats({c.code});
		if (frames)
		{
			for (int i = 0; i < 10; i++)
				ps5libretro::Run(pads);
			read(with);
			ps5libretro::SetCheats({});
			for (int i = 0; i < 10; i++)
				ps5libretro::Run(pads);
			read(after);
			char what[200];
			std::snprintf(what, sizeof(what), "%s: %s seen by the game (%02x before, %02x with it, %02x after it is taken back)", name.c_str(), c.code, before, with, after);
			// mGBA's code writes RAM, which keeps the value once it is taken back; Mesen's change only what is read
			const bool written = std::string(c.core) == "mgba";
			Check(before != c.value && with == c.value && (written || after != c.value), what);
		}
		else
			Check(true, name + ": " + c.code + " given and taken back");
		ps5libretro::SetCheats({});
		ps5libretro::Unload();
	}
	std::printf("%s\n", failures ? "SOME CORES FAILED" : "EVERY CORE PASSED");
	return failures ? 1 : 0;
}
