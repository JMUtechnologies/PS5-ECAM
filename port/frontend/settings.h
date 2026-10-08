// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the launcher's settings, /data/ps5cemu/ps5cemu.json. Cemu's own settings stay in its
// settings.xml; the launcher writes the few it manages (game folder, volume, overlay, upscaling
// filter) into both. Azahar's are here (n3ds), and given to it when a 3DS game starts.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ps5settings
{
	// Azahar's side
	struct N3ds
	{
		std::string gamesFolder = "/data/ps5cemu/azahar/games";
		int resolution = 6;			  // the internal resolution, times the 3DS's 400x240
		int layout = 2;				  // the screens' layout (ps5azahar::Layout): the top one large
		int textureFilter = 0;		  // none
		int volume = 100;			  // percent
		bool motion = true;			  // the DualSense's motion sensors as the 3DS's
		int deadzone = 15;			  // the sticks', percent
		bool performance = false;	  // the frame rate and speed over the game (its in-game menu)
		int region = -1;			  // the emulated 3DS's region: -1 automatic (the game's), else Azahar's region_value
		int language = -1;			  // the emulated 3DS's language: -1 as Azahar sets it, else its SystemLanguage (0 to 11)
		bool customTextures = false;  // texture packs from azahar/load/textures/<title ID> (Azahar's custom_textures)
		bool asyncShaders = true;	  // Azahar's async_shader_compilation (Settings > Video), as the Wii U side's
		int border = 0;				  // artwork around the screens: 0 none, else ps5ingame3ds::kBorderNames' theme
		int cpuClock = 100;			  // the emulated CPU's speed, percent of the 3DS's (Azahar's cpu_clock_percentage)
		// only in ps5cemu.json: Azahar's statistics and frame limiter on the TSC (Azahar patch 0013),
		// which time every SVC; false: the steady clock, a call into the kernel each read
		bool tscClock = true;
		// only in ps5cemu.json: every 3DS vertex position computed invariantly (Azahar patch 0014), as
		// Azahar does on Apple GPUs, for Pokemon X's moving black stipples (#23). Off, as on a PC,
		// until a console has shown it fixes them, and what it costs
		bool invariantPosition = false;
		std::string articAddress;	  // the last Artic Base server's IPv4 address, a 3DS on the network
		int gameCount = -1;			  // the library's games when last looked for (the start screen's); -1: never
		std::map<std::string, std::string> buttons; // 3DS button: DualSense input, where not the default
		uint64_t lastGame = 0;		  // title ID, or one made from the path
		std::vector<uint64_t> recent; // newest first, at most four
	};

	struct Launcher
	{
		std::string gamesFolder = "/data/ps5cemu/games";
		int upscaleFilter = 1;		  // how the game's picture is scaled to 4K: Cemu's upscale_filter
		bool highFrameRate = false;	  // the 119.88 Hz mode where the display has it
		bool overlay = false;		  // Cemu's performance overlay from the start
		bool asyncShaders = true;	  // Cemu's async_compile (Settings > Video, and the in-game menu's Graphics)
		bool gamePadSpeaker = false;  // the GamePad's sound on player 1's DualSense speaker (Settings > Audio)
		bool rumble = true;
		bool pinCpuThreads = false;	  // an experiment (ps5/threads.h): only in ps5cemu.json
		// only in ps5cemu.json: Cemu's pipeline compile threads but the first below normal priority, as
		// Cemu runs them on Windows (ps5/threads.h). An experiment, off: the console schedules by strict
		// priority, where Windows lifts a starving thread now and then, so a compile thread kept off the
		// CPUs while it holds a lock others need (the heap's arenas) could hold them up for as long
		// (ProsperoEden hung for good that way, in its allocator)
		bool compileBelowNormal = false;
		// only in ps5cemu.json: how long, in microseconds, an idle emulated CPU core spins for a thread
		// to run before it sleeps (Cemu patch 0026); any but 0 also has the GPU thread wait for
		// commands without yielding (Cemu patch 0027). Games hand work between their cores many times a
		// frame, and a sleeping core then waits on the console's thread wake-up; ProsperoEden spins
		// 100 us for the same reason (a racing game's heavy phase from ~55 to ~59.6 fps). An
		// experiment: 0, Cemu's own (sleep at once), until a console has measured it
		int idleSpinUs = 0;
		// only in ps5cemu.json: the emulated CPU cores and Cemu's GPU thread on physical cores of their
		// own, every other thread kept off them, as ProsperoEden places its threads (ps5/threads.h).
		// An experiment, off until a console has measured it
		bool isolateCores = false;
		// only in ps5cemu.json: Cemu's samplers with standard border colours only (Cemu patch 0028), for
		// a game that stops the console's GPU: ProsperoEden saw custom ones fault it in some games.
		// Off, as on a PC
		bool standardBorderColors = false;
		// only in ps5cemu.json: RADV's shader disk cache, on as on a PC. Mesa turns it off in a process
		// whose effective user is not its real one (the HEN's root), so the real user and group are
		// made the effective ones and the driver keeps the shaders it compiles for the next start.
		// false: every start compiles them again (as before 3.0.1)
		bool shaderDiskCache = true;
		// only in ps5cemu.json: Cemu's exact stfs conversion (Cemu patch 0021), as upstream Cemu now
		// stores on a PC; off, as every Cemu before it and every PS5CEMU-HAR release so far stored (with
		// cvtsd2ss). Which costs more on the console has not been measured yet
		bool accurateSingleStores = false;
		// only in ps5cemu.json: Cemu's millisecond clock, which its GPU thread takes at every draw call,
		// on the TSC instead of clock_gettime, a call into the kernel (890 ns a call, measured): it gave
		// Breath of the Wild's GPU thread back 3.3 ms of each 16.7 ms frame (A/B on a console,
		// 2026-10-03, at 4,300 draws a frame); false: clock_gettime
		bool tscTickCount = true;
		// only in ps5cemu.json: the Vulkan driver reads the time from the TSC (ps5/driver_clock.h)
		// rather than with clock_gettime, a 20 us system call on the console. An experiment: off
		// until a console has measured it against clock_gettime
		bool driverClockTsc = false;
		// only in ps5cemu.json: the sampling profiler (ps5/profiler.h), into logs/profile.bin
		bool profile = false;
		// only in ps5cemu.json: RADV_DEBUG for the Vulkan driver (e.g. "nongg,nohiz"), to narrow down
		// a GPU hang by turning hardware features off; empty by default
		std::string radvDebug;
		int volume = 100;			  // the TV sound, in percent
		int gameCount = -1;			  // the Wii U library's games when last looked for (the start screen's); -1: never
		uint64_t lastGame = 0;		  // title ID
		std::vector<uint64_t> recent; // newest first, at most four
		N3ds n3ds;
		// The launcher's own sound, on both sides (frontend/sound.h): its music ("shop", "setup" or
		// "off") and the music's volume in percent, and the menu's sounds.
		std::string music = "shop";
		int musicVolume = 50;
		bool boxArt = true;			  // box art from GameTDB (Settings > Online and updates)
		bool menuSounds = true;
		// The emulator the launcher opens on, the one last played ("wiiu" or "3ds"); empty: the
		// start screen, to choose.
		std::string side;
		// Why the last game did not start, when that needed a fresh process to show (the launcher
		// shows it once, then clears it).
		std::string launchError;
	};

	Launcher Load();
	bool Save(const Launcher& settings);
	// The game played last and first among the recent ones (at most four).
	void AddRecent(uint64_t& lastGame, std::vector<uint64_t>& recent, uint64_t titleId);
	inline void AddRecent(Launcher& settings, uint64_t titleId) { AddRecent(settings.lastGame, settings.recent, titleId); }
}
