// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: where the DS's screens go on the TV (port/melonds/screen_place.cpp) on a PC, run by
// test-screens.sh: every layout, swapped or not, turned 0 to 3 quarter turns, with and without the gap,
// on pictures of several sizes. Each screen inside the picture and in the DS's shape, the two apart,
// centred and as large as fits; the gap only where it is on; swapped and turned as they should be.
#include "melonds/screens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace ps5melonds::screens;

static int failures = 0, checks = 0;
static void Check(bool ok, const char* what, int layout, bool swapped, int rotation, bool gap, float width, float height)
{
	checks++;
	if (ok)
		return;
	failures++;
	if (failures <= 20)
		std::printf("FAIL  %s: layout %d%s, %d quarter turns, %s, %gx%g\n", what, layout, swapped ? " swapped" : "", rotation,
			gap ? "gap" : "no gap", width, height);
}

static bool Near(float a, float b, float tolerance = 1.01f)
{
	return std::fabs(a - b) <= tolerance;
}

int main()
{
	const float pictures[][2] = {{3840, 2160}, {1920, 1080}, {1280, 720}, {1366, 768}, {2560, 1600}, {1000, 1000}, {720, 1280}};
	for (const auto& picture : pictures)
	{
		const float width = picture[0], height = picture[1];
		for (int layout = 0; layout < 4; layout++)
			for (int rotation = 0; rotation < 4; rotation++)
				for (int gap = 0; gap < 2; gap++)
					for (int swapped = 0; swapped < 2; swapped++)
					{
						Rect top, bottom;
						Place(layout, swapped, rotation, gap, width, height, top, bottom);
						auto check = [&](bool ok, const char* what) { Check(ok, what, layout, swapped, rotation, gap, width, height); };
						const Rect* main = swapped ? &bottom : &top;
						const Rect* other = swapped ? &top : &bottom;
						check(main->Shown(), "the main screen shows");
						check(other->Shown() == (layout != 1), "the other screen shows, but in the main screen alone");
						const bool sideways = rotation % 2 == 1;
						Rect box = *main;
						for (const Rect* rect : {&top, &bottom})
						{
							if (!rect->Shown())
								continue;
							check(rect->left >= 0 && rect->top >= 0 && rect->right <= width && rect->bottom <= height, "inside the picture");
							// 256 x 192, or 192 x 256 turned a quarter
							const float across = sideways ? rect->Height() : rect->Width(), down = sideways ? rect->Width() : rect->Height();
							check(Near(across * kScreenHeight, down * kScreenWidth, 2.0f * kScreenWidth), "in the DS's shape");
							box = Rect{std::min(box.left, rect->left), std::min(box.top, rect->top), std::max(box.right, rect->right),
								std::max(box.bottom, rect->bottom)};
						}
						if (top.Shown() && bottom.Shown())
							check(top.right <= bottom.left || bottom.right <= top.left || top.bottom <= bottom.top || bottom.bottom <= top.top,
								"the screens do not overlap");
						check(Near(box.left, width - box.right, 1.5f) && Near(box.top, height - box.bottom, 1.5f), "centred");
						check(Near(box.Width(), width, 1.5f) || Near(box.Height(), height, 1.5f), "as large as fits");

						// the space between the two along the way they are laid out
						if (layout != 1)
						{
							float space = std::max({other->left - main->right, main->left - other->right, other->top - main->bottom,
								main->top - other->bottom});
							if (gap)
								check(space >= 2, "the gap between them");
							else
								check(Near(space, 0), "no gap, the screens together");
						}

						// swapped: the screens trade places
						Rect unswappedTop, unswappedBottom;
						Place(layout, !swapped, rotation, gap, width, height, unswappedTop, unswappedBottom);
						auto same = [](const Rect& a, const Rect& b) {
							return a.Shown() == b.Shown() && (!a.Shown() || (a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom));
						};
						check(same(top, unswappedBottom) && same(bottom, unswappedTop), "swapped: the screens trade places");

						// two quarter turns: the picture upside down
						if (rotation >= 2)
						{
							Rect turnedTop, turnedBottom;
							Place(layout, swapped, rotation - 2, gap, width, height, turnedTop, turnedBottom);
							auto upsideDown = [&](const Rect& a, const Rect& b) {
								return a.Shown() == b.Shown() && (!a.Shown() || (Near(a.left, width - b.right, 1.5f) && Near(a.top, height - b.bottom, 1.5f) &&
																					Near(a.right, width - b.left, 1.5f) && Near(a.bottom, height - b.top, 1.5f)));
							};
							check(upsideDown(top, turnedTop) && upsideDown(bottom, turnedBottom), "a half turn more: upside down");
						}
					}
	}

	// a few places known exactly, on a 4K TV
	Rect top, bottom;
	Place(0, false, 0, false, 3840, 2160, top, bottom);
	Check(top.top == 0 && bottom.bottom == 2160 && top.bottom == bottom.top && Near(top.Width(), 1440), "stacked fills the height", 0, false, 0,
		false, 3840, 2160);
	Place(0, false, 0, true, 3840, 2160, top, bottom);
	Check(Near(bottom.top - top.bottom, std::round(kGap * 2160 / (2 * kScreenHeight + kGap)), 1.5f), "stacked: the gap is kGap of the DS's pixels",
		0, false, 0, true, 3840, 2160);
	// book style: stacked, a quarter turn, the top screen on the right
	Place(0, false, 1, true, 3840, 2160, top, bottom);
	Check(top.left > bottom.right && top.top == bottom.top, "turned like a book: side by side, the top screen right", 0, false, 1, true, 3840,
		2160);
	Check(top.left - bottom.right >= 2, "turned like a book: the gap between them", 0, false, 1, true, 3840, 2160);

	std::printf("%s  %d checks of the DS's screens' places%s\n", failures ? "FAIL" : "PASS", checks, failures ? "" : ", all as they should be");
	return failures ? 1 : 0;
}
