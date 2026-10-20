// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the launcher's sound: its music, looping under every screen, and its menu's sounds as
// the cursor moves and choices are made. Both are PS5-ECAM's own, synthesised by
// tools/render-sounds.py into assets/ui/sounds; the music is in the spirit of a Nintendo console's
// shop and setup screens, written for the launcher. The player's own music plays in its place when
// chosen: the MP3 and WAV files in /data/ps5cemu/music.

#pragma once

#include <string>
#include <vector>

namespace ps5sound
{
	enum class Effect
	{
		Move,	// the cursor moved, or a value changed
		Select, // something opened or was chosen
		Back,	// something closed
		Denied, // Cross on what does nothing (yet)
		Launch, // a game is starting
	};

	// The player's own music: where it goes, and the MP3 and WAV files there, by file name, in order
	std::string MusicFolder();
	std::vector<std::string> MusicFiles();

	// The music as ps5cemu.json names it: "setup" (the launcher's own), "file:<a file in MusicFolder>"
	// (looping), "shuffle" (every file there, in a random order) or "off"; its volume in percent.
	// Opens AudioOut and starts the music, fading in. Without AudioOut the launcher is silent.
	void Start(const std::string& music, int volume, bool menuSounds);
	// At once: another piece fades out and the new one in, a volume glides to its new level.
	void SetMusic(const std::string& music, int volume);
	void SetMenuSounds(bool on);
	void Play(Effect effect);
	// The music fades out and the last menu sound finishes, then AudioOut is closed, for the game's
	// emulator to open its own.
	void Stop();
}
