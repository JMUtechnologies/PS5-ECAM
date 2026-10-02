// SPDX-License-Identifier: GPL-3.0-or-later
#include "bubbles.h"

#include <algorithm>
#include <cmath>

namespace ps5ui
{
	namespace
	{
		constexpr uint8_t kTop[3] = {59, 159, 223}, kBottom[3] = {79, 153, 239}; // RGB
		constexpr float kMinSpeed = 14.0f, kMaxSpeed = 52.0f, kMaxDrift = 7.0f;
	}

	Bubbles::Bubbles()
	{
		m_bubbles.resize(kCount);
		for (Bubble& bubble : m_bubbles)
		{
			Respawn(bubble);
			bubble.y = Random() * kHeight; // the first ones are everywhere, not only at the bottom
		}
	}

	float Bubbles::Random()
	{
		// xorshift32: the same bubbles every time
		m_seed ^= m_seed << 13;
		m_seed ^= m_seed >> 17;
		m_seed ^= m_seed << 5;
		return (m_seed >> 8) / 16777216.0f;
	}

	void Bubbles::Respawn(Bubble& bubble)
	{
		bubble.radius = 1 + (int)(Random() * kMaxRadius);
		bubble.x = Random() * kWidth;
		bubble.y = kHeight + bubble.radius;
		bubble.alpha = Random() * 0.6f + 0.05f;
		// the larger ones a little faster, as if nearer
		bubble.speed = kMinSpeed + (kMaxSpeed - kMinSpeed) * (0.5f * Random() + 0.5f * bubble.radius / kMaxRadius);
		bubble.drift = (Random() * 2.0f - 1.0f) * kMaxDrift;
	}

	void Bubbles::Advance(double seconds)
	{
		const float step = (float)std::clamp(seconds, 0.0, 0.1); // a stall does not make them jump
		for (Bubble& bubble : m_bubbles)
		{
			bubble.y -= bubble.speed * step;
			bubble.x += bubble.drift * step;
			if (bubble.y < -bubble.radius)
				Respawn(bubble);
			else if (bubble.x < -bubble.radius)
				bubble.x = kWidth + bubble.radius;
			else if (bubble.x > kWidth + bubble.radius)
				bubble.x = -bubble.radius;
		}
	}

	std::vector<uint8_t> Bubbles::Gradient()
	{
		std::vector<uint8_t> pixels((size_t)kWidth * kHeight * 4);
		for (int y = 0; y < kHeight; y++)
		{
			const float t = y / (float)(kHeight - 1);
			uint8_t bgra[4];
			for (int c = 0; c < 3; c++)
			{
				const float value = kTop[c] + (kBottom[c] - kTop[c]) * t;
				bgra[2 - c] = (uint8_t)std::lround(value * (1.0f - kOverlay));
			}
			bgra[3] = 255;
			uint8_t* row = pixels.data() + (size_t)y * kWidth * 4;
			for (int x = 0; x < kWidth; x++)
				std::copy(bgra, bgra + 4, row + x * 4);
		}
		return pixels;
	}

	std::vector<uint8_t> Bubbles::Disc(int radius)
	{
		const int size = DiscSize(radius);
		const float centre = size / 2.0f;
		std::vector<uint8_t> coverage((size_t)size * size);
		for (int y = 0; y < size; y++)
			for (int x = 0; x < size; x++)
			{
				const float distance = std::hypot(x + 0.5f - centre, y + 0.5f - centre);
				const float value = std::clamp(radius - distance + 0.5f, 0.0f, 1.0f);
				coverage[(size_t)y * size + x] = (uint8_t)std::lround(value * 255.0f);
			}
		return coverage;
	}
}
