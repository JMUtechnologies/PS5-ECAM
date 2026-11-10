// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the in-game menus' sheet, the Wii U's (ingame.cpp) and the 3DS's (ingame3ds.cpp), as the
// launcher's Quick Menu. One full-height glass sheet against the left edge of the screen over the
// game (6.0), dimmed more on the left than on the right: the game's cover, badges and name at its top,
// a row of round quick actions, then the list. A row is a setting, an action, or a category that opens in
// place to show its own rows (one open at a time), its current value beside it so most visits need
// no opening; the list scrolls when an open category makes it longer than the sheet. Under the list,
// the focused row's help; at the bottom, the controller hints.
//
// The menu reads the controller itself, as the launcher does: Up and Down move (held, they repeat),
// from the list's ends onto the quick actions; Left and Right move along the quick actions or change
// a setting; Cross chooses, or opens and closes a category (Right opens it, Left closes it); Circle
// closes the open category, else the menu. What cannot be undone (leaving the game, loading a state)
// is held: a ring fills over 600 ms. The focus is a tint of the side's colour with a bar at its left,
// gliding between rows; on the quick actions, the focused one is filled in that colour. An open
// category's rows hang off a thin guide line. tools/preview-menu.sh draws it on the PC.

#pragma once

#include "menu_canvas.h"

#include "../ps5/pad.h"

