// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's music (port/frontend/music.cpp) on the PC: tools/tests/test-music.sh makes the files
// with ffmpeg and runs this on them. Each check prints PASS or FAIL; the exit status is the failures.

#include "frontend/music.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ps5log
{
	void Write(std::string_view line) { std::fprintf(stderr, "%.*s\n", (int)line.size(), line.data()); }
}

namespace
{
	int failures = 0;

	void Check(bool ok, const char* what)
	{
		std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
		failures += !ok;
	}

	// a second of the player's left channel: its pitch (rising zero crossings) and its peak
	void Measure(ps5music::Player& player, double& hertz, float& peak)
	{
		int crossings = 0;
		float last = 0;
		peak = 0;
		for (uint32_t i = 0; i < ps5music::kRate; i++)
		{
			float left, right;
			player.Next(left, right);
			if (last < 0 && left >= 0)
				crossings++;
			last = left;
			peak = std::max(peak, std::fabs(left));
		}
		hertz = crossings;
	}

	std::vector<int16_t> Tone(double hertz, double seconds)
	{
		std::vector<int16_t> frames;
		for (uint32_t i = 0; i < seconds * ps5music::kRate; i++)
		{
			const int16_t v = (int16_t)(12000 * std::sin(2 * M_PI * hertz * i / ps5music::kRate));
			frames.push_back(v);
			frames.push_back(v);
		}
		return frames;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
		return 2;
	const std::string folder = argv[1];
	int setupOpened = 0;
	ps5music::Player player(folder, [&] {
		setupOpened++;
		return ps5music::FromFrames(Tone(300, 1));
	});

	const auto files = ps5music::Files(folder);
	Check(files == std::vector<std::string>{"A tone.mp3", "b-tone.WAV", "not music.wav"}, "the folder's MP3s and WAVs, in order regardless of case, nothing else");

	double hertz;
	float peak;
	player.Choose("file:A tone.mp3");
	Measure(player, hertz, peak);
	Check(player.Playing() && player.Current() == "A tone.mp3", "an MP3 plays");
	Check(std::fabs(hertz - 440) <= 3, "an MP3 at 44.1 kHz, mono, keeps its pitch at 48 kHz (440 Hz)");
	std::printf("      %.1f Hz, peak %.0f\n", hertz, peak);
	// ffmpeg's sine is at an eighth of full scale: 4096
	Check(std::fabs(peak - 4096) < 400, "at its level (an eighth of full scale)");
	// past its two seconds: it loops
	Measure(player, hertz, peak);
	Measure(player, hertz, peak);
	Check(player.Playing() && std::fabs(hertz - 440) <= 3 && std::fabs(peak - 4096) < 400, "a chosen file loops past its end");

	player.Choose("file:b-tone.WAV");
	Measure(player, hertz, peak);
	Check(player.Playing() && std::fabs(hertz - 1000) <= 4, "a WAV at 22.05 kHz, stereo, keeps its pitch (1000 Hz)");

	setupOpened = 0;
	player.Choose("file:not music.wav");
	Check(player.Playing() && player.Current().empty() && setupOpened == 1, "a WAV that is not 16-bit PCM: the launcher's own piece instead");
	player.Choose("file:gone.mp3");
	Check(player.Playing() && player.Current().empty(), "a file no longer there: the launcher's own piece");
	player.Choose("file:../A tone.mp3");
	Check(player.Current().empty(), "a name with a folder in it is not opened");
	player.Choose("setup");
	Measure(player, hertz, peak);
	Check(player.Playing() && std::fabs(hertz - 300) <= 2, "the launcher's own piece");

	player.Choose("off");
	float left = 1, right = 1;
	Check(!player.Playing() && !player.Next(left, right) && left == 0 && right == 0, "off: nothing plays");

	player.Choose("shuffle");
	std::set<std::string> heard;
	for (int second = 0; second < 8 && player.Playing(); second++)
	{
		heard.insert(player.Current());
		Measure(player, hertz, peak);
	}
	Check(heard.size() == 2 && !heard.count("not music.wav"), "shuffle plays one file after another, skipping what cannot be played");
	return failures;
}
