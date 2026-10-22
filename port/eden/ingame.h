// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the Switch side's loading screen and in-game menu, drawn by Eden's program (eden/eboot.bin,
// eden.h) over its own frames, so a Switch game starts and pauses as the other sides' do, never in
// ProsperoEden's screens. The menu is the Wii U's and the 3DS's (side_menu.h, menu_canvas.h) in the
// Switch side's red, with Dear ImGui and its Vulkan backend recording into Eden's present pass, as the
// 3DS's menu records into Azahar's (ingame3ds.h). This file and ingame.cpp are compiled into Eden's
// program, not the app's (patches/eden 0007: tools/build-eden.sh hands it PS5ECAM_PORT), with the app's
// lang.cpp, keyboard.cpp and text.cpp's shaping (PS5ECAM_TEXT_NO_FONTS); the launcher only writes the
// session file (ps5eden::WriteSession).
//
// The loading screen: the game's cover, its name, GameTDB's publisher and year and what Eden is doing,
// on the cover's colours blurred, in place of ProsperoEden's. Touchpad click + Options opens the menu,
// as on the other sides. The game runs on behind it with its controller still, as Cemu's does behind
// the Wii U's menu: Eden's port keeps the emulation from being paused (its
// tools/check-gpu-producer-stop.py). Without a session file, or when ImGui cannot start on Eden's
// device, Eden's own loading screen shows and the menu does not open: the game still plays.

#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include "../app/paths.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps5ingamenx
{
	// What the launcher leaves for Eden's program as it hands over (ps5eden::WriteSession): a line
	// each, title=, details=, cover=, language=, font=, face=, exit=, game= (the title ID the launcher
	// keeps the game's settings under, in hex)
	inline std::string SessionFile()
	{
		return std::string(ps5paths::kDefaultRoot) + "/eden/ecam-session.txt";
	}

	// What Eden's program leaves for the launcher (ps5eden::TakeResult), a line each: state= (running
	// from the game's start, leaving once the player quits, ended or failed as the program returns: a
	// session still running when the app starts again stopped by itself, a crash), error=, game=, and
	// changed=1 with the settings the menu left (Settings' names) when it changed any
	inline std::string ResultFile()
	{
		return std::string(ps5paths::kDefaultRoot) + "/eden/ecam-result.txt";
	}

	// What the menu shows and changes, as Eden names them
	struct Settings
	{
		int filter = 0;				  // how the picture is scaled to the TV: bilinear, FSR 1, bicubic, nearest
		int antiAliasing = 0;		  // Eden's: none, FXAA, SMAA
		int sharpening = 25;		  // FSR 1's, Eden's slider: 0 (the sharpest) to 200
		bool frameGeneration = false; // MihawkGen's picture between each two frames
		bool canGenerate = false;	  // whether this session can (MihawkGen built in, its weights there)
		bool overlay = false;		  // Eden's performance overlay
		int speedLimit = 0;			  // 100%, 150%, 200%, 300%, none (Eden's kSpeedLimitPercent's order)
		int volume = 100;			  // percent
		bool docked = true;			  // the emulated Switch in its dock, else handheld
		int controller = -1;		  // -1 automatic (a Pro Controller), else Pro, handheld, Joy-Con pair, left, right
		bool vibration = true;
		int deadzone = 8;			  // the sticks', percent
	};

	// The room the session joined before the game (Eden's multiplayer.h), as the menu shows it
	struct Room
	{
		bool joined = false;
		bool lost = false; // its connection went: Eden ends the game
		std::string name;
		std::vector<std::string> members;
	};
	void SetRoom(const Room& room);

	// The amiibo dump the menu touches to the controller (empty: the one there taken away), and what
	// came of it, said in the menu
	bool TakeAmiiboRequest(std::string& path);
	enum class AmiiboAnswer
	{
		Touched,
		Removed,
		NotAsked, // the game is not looking for an amiibo
		NotAnAmiibo,
		Unreadable,
	};
	void SetAmiiboAnswer(AmiiboAnswer answer);

	// The result file: running at Start, then the state Eden's program is in, and why it failed
	void WriteResult(const char* state, const std::string& error = {});

	// Eden's frame being recorded: before its present pass begins, then in it
	struct Target
	{
		PFN_vkGetInstanceProcAddr getInstanceProcAddr;
		VkInstance instance;
		VkPhysicalDevice physicalDevice;
		VkDevice device;
		uint32_t queueFamily;
		VkRenderPass renderPass;
		uint32_t imageCount;
		VkCommandBuffer commandBuffer;
		uint32_t width, height;
		bool insideRenderPass;
	};

	// As Eden starts the game, before its loading screen: the session file read (and taken away), and
	// the settings the game starts with
	void Start(const Settings& settings);

	// What Eden is doing before the game's first frame (its Eden::Loading::Step), and how far into the
	// shaders it is; EndLoading once the game draws
	enum class Step
	{
		None,
		Game,
		Graphics,
		Shaders,
		Starting,
	};
	void SetLoading(Step step);
	void SetShaders(size_t done, size_t of);
	void EndLoading();
	// Whether this loading screen is drawn: Eden's own then is not
	bool DrawsLoading();

	// Player 1's controller, each time Eden reads it (its scePad layout is the app's, ps5/pad.h). True
	// while the menu is up: the game then gets a still controller.
	bool Controller(uint32_t buttons, uint8_t leftX, uint8_t leftY);
	// Touchpad click + Options
	void ToggleMenu();
	bool MenuOpen();
	// For Eden's game loop: a change made in the menu since the last call, and the library asked for
	bool TakeChanges(Settings& settings);
	bool TakeLeaveRequest();

	// For Eden's renderer, twice a frame, where it records its present pass
	void Record(const Target& target);
}
