// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the launcher's background, the Wii U Homebrew Launcher's, moving: a blue gradient with
// bubbles rising through it, as Dimok's homebrew_launcher draws it (src/menu/MainWindow.cpp
// with libgui's GuiParticleImage, both GPL-3.0-or-later): (59, 159, 223) at the top to
// (79, 153, 239) at the bottom, and 500 bubbles of radius up to 30 and alpha 0.05 to 0.65 on its
// 1280x720 screen, here at 1920x1080. Black at kOverlay's opacity covers it all, so the launcher's
// white text and artwork stand out; it is applied to the gradient and the bubbles alike. The
// bubbles are white with a little of the gradient's blue: under the overlay, white turns a grey
// that looks beige against the blue.
//
// This holds the picture and the bubbles' places; ui_host.cpp draws them under the launcher.

#pragma once

#include <cstdint>
#include <vector>

namespace ps5ui
{
	class Bubbles
	{
	public:
		static constexpr int kWidth = 1920, kHeight = 1080;
		static constexpr int kCount = 500;
		static constexpr int kMaxRadius = 45;  // 30 on the Homebrew Launcher's 1280x720 screen
		static constexpr float kOverlay = 0.6f; // the dark overlay's opacity
		// the bubbles' colour under the overlay, blue, green and red
		static constexpr uint8_t kColour[3] = {(uint8_t)(255 * (1.0f - kOverlay) + 0.5f), (uint8_t)(228 * (1.0f - kOverlay) + 0.5f),
			(uint8_t)(196 * (1.0f - kOverlay) + 0.5f)};

		struct Bubble
		{
			float x, y;		// centre, in pixels
			int radius;		// 1 to kMaxRadius
			float alpha;	// 0.05 to 0.65
			float speed;	// upwards, in pixels per second
			float drift;	// sideways, in pixels per second
		};

		Bubbles();
		// Moves the bubbles on by the time since the last frame. One that leaves at the top comes
		// back in at the bottom, somewhere else.
		void Advance(double seconds);
		const std::vector<Bubble>& List() const { return m_bubbles; }

		// The gradient under the bubbles, darkened: kWidth x kHeight BGRA pixels, top-down.
		static std::vector<uint8_t> Gradient();
		// A bubble of a radius as a square of (2 * radius + 2) pixels a side: each one's coverage,
		// 0 to 255, with its edge smoothed over a pixel.
		static std::vector<uint8_t> Disc(int radius);
		static int DiscSize(int radius) { return 2 * radius + 2; }

	private:
		float Random(); // 0 to 1
		void Respawn(Bubble& bubble);

		uint32_t m_seed = 0x5735;
		std::vector<Bubble> m_bubbles;
	};
}
