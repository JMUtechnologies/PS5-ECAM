// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM's UI kit: the DualSense as actions (input.h).

#include "input.h"
#include "../ps5/pad.h"

#include <algorithm>
#include <cstdlib>

namespace ui
{
	namespace
	{
		constexpr double kRepeatDelay = 0.4, kRepeatRate = 0.09;
		// the left stick as a direction: pushed past 0.55 of the way, let go below 0.35, so a thumb resting
		// near the edge does not flicker in and out of it (each flicker was a step of its own); and only
		// its stronger axis, so a push a little off straight does not move both ways at once
		constexpr int kStickOn = 70, kStickOff = 45; // of 128, from the middle

		struct Mapping
		{
			uint32_t mask;
			Button button;
			bool repeats;
		};
		constexpr Mapping kMap[] = {
			{ps5pad::kUp, Button::Up, true},
			{ps5pad::kDown, Button::Down, true},
			{ps5pad::kLeft, Button::Left, true},
			{ps5pad::kRight, Button::Right, true},
			{ps5pad::kCross, Button::Cross, false},
			{ps5pad::kCircle, Button::Circle, false},
			{ps5pad::kSquare, Button::Square, false},
			{ps5pad::kTriangle, Button::Triangle, false},
			{ps5pad::kL1, Button::L1, true},
			{ps5pad::kR1, Button::R1, true},
			{ps5pad::kL2, Button::L2, true},
			{ps5pad::kR2, Button::R2, true},
			{ps5pad::kOptions, Button::Options, false},
			{ps5pad::kTouchPad, Button::Touchpad, false},
			{ps5pad::kR3, Button::R3, false},
		};
	}

	uint32_t Input::Stick(int player, int x, int y)
	{
		uint32_t& held = m_stick[player];
		const int ax = std::abs(x), ay = std::abs(y);
		const bool vertical = held & (ps5pad::kUp | ps5pad::kDown), horizontal = held & (ps5pad::kLeft | ps5pad::kRight);
		uint32_t now = 0;
		// a direction held stays until the stick comes back past the lower mark; a new one needs the higher
		if (vertical && ay >= kStickOff && ay >= ax / 2)
			now = y < 0 ? ps5pad::kUp : ps5pad::kDown;
		else if (horizontal && ax >= kStickOff && ax >= ay / 2)
			now = x < 0 ? ps5pad::kLeft : ps5pad::kRight;
		else if (std::max(ax, ay) >= kStickOn)
			now = ay > ax ? (y < 0 ? ps5pad::kUp : ps5pad::kDown) : (x < 0 ? ps5pad::kLeft : ps5pad::kRight);
		held = now;
		return now;
	}

	Actions Input::Poll(double now)
	{
		uint32_t buttons = 0;
		bool intercepted = false;
		for (int player = 0; player < ps5pad::kMaxPlayers; player++)
		{
			ps5pad::Data data;
			if (!ps5pad::Read(player, data))
				continue;
			if (data.buttons & ps5pad::kIntercepted)
			{
				intercepted = true;
				continue;
			}
			buttons |= data.buttons;
			buttons |= Stick(player, data.leftX - 128, data.leftY - 128);
		}
		Actions actions;
		if (intercepted)
		{
			// the system's menu has the controller: what is held as it gives it back counts once let go
			Reset();
			m_down.fill(false);
			return actions;
		}
		for (const Mapping& mapping : kMap)
		{
			const size_t slot = (size_t)mapping.button;
			const bool down = buttons & mapping.mask;
			if (!down)
			{
				if (m_down[slot] && m_armed[slot])
					actions.released.push_back(mapping.button);
				m_down[slot] = false;
				m_armed[slot] = true;
				continue;
			}
			if (!m_armed[slot])
				continue; // held from before: not until it is let go
			if (!m_down[slot])
			{
				actions.presses.push_back({mapping.button, false});
				m_since[slot] = now;
				m_repeatAt[slot] = now + kRepeatDelay;
			}
			else if (mapping.repeats && now >= m_repeatAt[slot])
			{
				actions.presses.push_back({mapping.button, true});
				// on a steady beat from the first press, not from the frame that saw it, so the steps come
				// evenly; a late frame catches up with one step, not a burst
				m_repeatAt[slot] = std::max(m_repeatAt[slot] + kRepeatRate, now + kRepeatRate * 0.5);
			}
			m_down[slot] = true;
			actions.held |= 1u << slot;
			actions.heldSeconds[slot] = (float)(now - m_since[slot]);
		}
		return actions;
	}
}
