// SPDX-License-Identifier: GPL-3.0-or-later
#include "display.h"
#include "log.h"

#include <algorithm>
#include <atomic>

// RADV's VideoOut display (PS5_Mesa 7b59ef2 and later): sceVideoOutSetFlipRate's rate, a flip showing
// for at least rate + 1 vblanks. Weak, so a driver from before it still links, and pacing is then
// not offered.
extern "C" int wsi_videoout_set_flip_rate(int rate) __attribute__((weak));
// VideoOut's mode chosen again once no swapchain presents on it (patch 0006): 1 at 119.88 Hz, 0 at
// 59.94 Hz, -1 when VideoOut is not open yet (it settles as it opens) or still presenting.
extern "C" int wsi_videoout_set_high_frame_rate(bool high) __attribute__((weak));

namespace
{
	std::atomic<bool> s_highFrameRate{false};
	std::atomic<int> s_framePacing{1};
	std::atomic<uint32_t> s_outputRefresh{0};
	std::atomic<bool> s_frameGeneration{false}; // SetFrameGenerationActive
	std::atomic<int> s_frameGenerationRefreshes{1}; // each of its pictures', while it runs
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

	void ConfigureOutput(bool highFrameRate)
	{
		if (!wsi_videoout_set_high_frame_rate)
			return;
		const int result = wsi_videoout_set_high_frame_rate(highFrameRate);
		if (result >= 0)
			ps5log::Line("[vulkan] VideoOut configured again for the next surface: {} Hz{}", result ? "119.88" : "59.94",
				highFrameRate && !result ? " (120 Hz output is on, but the display or VideoOut refused it)" : "");
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
		// (while frame generation presents two pictures a frame, each shows for half the frame)
		const int result = wsi_videoout_set_flip_rate((s_frameGeneration ? s_frameGenerationRefreshes.load() : refreshes) - 1);
		if (changed || result != 0)
			ps5log::Line("[vulkan] frame pacing: each frame shown for at least {} refresh{}{}", refreshes, refreshes == 1 ? "" : "es",
				result == 0 ? "" : fmt::format(" (VideoOut refused it: {:#010x})", (unsigned)result));
		return result == 0;
	}

	int FramePacing()
	{
		return s_framePacing;
	}

	void SetFrameGenerationActive(bool active, int refreshes)
	{
		refreshes = std::clamp(refreshes, 1, 3);
		// what VideoOut holds each picture for, before and now
		const int before = s_frameGeneration.exchange(active) ? s_frameGenerationRefreshes.load() : s_framePacing.load();
		s_frameGenerationRefreshes = refreshes;
		const int now = active ? refreshes : s_framePacing.load();
		if (now == before || !wsi_videoout_set_flip_rate)
			return;
		const int result = wsi_videoout_set_flip_rate(now - 1);
		if (result != 0)
			ps5log::Line("[vulkan] frame generation: VideoOut refused its flip rate: {:#010x}", (unsigned)result);
	}
}