#include <algorithm>
#include <array>
#include <cmath>
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
		bool hold = false;		// Cross held to confirm, not pressed
		float slider = -1.0f;	// 0 to 1: drawn as a slider beside the value
		int pad = -1;			// a Buttons row's DualSense input (buttons_menu.h's kInput*): its place on the controller's picture
	};

	// A quick action: a large tile above the list. Its id is a row's (a category's opens it) or an
	// action's of its own.
	struct Tile
	{
		std::string id, label;
		std::string icon;	 // menu_canvas.h's Icon
		std::string caption; // small, under the label
		bool hold = false;
		std::string help; // under the list while it is focused (else its row's)
	};

	// What the player did this frame: a row chosen with Cross, or a setting changed with Left or Right
	struct Action
	{
		std::string id;
		int change = 0;		 // -1 or 1
		bool chosen = false; // Cross
	};

	// The top of the sheet: the game
	struct Header
	{
		ImTextureID cover = nullptr;
		float coverWidth = 0, coverHeight = 0; // the picture's own size, for its shape
		std::string system;					   // the badge: WII U, 3DS
		std::string status;					   // the second: RUNNING, PAUSED
		std::string title, details;
		// the Buttons page's DualSense: Zacksly's drawing (assets/ui/dualsense.tga, white lines), when the menu has it
		ImTextureID pad = nullptr;
	};

	class SideMenu
	{
	public:
		// The sheet's place on the launcher's 1920x1080 layout
		// (6.0: a full-height glass sheet against the screen's left edge, as the PS5's own Control Center)
		static constexpr float kX = 0, kY = 0, kWidth = 700, kHeight = 1080;
		static constexpr float kInsetX = 64, kInnerWidth = 576; // the content's column
		static constexpr float kTilesTop = 232, kTileHeight = 132, kTileGap = 0;
		static constexpr float kRowHeight = 56, kPitch = 60, kApart = 26;
		static constexpr float kListBottom = kY + kHeight - 164;
		static constexpr float kFooter = kY + kHeight - 100; // the hairline over the hints
		static constexpr uint64_t kHoldUs = 600000;

		// The menu opened again: its first quick action in focus, every category closed, and the
		// buttons held now (the shortcut that opened it) not counted until they are let go.
		void Reset()
		{
			m_focus.clear();
			m_open.clear();
			m_onTiles = true;
			m_tile = 0;
			m_scroll = 0;
			m_held = ~0u;
			m_holding.clear();
			m_holdProgress = 0;
			m_ringValid = false;
		}

		// The open category's id (empty: none), and the focused row's or quick action's
		const std::string& Open() const { return m_open; }
		const std::string& Focus() const { return m_onTiles ? m_tileId : m_focus; }
		void SetOpen(const std::string& id, const std::string& focus)
		{
			m_open = id;
			m_focus = focus;
			m_onTiles = false;
		}

		// One frame's buttons (ps5pad's, with the left stick's directions as the D-pad's), at nowUs.
		// What was chosen or changed comes back; close says Circle (or Options) asks for the menu to
		// close.
		Action Update(const std::vector<Row>& rows, const std::vector<Tile>& tiles, uint32_t buttons, uint64_t nowUs, bool& close)
		{
			close = false;
			Action action;
			const uint32_t pressed = buttons & ~m_held;
			m_held = buttons;
			if (buttons & ps5pad::kTouchPad)
			{
				m_holding.clear(); // a shortcut's chord, not the menu's
				m_holdProgress = 0;
				return action;
			}
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

			std::vector<Shown> shown = Visible(rows);
			m_tilesShown = !tiles.empty();
			if (tiles.empty())
				m_onTiles = false;
			else if (shown.empty())
				m_onTiles = true;
			m_tile = tiles.empty() ? 0 : std::clamp(m_tile, 0, (int)tiles.size() - 1);
			int at = Focused(shown);
			if (up || down)
			{
				const int last = (int)shown.size() - 1;
				if (m_onTiles)
				{
					m_onTiles = shown.empty();
					at = down ? 0 : last;
				}
				else if (!tiles.empty() && ((up && at == 0) || (down && at == last)))
					m_onTiles = true;
				else if (!shown.empty())
					at = (at + (down ? 1 : last)) % (last + 1);
				if (!m_onTiles)
					m_focus = shown[at].row->id;
			}

			if (pressed & ps5pad::kCircle)
			{
				if (!m_open.empty())
				{
					m_focus = m_open; // back to the category's own row, closed
					m_open.clear();
					m_onTiles = false;
				}
				else
					close = true;
			}
			else if (pressed & ps5pad::kOptions)
				close = true;
			else if (m_onTiles)
			{
				if (left || right)
					m_tile = std::clamp(m_tile + (right ? 1 : -1), 0, (int)tiles.size() - 1);
				const Tile& tile = tiles[m_tile];
				if (tile.hold)
					action = Hold(tile.id, pressed, buttons, nowUs);
				else if (pressed & ps5pad::kCross)
				{
					const auto category = std::find_if(rows.begin(), rows.end(), [&](const Row& row) { return row.id == tile.id && !row.rows.empty(); });
					if (category != rows.end())
					{
						// a category's quick action: it opens, on its first row
						m_open = category->id;
						m_focus = category->rows.front().id;
						m_onTiles = false;
					}
					else
						action = {tile.id, 1, true};
				}
			}
			else if (!shown.empty())
			{
				const Shown& focused = shown[at];
				const Row& row = *focused.row;
				const bool category = !row.rows.empty();
				if (category && ((pressed & ps5pad::kCross) || right || left))
				{
					const bool opening = m_open != row.id && !left;
					m_open = opening ? row.id : std::string();
				}
				else if (left && focused.depth > 0 && !row.setting)
				{
					m_focus = m_open; // Left on a row in a category: back to the category, closed
					m_open.clear();
				}
				else if (row.hold)
					action = Hold(row.id, pressed, buttons, nowUs);
				else if (pressed & ps5pad::kCross)
					action = {row.id, 1, true};
				else if ((left || right) && row.setting)
					action = {row.id, left ? -1 : 1, false};
			}
			if (m_onTiles && !tiles.empty())
				m_tileId = tiles[m_tile].id;
			const std::string focus = Focus();
			if (!m_holding.empty() && m_holding != focus)
			{
				m_holding.clear(); // the focus moved away: the hold starts over
				m_holdProgress = 0;
			}
			KeepInView(Visible(rows));
			return action;
		}

		void Draw(const Canvas& canvas, const Fonts& fonts, const Header& header, const std::vector<Row>& rows, const std::vector<Tile>& tiles,
			const std::vector<std::pair<const char*, std::string>>& hints) const
		{
			const Palette& c = canvas.colours;
			ImDrawList* draw = canvas.draw;
			const float s = canvas.scale;
			const float line = std::max(1.0f, s);
			const ImGuiIO& io = ImGui::GetIO();
			const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
			auto box = [&](float x, float y, float w, float h, ImU32 colour, float radius) {
				draw->AddRectFilled(canvas.At(x, y), canvas.At(x + w, y + h), colour, radius * s);
			};

			// the game, dimmed by one quad: more on the left, under the sheet, than on the right
			const ImU32 heavy = Colour(0x04060c, 0xc8), light = Colour(0x04060c, 0x30);
			draw->AddRectFilledMultiColor({0, 0}, io.DisplaySize, heavy, light, light, heavy);
			// the sheet: glass, darker at its foot, the side's colour a glow in its top corner; its edge a
			// hairline with a soft shadow beyond it
			const ImVec2 sheetA = canvas.At(kX, kY), sheetB = canvas.At(kX + kWidth, kY + kHeight);
			draw->AddRectFilledMultiColor({0, sheetA.y}, sheetB, Colour(0x0c1120, 0xee), Colour(0x0c1120, 0xee), Colour(0x06080f, 0xf6),
				Colour(0x06080f, 0xf6));
			draw->AddRectFilledMultiColor({0, sheetA.y}, canvas.At(kX + kWidth, kY + 420), Fade(c.accent, 0.11f), Fade(c.accent, 0.11f),
				Fade(c.accent, 0.0f), Fade(c.accent, 0.0f));
			draw->AddRectFilledMultiColor({sheetB.x, sheetA.y}, {sheetB.x + 36 * s, sheetB.y}, Colour(0x000000, 0x50), Colour(0x000000, 0),
				Colour(0x000000, 0), Colour(0x000000, 0x50));
			draw->AddLine({sheetB.x, sheetA.y}, {sheetB.x, sheetB.y}, Fade(c.accent, 0.35f), line);

			// the game: its cover (or a place for it), its name, its badges and facts
			constexpr float kCoverX = kInsetX, kCoverY = 72, kCoverBox = 124;
			float coverWidth = kCoverBox * 0.75f, coverHeight = kCoverBox;
			if (header.cover && header.coverWidth > 0 && header.coverHeight > 0)
			{
				const float fit = std::min(kCoverBox / header.coverWidth, kCoverBox / header.coverHeight);
				coverWidth = header.coverWidth * fit;
				coverHeight = header.coverHeight * fit;
				const ImVec2 a = canvas.At(kCoverX, kCoverY), b = canvas.At(kCoverX + coverWidth, kCoverY + coverHeight);
				for (int shadow = 3; shadow >= 1; shadow--) // a soft shadow under it
					draw->AddRectFilled({a.x - shadow * 2 * s, a.y + (4 + shadow * 3) * s}, {b.x + shadow * 2 * s, b.y + (8 + shadow * 4) * s},
						IM_COL32(0, 0, 0, 34), (12 + shadow * 4) * s);
				draw->AddImageRounded(header.cover, a, b, {0, 0}, {1, 1}, IM_COL32_WHITE, 12 * s);
				draw->AddRect(a, b, Colour(0xffffff, 0x26), 12 * s, 0, line);
			}
			else
			{
				box(kCoverX, kCoverY, coverWidth, coverHeight, Colour(0xffffff, 0x0c), 12);
				canvas.Icon("resume", kCoverX + coverWidth / 2, kCoverY + coverHeight / 2, Colour(0xffffff, 0x30));
			}
			const float textX = kCoverX + coverWidth + 26, textWidth = kX + kWidth - 40 - textX;
			float chipX = textX;
			if (!header.system.empty())
				chipX = canvas.Chip(fonts.chip, chipX, kCoverY + 4, header.system, c.accent, Fade(c.accent, 0.16f), true);
			if (!header.status.empty())
				chipX = canvas.Chip(fonts.chip, chipX, kCoverY + 4, header.status, c.copy, Colour(0xffffff, 0x10));
			canvas.TextFit(fonts.heading, kHeading, textX, kCoverY + 46, c.title, header.title, textWidth);
			canvas.TextFit(fonts.caption, kCaption, textX, kCoverY + 92, c.faint, header.details, textWidth);

			// the quick actions: a round button each, its name and state under it; the focused one filled
			// in the side's colour
			const float listTop = tiles.empty() ? kTilesTop + 8 : kTilesTop + kTileHeight + 24;
			if (!tiles.empty())
			{
				const float width = kInnerWidth / tiles.size();
				for (size_t i = 0; i < tiles.size(); i++)
				{
					const Tile& tile = tiles[i];
					const float cx = kInsetX + width * (i + 0.5f), cy = kTilesTop + 38, radius = 36;
					const bool focused = m_onTiles && (int)i == m_tile;
					if (focused)
					{
						draw->AddCircleFilled(canvas.At(cx, cy), (radius + 9) * s, Fade(c.accent, 0.16f), 48);
						draw->AddCircleFilled(canvas.At(cx, cy), radius * s, c.accent, 48);
					}
					else
					{
						draw->AddCircleFilled(canvas.At(cx, cy), radius * s, Colour(0xffffff, 0x10), 48);
						draw->AddCircle(canvas.At(cx, cy), radius * s, Colour(0xffffff, 0x14), 48, line);
					}
					canvas.Icon(tile.icon, cx, cy, focused ? c.accentInk : c.text);
					if (focused && tile.hold && m_holdProgress > 0)
						canvas.HoldRing(cx, cy, radius + 6, m_holdProgress, c.title);
					canvas.TextFit(fonts.caption, kCaption, cx, kTilesTop + 84, focused ? c.title : c.copy, tile.label, width - 8, Canvas::Align::Centre);
					if (!tile.caption.empty())
						canvas.TextFit(fonts.caption, kCaption - 3, cx, kTilesTop + 108, focused ? c.accent : c.faint, tile.caption, width - 8,
							Canvas::Align::Centre);
				}
			}

			// the rows, scrolled so the focused one is in view
			const std::vector<Shown> shown = Visible(rows);
			const int at = Focused(shown);
			const float room = kListBottom - listTop;
			m_scrollShown += (m_scroll - m_scrollShown) * (m_ringValid ? 1.0f - std::exp(-16.0f * dt) : 1.0f);
			draw->PushClipRect(canvas.At(kX, listTop - 8), canvas.At(kX + kWidth, kListBottom + 8), true);

			// the focus: a tint of the side's colour with a bar at its left, gliding to the focused row
			if (!m_onTiles && !shown.empty())
			{
				const float indent = shown[at].depth > 0 ? 28.0f : 0.0f;
				const float targetX = kInsetX - 16 + indent, targetY = listTop + shown[at].y - m_scrollShown;
				const float targetWidth = kInnerWidth + 16 - indent;
				const float k = m_ringValid ? 1.0f - std::exp(-22.0f * dt) : 1.0f;
				m_ring[0] += (targetX - m_ring[0]) * k;
				m_ring[1] += (targetY - m_ring[1]) * k;
				m_ring[2] += (targetWidth - m_ring[2]) * k;
				m_ring[3] = kRowHeight;
				m_ringValid = true;
				const ImU32 tint = shown[at].row->apart ? c.bad : c.accent;
				box(m_ring[0], m_ring[1], m_ring[2], m_ring[3], Fade(tint, 0.17f), 14);
				draw->AddRect(canvas.At(m_ring[0], m_ring[1]), canvas.At(m_ring[0] + m_ring[2], m_ring[1] + m_ring[3]), Fade(tint, 0.38f), 14 * s,
					0, line);
				box(m_ring[0] + 6, m_ring[1] + 14, 4, m_ring[3] - 28, tint, 2);
			}
			else
				m_ringValid = false;

			// an open category's rows: a thin guide down their left, in place of a box each
			for (int i = 0; i < (int)shown.size(); i++)
				if (shown[i].depth > 0 && (i == 0 || shown[i - 1].depth == 0))
				{
					int last = i;
					while (last + 1 < (int)shown.size() && shown[last + 1].depth > 0)
						last++;
					const float top = listTop + shown[i].y - m_scrollShown + 8, bottom = listTop + shown[last].y - m_scrollShown + kRowHeight - 8;
					box(kInsetX + 4, top, 2, bottom - top, Fade(c.accent, 0.45f), 1);
				}

			for (int i = 0; i < (int)shown.size(); i++)
			{
				const Row& row = *shown[i].row;
				const float y = listTop + shown[i].y - m_scrollShown;
				if (y + kRowHeight < listTop - 8 || y > kListBottom + 8)
					continue;
				if (row.apart)
					draw->AddLine(canvas.At(kInsetX, y - kApart / 2), canvas.At(kInsetX + kInnerWidth, y - kApart / 2), Colour(0xffffff, 0x14), line);
				const bool focused = !m_onTiles && i == at;
				const bool child = shown[i].depth > 0;
				const float indent = child ? 28.0f : 0.0f;
				const float x = kInsetX + indent, width = kInnerWidth - indent;
				ImFont* labelFont = child ? fonts.label : fonts.body;
				const float labelSize = child ? kLabel : kBody;
				const ImU32 labelColour = row.apart ? c.bad : focused ? c.title : child ? c.copy : c.text;
				const bool category = !row.rows.empty();
				float right = x + width - 14;
				if (category)
				{
					canvas.Chevron(right - 4, y + kRowHeight / 2, focused ? c.text : c.faint, m_open == row.id ? Canvas::Point::Down : Canvas::Point::Right);
					right -= 28;
				}
				// what goes right of the label: a hold's ring, a slider, the value
				float valueLeft = right;
				if (row.hold && focused)
				{
					const float ringRadius = 11;
					canvas.HoldRing(right - ringRadius, y + kRowHeight / 2, ringRadius, m_holdProgress, row.apart ? c.bad : c.accent);
					right -= ringRadius * 2 + 12;
					canvas.Button(right - 9, y + kRowHeight / 2, "cross", c.copy);
					right -= 28;
					// tr: beside a row held to confirm: hold Cross
					const char* hold = ps5lang::Tr("Hold");
					valueLeft = right - canvas.TextFit(fonts.label, kLabel, right, y + 14, c.copy, hold, 140, Canvas::Align::Right);
				}
				else if (!row.value.empty())
				{
					const bool arrows = focused && row.setting;
					if (arrows)
					{
						canvas.Chevron(right - 4, y + kRowHeight / 2, c.accent);
						right -= 22;
					}
					const float labelRoom = canvas.Width(labelFont, labelSize, row.label) + 36;
					const float sliderWidth = row.slider >= 0 ? 150 : 0;
					// the value has what the label leaves it, and at least a third of the row
					const float valueRoom = std::max(width * 0.34f, right - x - 20 - labelRoom - sliderWidth);
					valueLeft = right - canvas.TextFit(fonts.label, kLabel, right, y + 14, focused ? c.text : c.faint, row.value, valueRoom,
											Canvas::Align::Right);
					if (row.slider >= 0)
					{
						// the slider: a thin track, filled in the side's colour to a small knob
						const float trackRight = valueLeft - 18, trackLeft = trackRight - 128, middle = y + kRowHeight / 2;
						const float knob = trackLeft + (trackRight - trackLeft) * std::clamp(row.slider, 0.0f, 1.0f);
						box(trackLeft, middle - 2, trackRight - trackLeft, 4, Colour(0xffffff, 0x1c), 2);
						box(trackLeft, middle - 2, knob - trackLeft, 4, focused ? c.accent : Fade(c.accent, 0.7f), 2);
						draw->AddCircleFilled(canvas.At(knob, middle), (focused ? 8 : 6) * s, c.title, 24);
						valueLeft = trackLeft - 12;
					}
					if (arrows)
					{
						canvas.Chevron(valueLeft - 14, y + kRowHeight / 2, c.accent, Canvas::Point::Left);
						valueLeft -= 26;
					}
				}
				canvas.TextFit(labelFont, labelSize, x + 20, y + (child ? 14.0f : 12.0f), labelColour, row.label, std::max(80.0f, valueLeft - x - 40));
			}
			draw->PopClipRect();
			// a scroll bar, when the list is longer than its place
			const float total = shown.empty() ? 0 : shown.back().y + kRowHeight;
			if (total > room)
			{
				const float barTop = listTop + room * m_scrollShown / total, barHeight = room * room / total;
				box(kX + kWidth - 18, barTop, 3, barHeight, Colour(0xffffff, 0x40), 1.5f);
			}

			// the focused row's help: its first paragraph, on two lines at most
			std::string help;
			if (!m_onTiles && !shown.empty())
				help = shown[at].row->help;
			else if (m_onTiles && !tiles.empty())
			{
				const Tile& tile = tiles[std::clamp(m_tile, 0, (int)tiles.size() - 1)];
				help = tile.help;
				for (const Row& row : rows)
				{
					if (row.id == tile.id && help.empty())
						help = row.help;
					for (const Row& child : row.rows)
						if (child.id == tile.id && help.empty())
							help = child.help;
				}
				if (help.empty() && tile.hold)
					help = ps5lang::Tr("Hold Cross until the ring fills.");
			}
			if (!help.empty())
			{
				const std::string first = help.substr(0, help.find('\n'));
				// two lines under the list: a little smaller, then cut, never clipped mid-line
				canvas.TextLines(fonts.caption, kCaption, kInsetX, kListBottom + 14, c.faint, first, kInnerWidth, 2);
			}
			// the hints, under a hairline
			draw->AddLine(canvas.At(kInsetX, kFooter), canvas.At(kInsetX + kInnerWidth, kFooter), Colour(0xffffff, 0x14), line);
			float x = kInsetX;
			const float hintSize = canvas.HintsSize(fonts.label, hints, kInnerWidth);
			for (const auto& [button, label] : hints)
				x = canvas.Hint(fonts.label, x, kFooter + 30, button, label, hintSize);
			// the Buttons category open: the DualSense beside the sheet, each control with the console's button it
			// presses, the focused one's lit (Porpoise X's Customize Buttons, in the sheet's look)
			for (const Row& row : rows)
				if (row.id == "buttons" && m_open == "buttons")
					DrawPad(canvas, fonts, header, row, m_onTiles ? std::string() : m_focus);
		}

		// The DualSense on the game's side of the screen (Zacksly's drawing, as Porpoise X's Customize Buttons
		// has it), each set control with a chip of the console's button it presses, in a column beside it
		void DrawPad(const Canvas& canvas, const Fonts& fonts, const Header& header, const Row& category, const std::string& focus) const
		{
			const Palette& c = canvas.colours;
			ImDrawList* draw = canvas.draw;
			const float s = canvas.scale;
			const float line = std::max(1.5f, 2.2f * s);
			// each input's place on the drawing, in its own 1400x900 units (Porpoise X's ui_app_controls.cpp), in
			// buttons_menu.h's kInput order
			static constexpr ImVec2 kSpots[] = {
				{0, 0},																	   // none
				{1035, 399}, {1114, 319}, {955, 319}, {1035, 240},						   // Cross, Circle, Square, Triangle
				{372, 140}, {1028, 140}, {366, 112}, {1034, 112},						   // L1, R1, L2, R2
				{525, 468}, {873, 468},													   // L3, R3
				{451, 198}, {949, 198},													   // Create, Options
				{363, 250}, {363, 388}, {299, 319}, {428, 319},							   // the D-pad: up, down, left, right
				{525, 415}, {525, 521}, {472, 468}, {578, 468},							   // the left stick's directions
				{873, 415}, {873, 521}, {820, 468}, {926, 468},							   // the right stick's
				{700, 233},																   // the touchpad
			};
			// the drawing, 640 wide, centred at x 1310 in the launcher's 1920x1080
			constexpr float kArtX = 990, kArtY = 372, kArtScale = 640.0f / 1400.0f;
			auto spot = [&](int input) { return ImVec2(kArtX + kSpots[input].x * kArtScale, kArtY + kSpots[input].y * kArtScale); };
			auto at = [&](float x, float y) { return canvas.At(x, y); };
			canvas.Text(fonts.heading, kHeading, 960, 140, c.title, ps5lang::Tr("Your controller"));
			canvas.TextFit(fonts.caption, kCaption, 960, 186, c.faint,
				ps5lang::Tr("Each button shows the console's button it presses."), 900);
			if (header.pad)
				draw->AddImage(header.pad, at(kArtX, kArtY), at(kArtX + 640, kArtY + 900 * kArtScale), {0, 0}, {1, 1}, Colour(0xffffff, 0xf0));
			else
			{
				// no drawing in this copy: the controller's outline, so the chips still have a place
				draw->AddRect(at(kArtX + 90, kArtY + 40), at(kArtX + 550, kArtY + 380), Fade(c.accent, 0.55f), 120 * s, 0, line);
				draw->AddRect(at(kArtX + 215, kArtY + 45), at(kArtX + 425, kArtY + 160), Colour(0xffffff, 0x30), 16 * s, 0, line);
			}

			// which input the focused row is, and whether the menu waits for a press (every control lit)
			int focused = -1;
			bool listening = false;
			for (const Row& row : category.rows)
			{
				if (row.id == focus)
					focused = row.pad;
				listening = listening || row.value == ps5lang::Tr("Press a button");
			}
			const float pulse = 0.75f + 0.25f * std::sin((float)ImGui::GetTime() * 5.0f);
			for (int i = 1; i < (int)std::size(kSpots); i++)
				if (listening || i == focused)
				{
					const ImVec2 p = spot(i);
					const float r = listening ? 18.0f : 24.0f;
					draw->AddCircleFilled(at(p.x, p.y), r * s, Fade(c.accent, (listening ? 0.30f : 0.55f) * pulse), 32);
					draw->AddCircle(at(p.x, p.y), r * s, Fade(c.accent, 0.9f), 32, line);
				}
			// each set control's chip: the left half's in a column left of the drawing, the right half's right of it,
			// the touchpad's above; in the order of their controls from the top, as near level with them as they fit
			struct Chip
			{
				const Row* row;
				ImVec2 from;
				float y;
				int side; // -1 left, 1 right, 0 above
			};
			std::vector<Chip> chips;
			for (const Row& row : category.rows)
				if (row.pad > 0 && row.pad < (int)std::size(kSpots))
				{
					const ImVec2 p = spot(row.pad);
					const int side = row.pad == (int)std::size(kSpots) - 1 ? 0 : kSpots[row.pad].x < 700 ? -1 : 1;
					chips.push_back({&row, p, side == 0 ? kArtY - 46 : p.y, side});
				}
			std::stable_sort(chips.begin(), chips.end(), [](const Chip& a, const Chip& b) { return a.from.y < b.from.y; });
			constexpr float kGap = 42, kTop = 300, kBottom = 860;
			for (int side : {-1, 1})
			{
				std::vector<Chip*> column;
				for (Chip& chip : chips)
					if (chip.side == side)
						column.push_back(&chip);
				float next = kTop;
				for (Chip* chip : column)
					next = (chip->y = std::max(chip->y, next)) + kGap;
				// past the bottom: the column moved up, as far as its top allows
				if (!column.empty() && column.back()->y > kBottom)
				{
					float limit = kBottom;
					for (auto it = column.rbegin(); it != column.rend(); ++it, limit -= kGap)
						(*it)->y = std::min((*it)->y, limit);
				}
			}
			for (const Chip& chip : chips)
			{
				const bool on = chip.row->pad == focused;
				const std::string& label = chip.row->label;
				const float h = on ? 38 : 32, size = on ? 20 : 17;
				const float w = std::max(h, canvas.Measure(fonts.chip, size, label) + h * 0.8f);
				const float cx = chip.side < 0 ? kArtX - 30 - w / 2 : chip.side > 0 ? kArtX + 640 + 30 + w / 2 : chip.from.x;
				const float edgeX = chip.side < 0 ? cx + w / 2 : chip.side > 0 ? cx - w / 2 : cx;
				const float edgeY = chip.side == 0 ? chip.y + h / 2 : chip.y;
				draw->AddLine(at(chip.from.x, chip.from.y), at(edgeX, edgeY), Fade(on ? c.accent : 0xffffffff, on ? 0.9f : 0.3f), std::max(1.0f, 1.4f * s));
				draw->AddCircleFilled(at(chip.from.x, chip.from.y), 4 * s, Fade(on ? c.accent : 0xffffffff, on ? 1.0f : 0.6f), 12);
				draw->AddRectFilled(at(cx - w / 2, chip.y - h / 2), at(cx + w / 2, chip.y + h / 2), on ? c.accent : Colour(0x1a2236, 0xf0), h / 2 * s);
				draw->AddRect(at(cx - w / 2, chip.y - h / 2), at(cx + w / 2, chip.y + h / 2), on ? Colour(0xffffff, 0xff) : Fade(c.accent, 0.6f),
					h / 2 * s, 0, std::max(1.0f, 1.4f * s));
				canvas.Text(fonts.chip, size, cx - canvas.Measure(fonts.chip, size, label) / 2, chip.y - size * 0.62f, on ? Colour(0x06080f, 0xff) : c.title,
					label);
			}
		}

	private:
		struct Shown
		{
			const Row* row;
			int depth;
			float y; // from the list's top
		};

		// Cross held on a row or quick action that asks for it: what it does once the ring is full
		Action Hold(const std::string& id, uint32_t pressed, uint32_t buttons, uint64_t nowUs)
		{
			if (pressed & ps5pad::kCross)
			{
				m_holding = id;
				m_holdStart = nowUs;
			}
			if (m_holding != id || !(buttons & ps5pad::kCross))
			{
				m_holding.clear();
				m_holdProgress = 0;
				return {};
			}
			m_holdProgress = (float)(nowUs - m_holdStart) / kHoldUs;
			if (m_holdProgress < 1.0f)
				return {};
			m_holding.clear(); // once: Cross is let go and pressed again before it counts again
			m_holdProgress = 0;
			return {id, 1, true};
		}

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
			if (m_onTiles)
				return;
			const float listTop = m_tilesShown ? kTilesTop + kTileHeight + 24 : kTilesTop + 8;
			const float room = kListBottom - listTop, total = shown.back().y + kRowHeight;
			// the focused row, and the one after it where there is one, in view
			const float top = shown[at].y - (at > 0 ? kPitch * 0.5f : 0.0f);
			const float bottom = shown[at].y + kRowHeight + (at + 1 < (int)shown.size() ? kPitch * 0.5f : 0.0f);
			if (top < m_scroll)
				m_scroll = top;
			if (bottom > m_scroll + room)
				m_scroll = bottom - room;
			m_scroll = std::clamp(m_scroll, 0.0f, std::max(0.0f, total - room));
		}

		std::string m_focus;  // the focused row's id
		std::string m_open;	  // the open category's id
		bool m_onTiles = true; // the focus is on the quick actions
		int m_tile = 0;
		std::string m_tileId;
		bool m_tilesShown = true;
		float m_scroll = 0;
		uint32_t m_held = ~0u;
		std::array<uint64_t, 4> m_repeatAt{};
		std::string m_holding; // the row or quick action Cross is held on
		uint64_t m_holdStart = 0;
		float m_holdProgress = 0;
		// what moves on a spring as it is drawn
		mutable float m_scrollShown = 0;
		mutable float m_ring[4] = {};
		mutable bool m_ringValid = false;
	};
}
