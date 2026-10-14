// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: what melonDS's PS5 frontend shares between its files (built in melonDS's tree:
// CMakeLists.txt): the game's loop and the emulation (core.cpp), melonDS's platform (platform.cpp), the
// DualSense as the DS (input.cpp) and AudioOut (sound.cpp).

#pragma once

#include "../frontend/settings.h"

#include "types.h"

#include <cstdint>
#include <string>

namespace melonDS
{
	class NDS;
}

namespace ps5melonds
{
	// A DS's frame rate, 59.83 a second: its 33.51 MHz clock over a frame's 263 lines of 355 dots, 6
	// cycles a dot. (Twice as many of the ARM9's 67 MHz cycles, which melonDS counts: those over this
	// clock made it 29.91, the sound twice as fast and the speed shown twice the DS's, #47.)
	constexpr double kDsFrameRate = 33513982.0 / (6 * 263 * 355);

	// melonDS stopped the console itself (Platform::SignalStop): the game shut the DS down, or tried
	// to start the GBA mode it has not
	void Stopped(int reason);

	// The game's save, as melonDS changes it (Platform::WriteNDSSave): written to its file by the game's
	// loop once the game has stopped writing for a moment, and when the game ends (core.cpp)
	void SaveChanged(const melonDS::u8* data, melonDS::u32 length);

	namespace input
	{
		// The DS side's controls: its buttons on the DS's, the circle pad's on the D-pad
		void Configure(const ps5settings::N3ds& settings);
		// Takes the controller's latest sample, a few hundred times a second (the game's loop). While
		// blocked (the in-game menu is up) the game sees the controller let go.
		void Update(bool blocked, bool bottomShown);
		// For the emulation, each frame: the DS's keys (melonDS's mask: a bit clear for a key held), the
		// touch screen (true while touched, at x, y in its 256 x 192), and the microphone (R3 held)
		melonDS::u32 KeyMask();
		bool Touch(int& x, int& y);
		bool Blowing();
		// The cursor over the bottom screen, from 0 to 1 across it; true while a finger is on the touchpad
		bool Cursor(float& x, float& y);
	}

	namespace sound
	{
		// AudioOut's port and the thread that feeds it melonDS's 48 kHz output
		void Start(melonDS::NDS& nds);
		void Stop();
		void SetVolume(int percent);
		// Once a frame, from the emulation: melonDS's sound made at the rate the screens are shown, its
		// buffer kept half full (speed: the emulation's, in percent of the DS's; 0 none)
		void Pace(melonDS::NDS& nds, double refreshHz, int speed);
	}
}
