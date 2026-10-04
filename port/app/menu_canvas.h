// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the in-game menus' look, the Wii U's (ingame.cpp) and the 3DS's (ingame3ds.cpp): the
// launcher's panels, rows, colours and controller hints (its stylesheet, frontend/ui/har.rcss, in the
// colours tools/render-layout.py gives each side), drawn with ImGui on the launcher's 1920x1080
// layout scaled to the screen, so a menu over a game is laid out as the launcher's screens are.

#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <cstdint>
#include <cstring>
#include <string>

namespace ps5menu
{
	constexpr ImU32 Colour(uint32_t rgb, uint8_t alpha = 255)
	{
		return IM_COL32(rgb >> 16, (rgb >> 8) & 255, rgb & 255, alpha);
	}

	// A side's colours, the launcher's (render-layout.py's THEMES): the Wii U's blue, or the 3DS's gold
	struct Palette
	{
		ImU32 title, text, copy, accent, kicker, line;
		ImU32 panel, panelEdge, row, rowEdge, focus, focusEdge, dim;
	};
	constexpr Palette kBlue{Colour(0xf3f7ff), Colour(0xf3f7ff), Colour(0xa9bcd6), Colour(0x5aa9ff), Colour(0x5aa9ff), Colour(0xffffff, 0x1e),
		Colour(0x07101f, 0xf0), Colour(0xffffff, 0x1e), Colour(0xffffff, 0x10), Colour(0xffffff, 0x0c), Colour(0x5aa9ff, 0x28),
		Colour(0x5aa9ff), Colour(0x02060e, 0xb8)};
	constexpr Palette kGold{Colour(0xfff7e8), Colour(0xfff7e8), Colour(0xd8c6a3), Colour(0xf4b63f), Colour(0xf4b63f), Colour(0xffffff, 0x1e),
		Colour(0x1a1206, 0xf0), Colour(0xffffff, 0x1e), Colour(0xffffff, 0x10), Colour(0xffffff, 0x0c), Colour(0xf4b63f, 0x28),
		Colour(0xf4b63f), Colour(0x0e0902, 0xb8)};

	struct Canvas
	{
		ImDrawList* draw;
		float scale;
		ImVec2 origin;
		const Palette& colours = kBlue;

		ImVec2 At(float x, float y) const { return {origin.x + x * scale, origin.y + y * scale}; }

		void Panel(float x, float y, float width, float height) const
		{
			draw->AddRectFilled(At(x + 2, y + 2), At(x + width - 3, y + height - 3), colours.panel, 28 * scale);
			draw->AddRect(At(x + 2, y + 2), At(x + width - 3, y + height - 3), colours.panelEdge, 28 * scale, 0, 2 * scale);
		}

		// A row as the launcher's: a faint card, or the accent's tint inside an accent outline
		void Row(float x, float y, float width, float height, bool focused) const
		{
			const ImVec2 a = At(x + 2, y + 2), b = At(x + width - 3, y + height - 3);
			draw->AddRectFilled(a, b, focused ? colours.focus : colours.row, 16 * scale);
			if (focused)
				draw->AddRect(a, b, colours.focusEdge, 16 * scale, 0, 3 * scale);
			else
				draw->AddRect(a, b, colours.rowEdge, 16 * scale, 0, scale);
		}

		void Text(ImFont* font, float size, float x, float y, ImU32 colour, const std::string& text, float wrap = 0.0f) const
		{
			draw->AddText(font, size * scale, At(x, y), colour, text.c_str(), nullptr, wrap * scale);
		}

		void TextRight(ImFont* font, float size, float right, float y, ImU32 colour, const std::string& text) const
		{
			const float width = font->CalcTextSizeA(size * scale, FLT_MAX, 0.0f, text.c_str()).x / scale;
			Text(font, size, right - width, y, colour, text);
		}

		// A controller hint: its button's mark, as the launcher's mono icons have it, and what it
		// does. Returns where the next one goes.
		float Hint(ImFont* font, float x, float y, const char* button, const std::string& label) const
		{
			const ImU32 colour = colours.copy;
			const float thick = 2.2f * scale;
			const ImVec2 centre = At(x + 13, y + 14);
			const float r = 10 * scale;
			if (std::strcmp(button, "cross") == 0)
			{
				draw->AddLine({centre.x - r, centre.y - r}, {centre.x + r, centre.y + r}, colour, thick);
				draw->AddLine({centre.x - r, centre.y + r}, {centre.x + r, centre.y - r}, colour, thick);
			}
			else if (std::strcmp(button, "circle") == 0)
				draw->AddCircle(centre, r, colour, 0, thick);
			else if (std::strcmp(button, "leftright") == 0)
			{
				draw->AddLine({centre.x - r - 2 * scale, centre.y}, {centre.x + r + 2 * scale, centre.y}, colour, thick);
				for (const float side : {-1.0f, 1.0f})
				{
					const ImVec2 tip{centre.x + side * (r + 2 * scale), centre.y};
					draw->AddLine(tip, {tip.x - side * 6 * scale, centre.y - 6 * scale}, colour, thick);
					draw->AddLine(tip, {tip.x - side * 6 * scale, centre.y + 6 * scale}, colour, thick);
				}
			}
			else if (std::strcmp(button, "touchpad") == 0)
				draw->AddRect({centre.x - r - 3 * scale, centre.y - r + 3 * scale}, {centre.x + r + 3 * scale, centre.y + r - 3 * scale},
					colour, 3 * scale, 0, thick);
			else if (std::strcmp(button, "triangle") == 0)
				draw->AddTriangle({centre.x, centre.y - r}, {centre.x + r, centre.y + r * 0.75f}, {centre.x - r, centre.y + r * 0.75f}, colour, thick);
			else if (std::strcmp(button, "options") == 0)
				for (const float line : {-1.0f, 0.0f, 1.0f}) // the Options button's three lines
					draw->AddLine({centre.x - r * 0.7f, centre.y + line * 5 * scale}, {centre.x + r * 0.7f, centre.y + line * 5 * scale}, colour, thick);
			Text(font, 20, x + 38, y + 2, colour, label);
			return x + 38 + font->CalcTextSizeA(20 * scale, FLT_MAX, 0.0f, label.c_str()).x / scale + 44;
		}
	};

	// The touchpad's cursor
	inline void DrawCursor(ImDrawList* draw, ImVec2 at, bool pressed, float scale)
	{
		const float radius = 12.0f * scale;
		if (pressed)
			draw->AddCircleFilled(at, radius, IM_COL32(255, 255, 255, 170));
		draw->AddCircle(at, radius + 2.0f * scale, IM_COL32(0, 0, 0, 200), 0, 3.0f * scale);
		draw->AddCircle(at, radius, IM_COL32(255, 255, 255, 255), 0, 2.5f * scale);
	}
}
