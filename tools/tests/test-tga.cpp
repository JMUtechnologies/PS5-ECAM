// SPDX-License-Identifier: GPL-3.0-or-later
// port/app/tga.h: what the in-game menus show of a game's box art (tools/tests/test-tga.sh)
#include "app/tga.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	int s_failures = 0;

	void Check(bool ok, const char* what)
	{
		if (!ok)
		{
			std::fprintf(stderr, "FAIL: %s\n", what);
			s_failures++;
		}
	}

	// a 4x2 picture: the top row red, the bottom blue, its last column see-through
	std::vector<uint8_t> Picture()
	{
		std::vector<uint8_t> rgba(4 * 2 * 4);
		for (int y = 0; y < 2; y++)
			for (int x = 0; x < 4; x++)
			{
				uint8_t* p = &rgba[(y * 4 + x) * 4];
				p[0] = y == 0 ? 255 : 0, p[1] = 0, p[2] = y == 0 ? 0 : 255, p[3] = x == 3 ? 0 : 255;
			}
		return rgba;
	}

	// a PNG or JPEG, as ui::DecodeFile reads one: here, any file named *.png
	bool Decode(const std::string& path, std::vector<uint8_t>& rgba, int& width, int& height)
	{
		if (path.size() < 4 || path.compare(path.size() - 4, 4, ".png") != 0)
			return false;
		rgba = Picture();
		width = 4, height = 2;
		return true;
	}
}

int main(int argc, char* argv[])
{
	const std::string work = argc > 1 ? argv[1] : ".";
	std::vector<uint8_t> rgba;
	int width = 0, height = 0;

	// written and read back as it was
	const std::string tga = work + "/cover.tga";
	Check(ps5tga::Save(tga, Picture(), 4, 2), "a TGA is written");
	Check(ps5tga::Load(tga, 100, 100, rgba, width, height), "the TGA is read");
	Check(width == 4 && height == 2 && rgba == Picture(), "the TGA reads back as written, top row first");

	// scaled down: each pixel the average of those it covers
	Check(ps5tga::Load(tga, 2, 2, rgba, width, height) && width == 2 && height == 1, "the TGA is scaled to fit 2x2");
	Check(rgba.size() == 8 && rgba[0] == 128 && rgba[2] == 128 && rgba[3] == 255 && rgba[7] == 128, "each pixel the average of those it covers");

	// a PNG: not a TGA, read by the decoder when there is one
	const std::string png = work + "/cover.png";
	{
		std::FILE* file = std::fopen(png.c_str(), "wb");
		std::fputs("\x89PNG not really", file);
		std::fclose(file);
	}
	Check(!ps5tga::Load(png, 100, 100, rgba, width, height), "a PNG is no TGA");
	Check(!ps5tga::Load(png, 100, 100, rgba, width, height, nullptr), "a PNG without a decoder is not read");
	Check(ps5tga::Load(png, 100, 100, rgba, width, height, Decode) && width == 4 && height == 2 && rgba == Picture(), "a PNG is read by the decoder");
	Check(ps5tga::Load(png, 2, 1, rgba, width, height, Decode) && width == 2 && height == 1, "a decoded PNG is scaled down too");
	Check(ps5tga::Load(tga, 100, 100, rgba, width, height, Decode) && rgba == Picture(), "a TGA with a decoder is still read as a TGA");
	Check(!ps5tga::Load(work + "/missing.tga", 100, 100, rgba, width, height, Decode), "a missing file is not read");
	Check(!ps5tga::Load("", 100, 100, rgba, width, height, Decode), "no path is not read");

	// a decoded picture made a TGA (the Switch side's cover for Eden's menu)
	Check(ps5tga::Load(png, 512, 704, rgba, width, height, Decode) && ps5tga::Save(work + "/copy.tga", rgba, width, height), "a PNG is made a TGA");
	Check(ps5tga::Load(work + "/copy.tga", 512, 704, rgba, width, height) && rgba == Picture(), "the copy reads as the PNG");
	Check(!ps5tga::Save(work + "/none.tga", {}, 0, 0), "nothing is not written");

	if (s_failures)
		return EXIT_FAILURE;
	std::printf("tga: all passed\n");
	return 0;
}
