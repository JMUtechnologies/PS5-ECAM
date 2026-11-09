// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a 4:3 title screen on a 16:9 screen: the picture whole in the middle, over a dimmed, blurred
// copy of itself. GameCube and Wii games' loading picture, as a Wii U game's boot screen (boxart.h).

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ps5boxart
{
	// RGBA in, RGBA out, sampled smoothly: a very small picture made large reads as a blur
	inline std::vector<uint8_t> ResizeSmooth(const std::vector<uint8_t>& in, int width, int height, int outWidth, int outHeight)
	{
		std::vector<uint8_t> out((size_t)outWidth * outHeight * 4);
		for (int y = 0; y < outHeight; y++)
			for (int x = 0; x < outWidth; x++)
			{
				const float fx = std::clamp((x + 0.5f) * width / outWidth - 0.5f, 0.0f, width - 1.0f);
				const float fy = std::clamp((y + 0.5f) * height / outHeight - 0.5f, 0.0f, height - 1.0f);
				const int x0 = (int)fx, y0 = (int)fy, x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
				const float ax = fx - x0, ay = fy - y0;
				for (int c = 0; c < 4; c++)
				{
					const auto at = [&](int px, int py) { return (float)in[((size_t)py * width + px) * 4 + c]; };
					const float top = at(x0, y0) * (1 - ax) + at(x1, y0) * ax, bottom = at(x0, y1) * (1 - ax) + at(x1, y1) * ax;
					out[((size_t)y * outWidth + x) * 4 + c] = (uint8_t)(top * (1 - ay) + bottom * ay + 0.5f);
				}
			}
		return out;
	}

	inline constexpr int kBannerWidth = 1280, kBannerHeight = 720;

	inline std::vector<uint8_t> ComposeBanner(const std::vector<uint8_t>& rgba, int width, int height)
	{
		// the backdrop: down to next to nothing, up again, dimmed
		std::vector<uint8_t> out = ResizeSmooth(ResizeSmooth(rgba, width, height, 32, 18), 32, 18, kBannerWidth, kBannerHeight);
		for (size_t i = 0; i < out.size(); i += 4)
		{
			for (int c = 0; c < 3; c++)
				out[i + c] = (uint8_t)(out[i + c] * 2 / 5);
			out[i + 3] = 255;
		}
		// the picture, as large as it goes without cutting any of it
		const int fitWidth = std::min(kBannerWidth, kBannerHeight * width / height);
		const int fitHeight = std::min(kBannerHeight, fitWidth * height / width);
		const std::vector<uint8_t> whole = ResizeSmooth(rgba, width, height, fitWidth, fitHeight);
		const int left = (kBannerWidth - fitWidth) / 2, top = (kBannerHeight - fitHeight) / 2;
		for (int y = 0; y < fitHeight; y++)
			std::copy_n(&whole[(size_t)y * fitWidth * 4], (size_t)fitWidth * 4, &out[((size_t)(y + top) * kBannerWidth + left) * 4]);
		return out;
	}
}
