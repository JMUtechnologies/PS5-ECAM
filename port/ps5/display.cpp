// SPDX-License-Identifier: GPL-3.0-or-later
#include "display.h"
#include "log.h"

#include <algorithm>
#include <atomic>

// RADV's VideoOut display (PS5_Mesa 7b59ef2 and later): sceVideoOutSetFlipRate's rate, a flip showing
// for at least rate + 1 vblanks. Weak, so a driver from before it still links, and pacing is then
// not offered.
extern "C" int wsi_videoout_set_flip_rate(int rate) __attribute__((weak));

namespace
{
	std::atomic<bool> s_highFrameRate{false};
	std::atomic<int> s_framePacing{1};
	std::atomic<uint32_t> s_outputRefresh{0};
}

namespace ps5display
{
	void SetHighFrameRate(bool highFrameRate)
	{
		s_highFrameRate = highFrameRate;
	}

	bool HighFrameRate()
	{
		return s_highFrameRate;
	}

	void SetOutputRefresh(uint32_t millihertz)
	{
		s_outputRefresh = millihertz;
	}

	uint32_t OutputRefresh()
	{
		return s_outputRefresh;
	}

	bool SetFramePacing(int refreshes)
	{
		refreshes = std::clamp(refreshes, 1, 3);
		const bool changed = s_framePacing.exchange(refreshes) != refreshes;
		if (!wsi_videoout_set_flip_rate)
		{
			if (changed && refreshes > 1)
				ps5log::Line("[vulkan] frame pacing: this driver has none (PS5_Mesa 7b59ef2 or later has it)");
			return false;
		}
		const int result = wsi_videoout_set_flip_rate(refreshes - 1);
		if (changed || result != 0)
			ps5log::Line("[vulkan] frame pacing: each frame shown for at least {} refresh{}{}", refreshes, refreshes == 1 ? "" : "es",
				result == 0 ? "" : fmt::format(" (VideoOut refused it: {:#010x})", (unsigned)result));
		return result == 0;
	}

	int FramePacing()
	{
		return s_framePacing;
	}
}
