// SPDX-License-Identifier: MPL-2.0
// PS5CEMU-HAR: Cemu's emulated USB devices, the toys-to-life portals: the Skylanders Portal of Power,
// the Disney Infinity Base and the LEGO Dimensions Toypad, as Cemu's Emulated USB Devices window has
// them. A device switched on is plugged in when a game starts (Cemu's settings.xml keeps the switch);
// figures, dumps in /data/ps5cemu/figures/<skylanders|infinity|dimensions>, are put on and taken off
// while the game runs, from the in-game menu, as amiibo are scanned there. A physical portal on the
// PS5's USB is not reached: the console gives a homebrew app no USB passthrough.

#pragma once

#include <string>
#include <vector>

namespace ps5usb
{
	enum class Device
	{
		Skylanders,
		Infinity,
		Dimensions,
	};
	constexpr Device kDevices[] = {Device::Skylanders, Device::Infinity, Device::Dimensions};

	const char* Name(Device device); // "Skylanders Portal of Power"
	std::string Folder(Device device); // where its figures are
	// Cemu's switch for it (settings.xml), and whether it was on as the running game started (the
	// one the game has; a switch changed since takes effect at the next start)
	bool Enabled(Device device);
	void SetEnabled(Device device, bool on);
	bool Plugged(Device device);

	// The figure files in its folder, sorted (read again by Refresh)
	std::vector<std::string> Figures(Device device);
	// Its slots, as the in-game menu lists them, and the figure on each ("" for none)
	struct Slot
	{
		std::string label;
		std::string figure;
	};
	std::vector<Slot> Slots(Device device);

	// The in-game menu's: a figure put on a slot ("" takes it off), made on the game's main thread
	void RequestFigure(Device device, size_t slot, const std::string& figure);
	// Reads the figure folders again (the menu, as it opens)
	void Refresh();
	// RunGame's: what the game was started with, then the requests made
	void GameStarted();
	void ServiceRequests();
	// The last thing that went wrong, for the menu ("" for none)
	std::string LastError();
}
