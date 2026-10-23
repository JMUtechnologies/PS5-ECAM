// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: where the DS's screens go on the TV (screens.h, Place), apart from the Vulkan that draws them
// there, so tools/tests/test-screens.sh can check it on a PC.
#include "screens.h"

#include <algorithm>
#include <cmath>

namespace ps5melonds::screens
{
	void Place(int layout, bool swapped, int rotation, bool gap, float width, float height, Rect& top, Rect& bottom)
	{
		const int turns = QuarterTurns(rotation);
		if (turns != 0)
		{
			// laid out upright on the picture turned back (its sides swapped for a quarter turn), then
			// each corner turned clockwise onto the picture
			const bool sideways = turns % 2 == 1;
			const float uprightWidth = sideways ? height : width, uprightHeight = sideways ? width : height;
			Place(layout, swapped, 0, gap, uprightWidth, uprightHeight, top, bottom);
			auto turn = [&](Rect& rect) {
				if (!rect.Shown())
					return;
				auto point = [&](float x, float y, float& turnedX, float& turnedY) {
					switch (turns)
					{
					case 1: turnedX = uprightHeight - y, turnedY = x; break;
					case 2: turnedX = uprightWidth - x, turnedY = uprightHeight - y; break;
					default: turnedX = y, turnedY = uprightWidth - x; break;
					}
				};
				float x0, y0, x1, y1;
				point(rect.left, rect.top, x0, y0);
				point(rect.right, rect.bottom, x1, y1);
				rect = Rect{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
			};
			turn(top);
			turn(bottom);
			return;
		}
		top = bottom = {};
		Rect* first = swapped ? &bottom : &top;	 // the main screen: the top one, or the bottom one swapped
		Rect* second = swapped ? &top : &bottom;
		constexpr float w = kScreenWidth, h = kScreenHeight;
		const float g = gap ? kGap : 0; // in the DS's pixels, scaled with them
		// the screens' box fitted to the picture and centred, as Azahar's MaxRectangle fits its own: its
		// offsets rounded as its edges are, so its margins match, and never below 0 (a box that fills the
		// picture can come out a hair larger, which floor made a pixel off its edge)
		auto fit = [&](float boxWidth, float boxHeight, float& scale, float& x, float& y) {
			scale = std::min(width / boxWidth, height / boxHeight);
			x = std::max(0.0f, std::round((width - boxWidth * scale) / 2));
			y = std::max(0.0f, std::round((height - boxHeight * scale) / 2));
		};
		auto rect = [](float x, float y, float rectWidth, float rectHeight) {
			return Rect{x, y, std::round(x + rectWidth), std::round(y + rectHeight)};
		};
		float scale, x, y;
		switch (layout)
		{
		case 1: // the main screen alone
			fit(w, h, scale, x, y);
			*first = rect(x, y, w * scale, h * scale);
			break;
		case 2:
		{
			// the main screen large, the other a quarter of its size at its bottom right (Azahar's
			// large_screen_proportion 4, small_screen_position BottomRight)
			constexpr float kProportion = 4.0f;
			fit(w + g + w / kProportion, h, scale, x, y);
			*first = rect(x, y, w * scale, h * scale);
			const float smallWidth = w / kProportion * scale, smallHeight = h / kProportion * scale;
			*second = Rect{std::round(x + (w + g) * scale), first->bottom - std::round(smallHeight), std::round(x + (w + g) * scale + smallWidth),
				first->bottom};
			break;
		}
		case 3: // side by side, the main one on the left
			fit(w * 2 + g, h, scale, x, y);
			*first = rect(x, y, w * scale, h * scale);
			// its edges from the whole box's, so that rounding never takes it past the picture
			*second = Rect{std::round(x + (w + g) * scale), y, std::round(x + (2 * w + g) * scale), std::round(y + h * scale)};
			break;
		default: // stacked, the main one above
			fit(w, h * 2 + g, scale, x, y);
			*first = rect(x, y, w * scale, h * scale);
			*second = Rect{x, std::round(y + (h + g) * scale), std::round(x + w * scale), std::round(y + (2 * h + g) * scale)};
			break;
		}
	}
}
