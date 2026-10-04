// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: what the port shows and decides over a running game (ingame.cpp):
//   - its menu (touchpad + Options), a panel down the left (side_menu.h) with the game's box art,
//     name, publisher and year at its top: the screens, the picture and Cemu's graphics settings,
//     the volume, amiibo, the controllers, back to the library;
//   - the GamePad's screen, the TV's unless the player makes it the main one or puts it in a corner;
//   - the touchpad's cursor on the GamePad's screen, where a click touches it;
//   - the DualSense in Cemu's own overlays (ImGui), such as the software keyboard games type with.
// Cemu's renderer calls in through the PS5Cemu_* functions its patch declares (patches/cemu).

#pragma once

#include "emulator.h"

namespace ps5ingame
{
	// The game starting, for the menu's top: its name, box art (or icon) and GameTDB's facts.
	void SetGame(const ps5emu::Game& game);

	// Player 1's touchpad (the GamePad's): where the finger is, normalised to the GamePad's
	// screen, and whether the game is being touched there.
	void SetGamePadPointer(bool finger, float x, float y, bool touching);

	// Whether the menu or Cemu's keyboard is up: the game then gets no input.
	bool OverlayTakesInput();

	// The shortcuts (touchpad + Options, L1, R1).
	void ToggleMenu();
	void SwapScreens();
	void ToggleCornerScreen();

	// What the menu asks for, once each: its settings saved, the library.
	bool TakeSaveRequest();
	bool TakeLibraryRequest();
}
