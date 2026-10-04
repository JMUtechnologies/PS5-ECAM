// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the app's own TGAs (the box art and game icons the launcher shows: uncompressed,
// 24 or 32 bits, either way up) as RGBA, scaled down to a size, for the in-game menus to show.

#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace ps5tga
{
	// RGBA, top row first, no larger than maxWidth x maxHeight (each pixel the average of those it
	// covers). False when the file is not such a TGA.
	inline bool Load(const std::string& path, int maxWidth, int maxHeight, std::vector<uint8_t>& rgba, int& width, int& height)
	{
		std::ifstream file(path, std::ios::binary);
		const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (data.size() < 18 || data[2] != 2 || (data[16] != 24 && data[16] != 32))
			return false;
		const int bytes = data[16] / 8;
		const int w = data[12] | data[13] << 8, h = data[14] | data[15] << 8;
		const size_t start = 18 + data[0];
		if (w <= 0 || h <= 0 || data.size() < start + (size_t)w * h * bytes)
			return false;
		const bool topFirst = data[17] & 0x20;
		const double scale = std::min({1.0, (double)maxWidth / w, (double)maxHeight / h});
		width = std::max(1, (int)(w * scale + 0.5));
		height = std::max(1, (int)(h * scale + 0.5));
		rgba.assign((size_t)width * height * 4, 0);
		for (int y = 0; y < height; y++)
		{
			const int y0 = (int)((int64_t)y * h / height), y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * h / height));
			for (int x = 0; x < width; x++)
			{
				const int x0 = (int)((int64_t)x * w / width), x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * w / width));
				uint32_t sum[4] = {};
				for (int sy = y0; sy < y1; sy++)
					for (int sx = x0; sx < x1; sx++)
					{
						const uint8_t* in = &data[start + ((size_t)(topFirst ? sy : h - 1 - sy) * w + sx) * bytes];
						sum[0] += in[2], sum[1] += in[1], sum[2] += in[0], sum[3] += bytes == 4 ? in[3] : 255;
					}
				const uint32_t count = (uint32_t)((y1 - y0) * (x1 - x0));
				uint8_t* out = &rgba[((size_t)y * width + x) * 4];
				for (int c = 0; c < 4; c++)
					out[c] = (uint8_t)((sum[c] + count / 2) / count);
			}
		}
		return true;
	}
}
