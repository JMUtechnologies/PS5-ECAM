// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the remote touch screen in the in-game menus (#38, from
// dolphin78's idea and prototype), the Wii U's (ingame.cpp) and the 3DS's and DS's (ingame3ds.cpp).
// A row, On or Off, the same setting as the launcher's page; while it is on and focused, a card beside
// the sheet with the code to scan, the address and whether a browser shows it. The card is drawn only
// then, as squares (a row's dark run as one), and its code is made once per address.

#pragma once

#include "side_menu.h"
#include "../frontend/settings.h"
#include "../stream/stream.h"
#include "../ui/qr.h"

#include <memory>
#include <string>

namespace ps5menu
{
	using ps5lang::Tr;

	inline constexpr const char* kRemoteRow = "remote";

	// The row, for the side's second screen ("The GamePad's screen", "The bottom screen", ...)
	inline Row RemoteRow(const std::string& screen)
	{
		const bool on = ps5stream::Enabled();
		return {kRemoteRow, Tr("Remote touch screen"), on ? Tr("On") : Tr("Off"), true,
			screen + " in a phone's or tablet's browser, with touch: scan the code beside this. Kept for the next games."};
	}

	// Its row chosen or changed: the server on or off at once, and ps5cemu.json saying so
	inline void ToggleRemote()
	{
		const bool on = !ps5stream::Enabled();
		ps5stream::Configure(on);
		ps5settings::SaveRemoteScreen(on);
	}

	// The card, right of the sheet, while the row is focused and the screen is on
	inline void DrawRemoteCard(const Canvas& canvas, const Fonts& fonts, const std::string& focus)
	{
		if (focus != kRemoteRow || !ps5stream::Enabled())
			return;
		const Palette& c = canvas.colours;
		ImDrawList* draw = canvas.draw;
		const float s = canvas.scale;
		const ps5stream::State state = ps5stream::Current();
		constexpr float kX = SideMenu::kX + SideMenu::kWidth + 32, kWidth = 760, kHeight = 330, kY = (1080 - kHeight) / 2, kPad = 34;
		constexpr float kCode = kHeight - 2 * kPad;
		canvas.Panel(kX, kY, kWidth, kHeight);
		float tx = kX + kPad;
		if (!state.address.empty())
		{
			static std::string s_text;
			static std::unique_ptr<ui::QrCode> s_code;
			if (!s_code || s_text != state.address)
			{
				s_text = state.address;
				s_code = std::make_unique<ui::QrCode>(s_text);
			}
			// white, rounded, four modules of quiet zone; the dark modules a row's run at a time
			const int n = s_code->Size();
			const float x0 = kX + kPad, y0 = kY + kPad;
			draw->AddRectFilled(canvas.At(x0, y0), canvas.At(x0 + kCode, y0 + kCode), IM_COL32_WHITE, 16 * s);
			if (n > 0)
			{
				const float module = kCode / (n + 8);
				const ImU32 dark = IM_COL32(0x05, 0x07, 0x0d, 0xff);
				for (int y = 0; y < n; y++)
					for (int x = 0; x < n;)
					{
						if (!s_code->Dark(x, y))
						{
							x++;
							continue;
						}
						int end = x;
						while (end < n && s_code->Dark(end, y))
							end++;
						draw->AddRectFilled(canvas.At(x0 + (4 + x) * module, y0 + (4 + y) * module),
							canvas.At(x0 + (4 + end) * module, y0 + (5 + y) * module), dark);
						x = end;
					}
			}
			tx += kCode + 36;
		}
		const float tw = kX + kWidth - kPad - tx;
		const bool working = state.kind == ps5stream::State::Kind::Waiting || state.kind == ps5stream::State::Kind::Showing;
		float y = kY + 58;
		canvas.Tracked(fonts.chip, kChip, tx, y, c.faint, working ? "SCAN IT, OR OPEN" : "REMOTE TOUCH SCREEN", 2.0f);
		y += 36;
		if (!state.address.empty())
		{
			canvas.Text(fonts.heading, kHeading, tx, y, c.title, canvas.Fit(fonts.heading, kHeading, state.address, tw));
			y += 52;
		}
		const ImU32 dot = state.kind == ps5stream::State::Kind::Showing ? c.good : working ? c.accent : c.bad;
		const std::string what = ps5stream::Describe(state.kind);
		const float chipWidth = std::min(tw, canvas.Width(fonts.caption, kCaption, what) + 52);
		draw->AddRectFilled(canvas.At(tx, y), canvas.At(tx + chipWidth, y + 38), c.focus, 19 * s);
		draw->AddCircleFilled(canvas.At(tx + 20, y + 19), 6 * s, dot);
		canvas.Text(fonts.caption, kCaption, tx + 36, y + 8, c.text, canvas.Fit(fonts.caption, kCaption, what, chipWidth - 44));
		y += 56;
		canvas.Text(fonts.caption, kCaption, tx, y, c.copy,
			working ? Tr("A phone or tablet on the PS5's network. Sound stays on the PS5.") : Tr("Turn it off and on again once that is fixed."), tw);
	}
}
