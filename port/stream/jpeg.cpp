// SPDX-License-Identifier: GPL-3.0-or-later
#include "jpeg.h"

#include <turbojpeg.h>

#include <algorithm>

namespace ps5stream
{
	std::vector<uint8_t> EncodeJpeg(const uint8_t* rgb, int width, int height)
	{
		if (!rgb || width <= 0 || height <= 0)
			return {};
		// a larger picture (a GamePad drawn at 4K) made 854 x 480 at most, by nearest pixels
		const double scale = std::min({1.0, 854.0 / width, 480.0 / height});
		const int w = std::max(1, (int)(width * scale)), h = std::max(1, (int)(height * scale));
		std::vector<uint8_t> resized;
		const uint8_t* pixels = rgb;
		if (w != width || h != height)
		{
			resized.resize((size_t)w * h * 3);
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
					std::copy_n(rgb + ((size_t)(y * height / h) * width + (size_t)x * width / w) * 3, 3, &resized[((size_t)y * w + x) * 3]);
			pixels = resized.data();
		}
		tjhandle encoder = tjInitCompress();
		if (!encoder)
			return {};
		unsigned char* output = nullptr;
		unsigned long size = 0;
		std::vector<uint8_t> jpeg;
		if (tjCompress2(encoder, pixels, w, 0, h, TJPF_RGB, &output, &size, TJSAMP_420, 75, TJFLAG_FASTDCT) == 0 && output && size)
			jpeg.assign(output, output + size);
		tjFree(output);
		tjDestroy(encoder);
		return jpeg;
	}
}
