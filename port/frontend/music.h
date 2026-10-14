// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the launcher's music as the sound thread plays it (frontend/sound.cpp): its own piece,
// or the player's MP3 and WAV files from /data/ps5cemu/music. A file is
// read as it plays, a few thousand frames at a time, and resampled to 48 kHz; nothing of it is held
// whole. Nothing here touches the PS5, so a host test plays the same code (tools/tests/test-music.sh).

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace ps5music
{
	constexpr uint32_t kRate = 48000; // what AudioOut plays

	// A piece's stereo 16-bit frames at its own rate, as they are asked for
	class Source
	{
	public:
		virtual ~Source() = default;
		// Up to `frames` frames into out (left, right); fewer at the end
		virtual size_t Read(int16_t* out, size_t frames) = 0;
		// Back to its start, for a piece that loops
		virtual bool Rewind() = 0;
		uint32_t rate = kRate;
	};

	// Frames already decoded (the launcher's own piece), at a rate
	std::unique_ptr<Source> FromFrames(std::vector<int16_t> stereo, uint32_t rate = kRate);
	// An MP3, or a WAV of 16-bit PCM, at any rate, mono or more channels (the first two are kept); null
	// when the file cannot be read as one
	std::unique_ptr<Source> OpenFile(const std::string& path);
	// The MP3 and WAV files in a folder, by name, in order regardless of case
	std::vector<std::string> Files(const std::string& folder);

	// What plays: "setup" (the launcher's own, from setup()), "file:<name>" (a file in folder, looping),
	// "shuffle" (every file in folder, in a random order, one after another) or "off"
	class Player
	{
	public:
		Player(std::string folder, std::function<std::unique_ptr<Source>()> setup);
		// Opens what is chosen; a file that cannot be played plays the launcher's own piece instead
		void Choose(const std::string& music);
		bool Playing() const { return m_source != nullptr; }
		// The next frame at 48 kHz; false (and silence) when nothing plays
		bool Next(float& left, float& right);
		// The file playing, by name (empty for the launcher's piece or none)
		const std::string& Current() const { return m_current; }

	private:
		bool Frame(std::array<int16_t, 2>& frame); // the source's next frame at its rate, past its end as it continues
		bool OpenNextShuffled();

		std::string m_folder;
		std::function<std::unique_ptr<Source>()> m_setup;
		std::unique_ptr<Source> m_source;
		std::string m_music, m_current;
		std::vector<std::string> m_order; // shuffle's files, in the order they play
		size_t m_next = 0;
		std::mt19937 m_random;
		// the resampler: the source's frames either side of where 48 kHz is, and how far between
		std::array<int16_t, 2> m_before{}, m_after{};
		double m_between = 0;
		std::vector<int16_t> m_chunk;
		size_t m_chunkFrames = 0, m_chunkAt = 0;
	};
}
