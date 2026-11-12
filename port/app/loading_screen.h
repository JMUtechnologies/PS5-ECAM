// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the loading screen a game starts behind on every side whose emulator makes its shaders
// ready first, the Switch's first: the game's cover on its own colours blurred, the system, the
// game's name, GameTDB's publisher and year, then what the emulator is doing, with a bar for the
// shaders, and a short fade into the game's first frames. Drawn with ImGui on the menus' 1920x1080
// layout (menu_canvas.h): by Eden's program for the Switch and Switch 2 (port/eden/ingame.cpp), into
// Cemu's frames for the Wii U (ingame.cpp, in place of Cemu's shader cache screen) and into Azahar's
// for the 3DS (ingame3ds.cpp, while its shader cache loads). Each side makes its fonts and pictures
// its own way and hands them in; the drawing is this one.

#pragma once

#include "lang.h"
#include "menu_canvas.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace ps5menu
{
	// A picture made by the side's ImGui backend
	struct LoadingPicture
	{
		ImTextureID id = nullptr;
		int width = 0, height = 0;
	};

	// The type it draws with, each a font made at its size; any not made yet is left out
	constexpr float kLoadingTitle = 60; // the game's name, SemiBold
	struct LoadingFonts
	{
		ImFont* title;	 // kLoadingTitle SemiBold
		ImFont* chip;	 // kChip SemiBold: the system
		ImFont* body;	 // kBody Medium: publisher and year
		ImFont* label;	 // kLabel Regular: the step
		ImFont* caption; // kCaption Regular: the app's name
	};

	// How long the screen takes to fade into the game, in seconds, once the game draws
	constexpr float kLoadingFade = 0.4f;

	// The cover's size each side loads it at (ps5tga::Load), and its small copy's: drawn large, the
	// small one is its colours blurred
	constexpr int kLoadingCoverWidth = 512, kLoadingCoverHeight = 704, kLoadingBlurSize = 24;

	struct Loading
	{
		std::string system;	 // "Nintendo Switch", "Wii U"
		std::string title;	 // the game's name
		std::string details; // GameTDB's publisher and year
		std::string step;	 // what the emulator is doing (ShaderStep, LoadingStep)
		float progress = -1; // how far, 0 to 1; below 0, not known: a light runs along the bar
		LoadingPicture cover, blur;
	};

	// What the emulator is doing, said the same way on every side
	inline std::string ShaderStep(size_t done, size_t of)
	{
		// tr: a game's loading screen: the game's shaders read from the cache and made ready, {0} of {1}
		return ps5lang::TrF("Shaders: {0} of {1}", done, of);
	}
	inline std::string PipelineStep(size_t done, size_t of)
	{
		// tr: a game's loading screen: the graphics pipelines (shaders put together for the GPU) made ready from the cache, {0} of {1}
		return ps5lang::TrF("Pipelines: {0} of {1}", done, of);
	}
	inline std::string LoadingStep()
	{
		return ps5lang::Tr("Loading…");
	}
	inline std::string StartingStep()
	{
		return ps5lang::Tr("Starting");
	}

	// The screen's alpha seconds after the game first drew: 1 down to 0 over kLoadingFade
	inline float LoadingFadeAlpha(float since)
	{
		const float t = std::clamp(since / kLoadingFade, 0.0f, 1.0f);
		return 1.0f - t * t * (3.0f - 2.0f * t);
	}

	// The whole screen, size pixels, into draw. scale: the screen's height over 1080; time: seconds
	// since it first showed (the bar's light); alpha: 1, less as it fades into the game
	inline void DrawLoading(ImDrawList* draw, ImVec2 size, float scale, const Palette& colours, const LoadingFonts& fonts,
		const Loading& loading, float time, float alpha = 1.0f)
	{
		if (alpha <= 0.0f)
			return;
		const int firstVertex = draw->VtxBuffer.Size;
		draw->AddRectFilled({0, 0}, size, Colour(0x05070d));
		// the cover's colours, blurred, over the whole screen; darker on the left, where the words are
		if (const LoadingPicture& blur = loading.blur; blur.id && blur.width > 0 && blur.height > 0)
		{
			const float k = std::max(size.x / blur.width, size.y / blur.height);
			const ImVec2 half{blur.width * k * 0.5f, blur.height * k * 0.5f};
			const ImVec2 middle{size.x * 0.5f, size.y * 0.5f};
			draw->AddImage(blur.id, {middle.x - half.x, middle.y - half.y}, {middle.x + half.x, middle.y + half.y}, {0, 0}, {1, 1},
				IM_COL32(255, 255, 255, 170));
		}
		draw->AddRectFilledMultiColor({0, 0}, size, IM_COL32(5, 7, 13, 236), IM_COL32(5, 7, 13, 150), IM_COL32(5, 7, 13, 170),
			IM_COL32(5, 7, 13, 240));
		const ImVec2 origin{(size.x - 1920.0f * scale) * 0.5f, (size.y - 1080.0f * scale) * 0.5f};
		const Canvas canvas{draw, scale, origin, colours};
		// the cover, as the launcher's tile showed it, with a soft shadow
		const LoadingPicture& cover = loading.cover;
		const bool hasCover = cover.id && cover.width > 0 && cover.height > 0;
		const float coverHeight = 520;
		const float coverWidth = hasCover ? coverHeight * cover.width / cover.height : coverHeight * 0.75f;
		const float x0 = 200, y0 = (1080 - coverHeight) * 0.5f;
		for (int i = 0; i < 8; i++)
		{
			const float d = 4.0f + i * 5;
			draw->AddRectFilled(canvas.At(x0 - d, y0 - d * 0.5f), canvas.At(x0 + coverWidth + d, y0 + coverHeight + d * 1.5f),
				IM_COL32(0, 0, 0, 22 - i * 2), (22 + d) * scale);
		}
		if (hasCover)
			draw->AddImageRounded(cover.id, canvas.At(x0, y0), canvas.At(x0 + coverWidth, y0 + coverHeight), {0, 0}, {1, 1},
				IM_COL32(255, 255, 255, 255), 22 * scale);
		else
			draw->AddRectFilled(canvas.At(x0, y0), canvas.At(x0 + coverWidth, y0 + coverHeight), colours.accentInk, 22 * scale);
		draw->AddRect(canvas.At(x0, y0), canvas.At(x0 + coverWidth, y0 + coverHeight), IM_COL32(255, 255, 255, 30), 22 * scale, 0, 1.5f * scale);
		// beside it: the system, the game's name, GameTDB's publisher and year, then what the emulator is doing
		const float tx = x0 + coverWidth + 80, width = 1920 - tx - 180;
		float y = y0 + 40;
		if (fonts.chip)
			canvas.Chip(fonts.chip, tx, y, loading.system, colours.accentInk, colours.accent, true);
		y += 64;
		if (fonts.title)
		{
			const Canvas::Lines lines = canvas.Break(fonts.title, kLoadingTitle, loading.title, width);
			const int shown = std::min(2, (int)lines.drawn.size());
			for (int i = 0; i < shown; i++)
				canvas.Text(fonts.title, kLoadingTitle, tx, y + i * 70, colours.title, lines.drawn[i]);
			y += shown * 70 + 18;
		}
		if (fonts.body && !loading.details.empty())
			canvas.Text(fonts.body, kBody, tx, y, colours.copy, loading.details);
		// at the cover's foot: the step, and a bar for the shaders, or a light that runs along it while
		// how far is not known
		const float by = y0 + coverHeight - 64;
		if (fonts.label)
			canvas.Text(fonts.label, kLabel, tx, by, colours.text, loading.step);
		const float barWidth = std::min(width, 560.0f);
		draw->AddRectFilled(canvas.At(tx, by + 44), canvas.At(tx + barWidth, by + 50), IM_COL32(255, 255, 255, 26), 3 * scale);
		if (loading.progress >= 0)
			draw->AddRectFilled(canvas.At(tx, by + 44), canvas.At(tx + barWidth * std::min(1.0f, loading.progress), by + 50), colours.accent, 3 * scale);
		else
		{
			const float run = std::fmod(time * 0.6f, 1.0f);
			const float a = std::max(0.0f, run * 1.3f - 0.3f), b = std::min(1.0f, run * 1.3f);
			draw->AddRectFilled(canvas.At(tx + barWidth * a, by + 44), canvas.At(tx + barWidth * b, by + 50), colours.accent, 3 * scale);
		}
		// the app's name, small, in the corner
		if (fonts.caption)
			canvas.TextRight(fonts.caption, kCaption, 1920 - 64, 1080 - 64, colours.copy, "Playtendoh");
		// fading into the game: everything just drawn, its alpha scaled
		if (alpha < 1.0f)
			for (int i = firstVertex; i < draw->VtxBuffer.Size; i++)
				draw->VtxBuffer[i].col = Fade(draw->VtxBuffer[i].col, alpha);
	}
}
