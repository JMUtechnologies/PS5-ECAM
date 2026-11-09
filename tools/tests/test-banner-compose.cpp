// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/banner_compose.h"

#include <cstdio>

using namespace ps5boxart;

static int failures = 0;
#define CHECK(what) do { const bool ok_ = (what); std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", #what); failures += !ok_; } while (0)

int main()
{
	// white, 4:3: whole in the middle, the same colour dimmed either side
	const std::vector<uint8_t> white(640 * 480 * 4, 255);
	const auto out = ComposeBanner(white, 640, 480);
	const auto at = [&](int x, int y, int c) { return out[((size_t)y * kBannerWidth + x) * 4 + c]; };
	CHECK(out.size() == (size_t)kBannerWidth * kBannerHeight * 4);
	CHECK(at(640, 360, 0) == 255 && at(160, 360, 0) == 255 && at(1119, 360, 0) == 255);
	CHECK(at(159, 360, 0) == 102 && at(1120, 360, 0) == 102 && at(0, 0, 3) == 255);
	// wider than the screen: bars above and below, nothing out of bounds
	const std::vector<uint8_t> wide(800 * 200 * 4, 255);
	const auto bars = ComposeBanner(wide, 800, 200);
	CHECK(bars[((size_t)360 * kBannerWidth + 640) * 4] == 255 && bars[((size_t)10 * kBannerWidth + 640) * 4] == 102);
	return failures ? 1 : 0;
}
