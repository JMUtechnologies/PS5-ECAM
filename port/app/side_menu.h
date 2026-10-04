// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the in-game menus' panel, the Wii U's (ingame.cpp) and the 3DS's (ingame3ds.cpp). One
// panel down the left of the screen over the dimmed game, the rest of the game left in view: the
// game's box art, name, publisher and year at its top, then its rows. A row is a setting, an action,
// or a category that opens in place to show its own rows (one open at a time), so the list stays
// short; it scrolls when an open category makes it longer than the panel. Under the list, the
// focused row's help; at the bottom, the controller hints.
//
// The menu reads the controller itself, as the launcher does: Up and Down move (held, they repeat),
// Left and Right change a setting, Cross chooses or opens and closes a category (Right opens it,
// Left closes it), Circle closes the open category, else the menu.

#pragma once

#include "menu_canvas.h"

#include "../ps5/pad.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ps5menu
{
	struct Row
	{
		std::string id;
		std::string label, value;
		bool setting = false;	// Left and Right change it
		std::string help;		// its first paragraph shows under the list
		std::vector<Row> rows;	// a category: the rows it opens to
		bool apart = false;		// set apart from the rows above it, under a line (leaving the game)
	};

	// What the player did this frame: a row chosen with Cross, or a setting changed with Left or Right
	struct Action
	{
		std::string id;
		int change = 0;		 // -1 or 1
		bool chosen = false; // Cross
	};

	// The top of the panel: the game
	struct Header
	{
		ImTextureID cover = nullptr;
		float coverWidth = 0, coverHeight = 0; // the picture's own size, for its shape
		std::string kicker, title, details;
	};

	struct Fonts
	{
		ImFont* head;
		ImFont* row;
		ImFont* small;
	};

	class SideMenu
	{
	public:
		// The panel's place on the launcher's 1920x1080 layout
		static constexpr float kX = 40, kY = 40, kWidth = 660, kHeight = 1000;
		static constexpr float kListTop = 292, kListBottom = 872, kRowHeight = 52, kPitch = 56, kApart = 18;

		// The menu opened again: its first row in focus, every category closed, and the buttons held
		// now (the shortcut that opened it) not counted until they are let go.
		void Reset()
		{
			m_focus.clear();
			m_open.clear();
			m_scroll = 0;
			m_held = ~0u;
		}

		// The open category's id (empty: none), and the focused row's
		const std::string& Open() const { return m_open; }
		const std::string& Focus() const { return m_focus; }
		void SetOpen(const std::string& id, const std::string& focus)
		{
			m_open = id;
			m_focus = focus;
		}

		// One frame's buttons (ps5pad's, with the left stick's directions as the D-pad's), at nowUs.
		// What was chosen or changed comes back; close says Circle (or Options) asks for the menu to
		// close.
		Action Update(const std::vector<Row>& rows, uint32_t buttons, uint64_t nowUs, bool& close)
		{
			close = false;
			Action action;
			const uint32_t pressed = buttons & ~m_held;
			m_held = buttons;
			if (buttons & ps5pad::kTouchPad)
				return action; // a shortcut's chord, not the menu's
			auto repeat = [&](uint32_t mask, int slot) {
				if (pressed & mask)
				{
					m_repeatAt[slot] = nowUs + 380000;
					return true;
				}
				if ((buttons & mask) && nowUs >= m_repeatAt[slot])
				{
					m_repeatAt[slot] = nowUs + 90000;
					return true;
				}
				return false;
			};
			const bool up = repeat(ps5pad::kUp, 0), down = repeat(ps5pad::kDown, 1);
			const bool left = repeat(ps5pad::kLeft, 2), right = repeat(ps5pad::kRight, 3);

			const std::vector<Shown> shown = Visible(rows);
			int at = Focused(shown);
			if (up || down)
			{
				at = (at + (down ? 1 : (int)shown.size() - 1)) % (int)shown.size();
				m_focus = shown[at].row->id;
			}
			const Shown& focused = shown[at];
			const Row& row = *focused.row;
			const bool category = !row.rows.empty();
			if (pressed & ps5pad::kCircle)
			{
				if (!m_open.empty())
				{
					m_focus = m_open; // back to the category's own row, closed
					m_open.clear();
				}
				else
					close = true;
			}
			else if (pressed & ps5pad::kOptions)
				close = true;
			else if (category && ((pressed & ps5pad::kCross) || right || left))
			{
				const bool opening = m_open != row.id && !left;
				m_open = opening ? row.id : std::string();
			}
			else if (left && focused.depth > 0 && !row.setting)
			{
				m_focus = m_open; // Left on a row in a category: back to the category, closed
				m_open.clear();
			}
			else if (pressed & ps5pad::kCross)
				action = {row.id, 1, true};
			else if ((left || right) && row.setting)
				action = {row.id, left ? -1 : 1, false};
			KeepInView(Visible(rows));
			return action;
		}

		void Draw(const Canvas& canvas, const Fonts& fonts, const Header& header, const std::vector<Row>& rows,
			const std::vector<std::pair<const char*, std::string>>& hints) const
		{
			const Palette& c = canvas.colours;
			ImDrawList* draw = canvas.draw;
			const float s = canvas.scale;
			const ImVec2 screen = ImGui::GetIO().DisplaySize;
			draw->AddRectFilled({0, 0}, screen, (c.dim & 0x00ffffff) | (0x8cu << IM_COL32_A_SHIFT)); // the game, dimmed
			// the panel: the launcher's glass, with a brighter rim
			draw->AddRectFilled(canvas.At(kX, kY), canvas.At(kX + kWidth, kY + kHeight), c.panel, 26 * s);
			draw->AddRect(canvas.At(kX, kY), canvas.At(kX + kWidth, kY + kHeight), c.focusEdge, 26 * s, 0, 2 * s);

			// the game: its box art (or a place for it), the brand, its name, publisher and year
			constexpr float kCoverX = 76, kCoverY = 76, kCoverWidth = 132, kCoverHeight = 176;
			float coverWidth = kCoverWidth, coverHeight = kCoverWidth;
			if (header.cover && header.coverWidth > 0 && header.coverHeight > 0)
			{
				const float fit = std::min(kCoverWidth / header.coverWidth, kCoverHeight / header.coverHeight);
				coverWidth = header.coverWidth * fit;
				coverHeight = header.coverHeight * fit;
				const ImVec2 a = canvas.At(kCoverX, kCoverY), b = canvas.At(kCoverX + coverWidth, kCoverY + coverHeight);
				draw->AddRectFilled({a.x + 4 * s, a.y + 8 * s}, {b.x + 4 * s, b.y + 8 * s}, IM_COL32(0, 0, 0, 90), 12 * s);
				draw->AddImageRounded(header.cover, a, b, {0, 0}, {1, 1}, IM_COL32_WHITE, 10 * s);
				draw->AddRect(a, b, IM_COL32(255, 255, 255, 60), 10 * s, 0, 1.5f * s);
			}
			else
			{
				draw->AddRectFilled(canvas.At(kCoverX, kCoverY), canvas.At(kCoverX + coverWidth, kCoverY + coverHeight), c.row, 12 * s);
				draw->AddRect(canvas.At(kCoverX, kCoverY), canvas.At(kCoverX + coverWidth, kCoverY + coverHeight), c.rowEdge, 12 * s, 0, s);
			}
			const float textX = kCoverX + coverWidth + 26, textWidth = kX + kWidth - 36 - textX;
			canvas.Text(fonts.small, 20, textX, kCoverY + 6, c.kicker, header.kicker);
			// the name, on two lines at most
			draw->PushClipRect(canvas.At(textX, kCoverY + 36), canvas.At(textX + textWidth, kCoverY + 36 + 2 * 38), true);
			canvas.Text(fonts.head, 30, textX, kCoverY + 36, c.title, header.title, textWidth);
			draw->PopClipRect();
			const float titleHeight = std::min(2.0f * 38, fonts.head->CalcTextSizeA(30 * s, FLT_MAX, textWidth * s, header.title.c_str()).y / s);
			draw->PushClipRect(canvas.At(textX, 0), canvas.At(textX + textWidth, 1080), true);
			canvas.Text(fonts.small, 20, textX, kCoverY + 44 + titleHeight, c.copy, header.details, textWidth);
			draw->PopClipRect();
			const float line = kListTop - 18;
			draw->AddLine(canvas.At(kX + 32, line), canvas.At(kX + kWidth - 32, line), c.line, s);

			// the rows, scrolled so the focused one is in view
			const std::vector<Shown> shown = Visible(rows);
			const int at = Focused(shown);
			draw->PushClipRect(canvas.At(kX, kListTop - 4), canvas.At(kX + kWidth, kListBottom + 4), true);
			for (int i = 0; i < (int)shown.size(); i++)
			{
				const Row& row = *shown[i].row;
				const float y = kListTop + shown[i].y - m_scroll;
				if (y + kRowHeight < kListTop - 4 || y > kListBottom + 4)
					continue;
				if (row.apart)
					draw->AddLine(canvas.At(kX + 32, y - kApart / 2 - 2), canvas.At(kX + kWidth - 32, y - kApart / 2 - 2), c.line, s);
				const bool focused = i == at;
				const float indent = shown[i].depth ? 28.0f : 0.0f;
				const float x = kX + 24 + indent, width = kWidth - 48 - indent;
				canvas.Row(x, y, width, kRowHeight, focused);
				const bool category = !row.rows.empty();
				canvas.Text(fonts.row, shown[i].depth ? 22.0f : 24.0f, x + 24, y + (shown[i].depth ? 13.0f : 12.0f),
					focused ? c.title : shown[i].depth ? c.copy : c.text, row.label);
				float right = x + width - 24;
				if (category)
				{
					// a chevron: pointing right when closed, down when open
					const ImVec2 m = canvas.At(right - 6, y + kRowHeight / 2);
					const float r = 7 * s;
					if (m_open == row.id)
						draw->AddTriangleFilled({m.x - r, m.y - r * 0.5f}, {m.x + r, m.y - r * 0.5f}, {m.x, m.y + r * 0.7f}, c.accent);
					else
						draw->AddTriangleFilled({m.x - r * 0.5f, m.y - r}, {m.x - r * 0.5f, m.y + r}, {m.x + r * 0.7f, m.y}, c.accent);
					right -= 28;
				}
				if (row.value.empty())
					continue;
				if (focused && row.setting)
				{
					// arrows either side of the value: Left and Right change it
					const ImVec2 m = canvas.At(right - 4, y + kRowHeight / 2);
					const float r = 6 * s;
					draw->AddTriangleFilled({m.x - r * 0.6f, m.y - r}, {m.x - r * 0.6f, m.y + r}, {m.x + r * 0.6f, m.y}, c.accent);
					right -= 22;
				}
				const float valueWidth = fonts.row->CalcTextSizeA(22 * s, FLT_MAX, 0.0f, row.value.c_str()).x / s;
				const float limit = width - 48 - (category ? 28 : 0) - (focused && row.setting ? 44 : 0) - 200; // the label keeps 200
				const float shownWidth = std::min(valueWidth, std::max(80.0f, limit));
				draw->PushClipRect(canvas.At(right - shownWidth, y), canvas.At(right, y + kRowHeight), true);
				canvas.Text(fonts.row, 22, right - valueWidth, y + 14, c.accent, row.value);
				draw->PopClipRect();
				if (focused && row.setting)
				{
					const ImVec2 m = canvas.At(right - shownWidth - 14, y + kRowHeight / 2);
					const float r = 6 * s;
					draw->AddTriangleFilled({m.x + r * 0.6f, m.y - r}, {m.x + r * 0.6f, m.y + r}, {m.x - r * 0.6f, m.y}, c.accent);
				}
			}
			draw->PopClipRect();
			// a scroll bar, when the list is longer than its place
			const float total = shown.empty() ? 0 : shown.back().y + kRowHeight, room = kListBottom - kListTop;
			if (total > room)
			{
				const float barTop = kListTop + room * m_scroll / total, barHeight = room * room / total;
				draw->AddRectFilled(canvas.At(kX + kWidth - 14, kListTop), canvas.At(kX + kWidth - 10, kListBottom), c.row, 2 * s);
				draw->AddRectFilled(canvas.At(kX + kWidth - 14, barTop), canvas.At(kX + kWidth - 10, barTop + barHeight), c.accent, 2 * s);
			}

			// the focused row's help: its first paragraph, on two lines at most
			if (!shown.empty())
			{
				const std::string& help = shown[at].row->help;
				const std::string first = help.substr(0, help.find('\n'));
				draw->PushClipRect(canvas.At(kX + 32, kListBottom + 12), canvas.At(kX + kWidth - 32, kListBottom + 12 + 2 * 28), true);
				canvas.Text(fonts.small, 20, kX + 36, kListBottom + 12, c.copy, first, kWidth - 72);
				draw->PopClipRect();
			}
			draw->AddLine(canvas.At(kX + 32, kY + kHeight - 82), canvas.At(kX + kWidth - 32, kY + kHeight - 82), c.line, s);
			float x = kX + 36;
			for (const auto& [button, label] : hints)
				x = canvas.Hint(fonts.small, x, kY + kHeight - 60, button, label) - 18;
		}

	private:
		struct Shown
		{
			const Row* row;
			int depth;
			float y; // from the list's top
		};

		std::vector<Shown> Visible(const std::vector<Row>& rows) const
		{
			std::vector<Shown> shown;
			float y = 0;
			for (const Row& row : rows)
			{
				if (row.apart && !shown.empty())
					y += kApart;
				shown.push_back({&row, 0, y});
				y += kPitch;
				if (!row.rows.empty() && m_open == row.id)
					for (const Row& child : row.rows)
					{
						shown.push_back({&child, 1, y});
						y += kPitch;
					}
			}
			return shown;
		}

		int Focused(const std::vector<Shown>& shown) const
		{
			for (int i = 0; i < (int)shown.size(); i++)
				if (shown[i].row->id == m_focus)
					return i;
			return 0;
		}

		void KeepInView(const std::vector<Shown>& shown)
		{
			if (shown.empty())
				return;
			const int at = Focused(shown);
			m_focus = shown[at].row->id;
			const float room = kListBottom - kListTop, total = shown.back().y + kRowHeight;
			// the focused row, and the one after it where there is one, in view
			const float top = shown[at].y - (at > 0 ? kPitch * 0.5f : 0.0f);
			const float bottom = shown[at].y + kRowHeight + (at + 1 < (int)shown.size() ? kPitch * 0.5f : 0.0f);
			if (top < m_scroll)
				m_scroll = top;
			if (bottom > m_scroll + room)
				m_scroll = bottom - room;
			m_scroll = std::clamp(m_scroll, 0.0f, std::max(0.0f, total - room));
		}

		std::string m_focus; // the focused row's id
		std::string m_open;	 // the open category's id
		float m_scroll = 0;
		uint32_t m_held = ~0u;
		std::array<uint64_t, 4> m_repeatAt{};
	};
}
