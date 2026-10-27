// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
namespace ps5melonds::input
{
	// Display-space normalized coordinate. Defaults preserve the existing mapping exactly.
	inline float Calibrate(float value, int offsetPercent, int scalePercent)
	{
		if (!std::isfinite(value)) return 0.5f;
		if (offsetPercent == 0 && scalePercent == 100) return std::clamp(value, 0.0f, 1.0f);
		return std::clamp((value - 0.5f) * (std::clamp(scalePercent, 50, 150) / 100.0f) +
			0.5f + std::clamp(offsetPercent, -20, 20) / 100.0f, 0.0f, 1.0f);
	}
}
