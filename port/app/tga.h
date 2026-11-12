// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the app's own TGAs (the box art and game icons the launcher shows: uncompressed,
// 24 or 32 bits, either way up) as RGBA, scaled down to a size, for the in-game menus to show.
// The Library shows PNGs and JPEGs too (a player's own picture beside a game): with a decoder for
// them (the launcher's stb_image, ui::DecodeFile) they are read and scaled the same way.

#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace ps5tga
{
	// RGBA rows, top row first, no larger than maxWidth x maxHeight (each pixel the average of those it
	// covers). in(x, y) is the source's pixel, as R, G, B, A.
	template <class Pixel>
	inline void Shrink(int w, int h, int maxWidth, int maxHeight, Pixel in, std::vector<uint8_t>& rgba, int& width, int& height)
	{
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
						uint8_t pixel[4];
						in(sx, sy, pixel);
						for (int c = 0; c < 4; c++)
							sum[c] += pixel[c];
					}
				const uint32_t count = (uint32_t)((y1 - y0) * (x1 - x0));
				uint8_t* out = &rgba[((size_t)y * width + x) * 4];
				for (int c = 0; c < 4; c++)
					out[c] = (uint8_t)((sum[c] + count / 2) / count);
			}
		}
	}

	// RGBA, top row first, no larger than maxWidth x maxHeight. False when the file is not such a TGA.
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
		Shrink(w, h, maxWidth, maxHeight, [&](int x, int y, uint8_t* pixel) {
			const uint8_t* in = &data[start + ((size_t)(topFirst ? y : h - 1 - y) * w + x) * bytes];
			pixel[0] = in[2], pixel[1] = in[1], pixel[2] = in[0], pixel[3] = bytes == 4 ? in[3] : 255;
		}, rgba, width, height);
		return true;
	}

	// A picture file as RGBA rows (ui::DecodeFile's shape)
	using Decoder = bool (*)(const std::string& path, std::vector<uint8_t>& rgba, int& width, int& height);

	// The same, any picture decode reads when it is no such TGA (the Library's PNGs and JPEGs)
	inline bool Load(const std::string& path, int maxWidth, int maxHeight, std::vector<uint8_t>& rgba, int& width, int& height, Decoder decode)
	{
		if (Load(path, maxWidth, maxHeight, rgba, width, height))
			return true;
		std::vector<uint8_t> full;
		int w = 0, h = 0;
		if (!decode || path.empty() || !decode(path, full, w, h) || w <= 0 || h <= 0 || full.size() < (size_t)w * h * 4)
			return false;
		Shrink(w, h, maxWidth, maxHeight, [&](int x, int y, uint8_t* pixel) {
			std::copy_n(&full[((size_t)y * w + x) * 4], 4, pixel);
		}, rgba, width, height);
		return true;
	}

	// RGBA rows as the TGA Load reads (32 bits, top row first). False when it could not be written.
	inline bool Save(const std::string& path, const std::vector<uint8_t>& rgba, int width, int height)
	{
		if (width <= 0 || height <= 0 || width > 0xffff || height > 0xffff || rgba.size() < (size_t)width * height * 4)
			return false;
		std::vector<uint8_t> tga(18 + (size_t)width * height * 4);
		tga[2] = 2;
		tga[12] = width & 255, tga[13] = width >> 8, tga[14] = height & 255, tga[15] = height >> 8;
		tga[16] = 32, tga[17] = 0x28;
		for (size_t i = 0; i < (size_t)width * height; i++)
		{
			tga[18 + i * 4 + 0] = rgba[i * 4 + 2];
			tga[18 + i * 4 + 1] = rgba[i * 4 + 1];
			tga[18 + i * 4 + 2] = rgba[i * 4 + 0];
			tga[18 + i * 4 + 3] = rgba[i * 4 + 3];
		}
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write((const char*)tga.data(), (std::streamsize)tga.size());
		return (bool)out;
	}
}
