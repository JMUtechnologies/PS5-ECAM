// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the in-game menus' Buttons category (side_menu.h): which DualSense button presses each of the
// game's buttons, changed without leaving the game, as Customize Buttons does in Porpoise's menu on the
// GameCube and Wii sides (Phononenom's sheet) and Settings > Controls > Buttons does in the launcher. A row
// a button, its DualSense input beside it. Cross on one, then the input for it: the button that had that
// input takes this one's, so the two swap and none is pressed twice by mistake. Nothing pressed in five
// seconds, and it stays as it was. The first row is the controller's test: what the DualSense sends now. The
// last one puts every button back, held.
//
// The side keeps its mappings (ps5emu's, ps5azahar's, Eden's): this is the rows, the wait for a press, and
// what the player asked for. While it waits, and until every button is let go after, the sheet gets a still
// controller (Busy).

#pragma once

#include "lang.h"
#include "side_menu.h"

#include "../ps5/pad.h"

#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace ps5menu
{
	// A DualSense input, numbered as ps5emu::PadInput is (emulator.h): the sticks and triggers one direction each
	constexpr int kInputNone = 0, kInputCross = 1, kInputCircle = 2, kInputSquare = 3, kInputTriangle = 4, kInputL1 = 5, kInputR1 = 6,
		kInputL2 = 7, kInputR2 = 8, kInputL3 = 9, kInputR3 = 10, kInputCreate = 11, kInputOptions = 12, kInputUp = 13, kInputDown = 14,
		kInputLeft = 15, kInputRight = 16, kInputLeftStickUp = 17, kInputRightStickUp = 21, kInputTouchPad = 25;

	// Their names, as the sides' lists have them (azahar/controls.cpp's kInputNames)
	constexpr const char* kInputNames[] = {"", "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Create", "Options",
		"D-pad up", "D-pad down", "D-pad left", "D-pad right", "Left stick up", "Left stick down", "Left stick left", "Left stick right",
		"Right stick up", "Right stick down", "Right stick left", "Right stick right", "Touchpad click"};

	// The DualSense button held now, or none: the launcher's (frontend/actions.cpp's Pressed), for the menus
	// that are not the launcher's. The touchpad's click is not one here: the menus read it apart.
	inline int HeldInput(uint32_t buttons)
	{
		static constexpr std::pair<uint32_t, int> kButtons[] = {
			{ps5pad::kCross, kInputCross}, {ps5pad::kCircle, kInputCircle}, {ps5pad::kSquare, kInputSquare}, {ps5pad::kTriangle, kInputTriangle},
			{ps5pad::kL1, kInputL1}, {ps5pad::kR1, kInputR1}, {ps5pad::kL3, kInputL3}, {ps5pad::kR3, kInputR3}, {ps5pad::kCreate, kInputCreate},
			{ps5pad::kOptions, kInputOptions}, {ps5pad::kUp, kInputUp}, {ps5pad::kDown, kInputDown}, {ps5pad::kLeft, kInputLeft},
			{ps5pad::kRight, kInputRight}, {ps5pad::kL2, kInputL2}, {ps5pad::kR2, kInputR2},
		};
		for (const auto& [mask, input] : kButtons)
			if (buttons & mask)
				return input;
		return kInputNone;
	}

	// With the triggers and the sticks, past halfway
	inline int HeldInput(const ps5pad::Data& data)
	{
		if (const int button = HeldInput(data.buttons & ~(ps5pad::kL2 | ps5pad::kR2)))
			return button;
		if (data.l2 > 160 || (data.buttons & ps5pad::kL2))
			return kInputL2;
		if (data.r2 > 160 || (data.buttons & ps5pad::kR2))
			return kInputR2;
		if (data.leftY < 40) return kInputLeftStickUp;
		if (data.leftY > 215) return kInputLeftStickUp + 1;
		if (data.leftX < 40) return kInputLeftStickUp + 2;
		if (data.leftX > 215) return kInputLeftStickUp + 3;
		if (data.rightY < 40) return kInputRightStickUp;
		if (data.rightY > 215) return kInputRightStickUp + 1;
		if (data.rightX < 40) return kInputRightStickUp + 2;
		if (data.rightX > 215) return kInputRightStickUp + 3;
		return kInputNone;
	}

	// Nothing held at all, the triggers and the touchpad's click too: the wait for a press starts from here
	inline bool PadIdle(const ps5pad::Data& data)
	{
		return HeldInput(data) == kInputNone && !(data.buttons & ps5pad::kTouchPad) && data.l2 < 60 && data.r2 < 60;
	}

	// A button's or an input's name in the menu's language: frontend/actions.cpp's ButtonName, its strings
	// marked here too so that the in-game fonts have their characters (tools/lang.py's menu files).
	inline std::string ButtonLabel(const std::string& english)
	{
		using ps5lang::TrMarkC;
		static constexpr const char* kNames[] = {TrMarkC("button", "Cross"), TrMarkC("button", "Circle"), TrMarkC("button", "Square"),
			TrMarkC("button", "Triangle"), TrMarkC("button", "Create"), TrMarkC("button", "Options"), TrMarkC("button", "Touchpad"),
			TrMarkC("button", "Touchpad click"),
			TrMarkC("button", "D-pad up"), TrMarkC("button", "D-pad down"), TrMarkC("button", "D-pad left"), TrMarkC("button", "D-pad right"),
			TrMarkC("button", "Left stick up"), TrMarkC("button", "Left stick down"), TrMarkC("button", "Left stick left"),
			TrMarkC("button", "Left stick right"), TrMarkC("button", "Left stick click"), TrMarkC("button", "Right stick up"),
			TrMarkC("button", "Right stick down"), TrMarkC("button", "Right stick left"), TrMarkC("button", "Right stick right"),
			TrMarkC("button", "Right stick click"), TrMarkC("button", "Circle pad up"), TrMarkC("button", "Circle pad down"),
			TrMarkC("button", "Circle pad left"), TrMarkC("button", "Circle pad right"), TrMarkC("button", "C-stick up"),
			TrMarkC("button", "C-stick down"), TrMarkC("button", "C-stick left"), TrMarkC("button", "C-stick right"),
			TrMarkC("button", "Nunchuk stick up"), TrMarkC("button", "Nunchuk stick down"), TrMarkC("button", "Nunchuk stick left"),
			TrMarkC("button", "Nunchuk stick right"), TrMarkC("button", "Nunchuk C"), TrMarkC("button", "Nunchuk Z"), TrMarkC("button", "Home"),
			TrMarkC("button", "Start"), TrMarkC("button", "Select"), TrMarkC("button", "+ (Start)"), TrMarkC("button", "- (Select)"),
			TrMarkC("button", "Blow into the mic"), TrMarkC("button", "Show the GamePad's screen"), TrMarkC("button", "None"),
			TrMarkC("button", "Touch at the cursor"), TrMarkC("button", "Fast forward"), TrMarkC("button", "Fast forward, on and off"),
			TrMarkC("button", "Rewind")};
		(void)kNames;
		return english.size() <= 3 ? english : ps5lang::TrC("button", english);
	}

	// One of the game's buttons and the DualSense input that presses it, both in English as the side lists
	// them (ps5emu::ButtonMapping); no input: not set
	struct ButtonRow
	{
		std::string button, input;
	};

	class ButtonsMenu
	{
	public:
		// What the player asked for this frame: nothing, a button (the list's index) given an input, or every
		// button back as the console has it
		struct Asked
		{
			int index = -1;
			int input = kInputNone;
			bool reset = false;
		};

		// The menu opened
		void Reset()
		{
			m_listening = -1;
			m_settling = false;
			m_message.clear();
		}

		// Waiting for a press, or for its button to be let go: the sheet's controller is still meanwhile
		bool Busy() const
		{
			return m_listening >= 0 || m_settling;
		}

		// The category: test (what the DualSense sends now, heldNow from HeldInput), a row a button, then the
		// reset, whose help says what else of the side's it puts back (resetHelp)
		Row Category(const std::vector<ButtonRow>& buttons, int heldNow, uint64_t nowUs, bool touchpadCancels, const std::string& resetHelp) const
		{
			using ps5lang::Tr;
			using ps5lang::TrF;
			using ps5lang::TrP;
			int set = 0;
			for (const ButtonRow& button : buttons)
				set += !button.input.empty();
			Row category{"buttons", Tr("Buttons"), TrP(set, "{0} button set", "{0} buttons set"), false,
				Tr("Choose the DualSense button for each button.")};
			// tr: the row that shows what the controller sends: the button held now, or a dash
			category.rows.push_back({"buttons-test", Tr("Controller test"),
				heldNow > kInputNone && heldNow <= kInputTouchPad ? ButtonLabel(kInputNames[heldNow]) : std::string("-"), false,
				m_message.empty() ? std::string(Tr("This player's DualSense input, live.")) : m_message});
			for (size_t i = 0; i < buttons.size(); i++)
			{
				const bool listening = (int)i == m_listening;
				Row row{"button:" + std::to_string(i), ButtonLabel(buttons[i].button),
					listening ? std::string(Tr("Press a button")) : buttons[i].input.empty() ? std::string(Tr("Not set")) : ButtonLabel(buttons[i].input), false,
					// tr: a row of the in-game menu's Buttons: one of the game's buttons, and the DualSense button that presses it
					Tr("Press Cross, then the DualSense button to use. If another button already uses it, the two swap.")};
				// its place on the controller's picture (side_menu.h's DrawPad)
				for (int input = 1; input < (int)std::size(kInputNames); input++)
					if (buttons[i].input == kInputNames[input])
						row.pad = input;
				if (listening)
				{
					const int seconds = (int)((m_until > nowUs ? m_until - nowUs : 0) / 1000000) + 1;
					// tr: {0} is a button of the console's controller
					row.help = TrF("Press the DualSense button, trigger or stick direction for {0} now.", ButtonLabel(buttons[i].button)) + " " +
						(touchpadCancels ? TrP(seconds, "{0} second left. A touchpad click cancels.", "{0} seconds left. A touchpad click cancels.") :
										   TrP(seconds, "{0} second left, or nothing changes. Touchpad click can be chosen too.",
											   "{0} seconds left, or nothing changes. Touchpad click can be chosen too."));
				}
				else if (!m_message.empty() && (int)i == m_last)
					row.help = m_message;
				category.rows.push_back(std::move(row));
			}
			Row reset{"buttons-reset", Tr("Reset to defaults"), {}, false, resetHelp};
			reset.hold = true;
			category.rows.push_back(std::move(reset));
			return category;
		}

		// The sheet's action: Cross on a button's row starts the wait, the reset held asks for it. True when
		// the action was one of this category's.
		bool Act(const Action& action, uint64_t nowUs, Asked& asked)
		{
			asked = {};
			if (action.id == "buttons-reset")
			{
				if (action.chosen)
				{
					asked.reset = true;
					m_last = -1;
					m_message = ps5lang::Tr("Done.");
				}
				return true;
			}
			if (action.id == "buttons-test")
				return true;
			if (action.id.rfind("button:", 0) != 0)
				return false;
			if (action.chosen)
			{
				m_listening = m_last = std::atoi(action.id.c_str() + 7);
				m_released = false;
				m_until = nowUs + 5000000;
				m_message.clear();
			}
			return true;
		}

		// Each frame while Busy: the input held now (HeldInput), whether the controller is idle (PadIdle), the
		// touchpad's click, and whether the side takes this input for a button (allowed). The wait starts once
		// everything is let go (the Cross that began it), then takes the next press.
		Asked Capture(int held, bool idle, bool touchpad, bool touchpadCancels, bool allowed, uint64_t nowUs)
		{
			using ps5lang::Tr;
			Asked asked;
			if (m_listening < 0)
			{
				if (idle)
					m_settling = false; // the button that was taken is let go: the sheet reads the controller again
				return asked;
			}
			const auto stop = [&](const char* message) {
				m_listening = -1;
				m_settling = !idle;
				m_message = message;
			};
			if (nowUs > m_until)
				stop(Tr("No button pressed. Nothing changed."));
			else if (!m_released)
				m_released = idle;
			else if (touchpad && touchpadCancels)
				stop(Tr("Cancelled. Nothing changed."));
			else if (const int input = touchpad ? kInputTouchPad : held; input != kInputNone)
			{
				if (!allowed)
					stop(Tr("Cancelled. Nothing changed."));
				else
				{
					asked.index = m_listening;
					asked.input = input;
					stop(Tr("Done."));
				}
			}
			return asked;
		}

	private:
		int m_listening = -1;	 // the button waiting for a press
		int m_last = -1;		 // the one the message under the list is about
		bool m_released = false; // everything was let go since the wait began
		bool m_settling = false; // the press taken is still held
		uint64_t m_until = 0;
		std::string m_message;
	};
}
