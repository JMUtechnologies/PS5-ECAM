// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the controllers other than the pad that some games on the libretro sides were made for, and
// which one a game gets (the in-game Buttons page's Controller row): Arkanoid's paddle and the Zapper on
// the NES, the Power Pad, the SNES Mouse (Mario Paint) and the Super Scope. The DualSense stands in for
// each as game.cpp reads it:
//
//   Paddle     the touchpad, a finger across it as the knob turned; the left stick and the D-pad turn it
//   LightGun   aimed with a finger on the touchpad, by turning the DualSense (its gyroscope), or with the
//              right stick; a crosshair shows where
//   Mouse      a finger moved on the touchpad, or the left stick
//   Mat        the mat's buttons are player 1's buttons
//
// Auto is the core's own choice where it has one (Mesen's game database, or an NES 2.0 header: the port's
// patches/libretro/Mesen 0002 tells which), else the game's from what this file knows of it (the SNES has
// no database: its mouse and Super Scope games, by the title in the cartridge's header or the file's name).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ps5libretro
{
	enum class Device
	{
		Pad,	  // the console's own
		Paddle,	  // the Arkanoid controller's knob
		LightGun, // the Zapper, the Super Scope: aimed at the picture
		Mouse,	  // the SNES Mouse
		Mat,	  // the Power Pad, the Family Trainer, the Famicom's other add-ons of buttons
	};

	// A choice of the Controller row: its name (English, as Tr has it) and what the core is given, port by
	// port (port, libretro device). The first is Auto, the second the pad.
	struct Choice
	{
		const char* name;
		std::vector<std::pair<unsigned, unsigned>> ports;
	};
	// The core's choices; none for a core with no controller but its pad
	const std::vector<Choice>& Choices(std::string_view core);
	// What a choice gives the core for this game (Auto: the core's own choice, or the app's, above)
	std::vector<std::pair<unsigned, unsigned>> Ports(std::string_view core, int choice, const std::string& path);

	// The controller a core has in a port (session.h's PortDevice), as the DualSense stands in for it, and its
	// name (English; empty for the pad)
	Device DeviceOf(std::string_view core, unsigned device);
	const char* DeviceName(std::string_view core, unsigned device);

	// A SNES cartridge's title, from its header (LoROM's or HiROM's, a copier's 512 bytes before it or not);
	// empty when neither looks like one
	std::string SnesTitle(const std::vector<uint8_t>& rom);
	// What the app knows a SNES game needs, by that title or its file's name: Mouse, LightGun, else Pad
	Device SnesDevice(const std::string& title, const std::string& fileName);
}
