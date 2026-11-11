// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Porpoise-X's settings (premohq/Porpoise-X, src/ui_settings.cpp), for PS5-ECAM's own Settings
// to show on the GameCube and Wii sides: the files Porpoise reads as PS5-ECAM starts a game in it
// (launch.cpp), written in Porpoise's own words, so PS5-ECAM uses nothing of Porpoise's UI and Porpoise
// needs nothing of PS5-ECAM's.
//   <data>/settings.ini                every game's: "key = value" lines, Porpoise's settings table
//   <data>/game-settings/<key>.ini     a game's own, with only the keys it changes; <key> its disc's ID
//                                      (Porpoise's Library::settings_key_of), else its file's name
// Writing one changes the keys given and keeps every other line as Porpoise wrote it: its folders, its
// presets and button sets, its own menus' settings.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ps5porpoise
{
	// The settings_version Porpoise-X writes (ui_settings.cpp's Settings::save), for a settings.ini made here
	inline constexpr const char* kSettingsVersion = "19";
	// Whether Porpoise plays the side, on the emulator chosen for it (ps5retro::kSystems: the GameCube and the Wii)
	bool Plays(int side, int backend = 0);
	// Porpoise's folder (ps5retro::kSystems' data for the GameCube and the Wii)
	std::string Data();
	std::string SettingsFile();
	// A game's own file, for the disc at gamePath
	std::string GameFile(const std::string& gamePath);
	// The disc's ID as Porpoise keys a game's settings ("GALE01"): a GameCube or Wii disc image's own
	// (.iso, .gcm), a WBFS's, a WIA's or RVZ's, a CISO's; else the file's name
	std::string GameKey(const std::string& gamePath);

	void ClearAchievementsAccount(); // signed out: Porpoise's copy goes too
	// What Porpoise's loading screen (the Switch side's, in Porpoise X) says of the game about to start:
	// <data>/playtendoh-session.txt, a line each; banner: a full-screen picture of the game (ps5boxart's title
	// art) for its ground, in place of its cover's colours
	void WriteSession(const std::string& side, const std::string& title, const std::string& details, const std::string& cover, const std::string& banner = {});

	// Porpoise's own logs: its trace.txt and Dolphin's core.log, each start's own with the last one's beside it
	// (trace.prev.txt, core.prev.log; Porpoise-X's porpoise_paths.hpp). Its trace is in the sandbox
	// (kSandboxTrace, in SandboxDownload()) while /data can't be reached.
	std::string LogsFolder();
	inline constexpr const char* kSandboxTrace = "porpoise-trace.txt";
	// The crash Porpoise's last start ended in, from its trace ("signal 11 fault=... rip=..." or an uncaught
	// exception's "terminate: ..."), when the first trace of `traces` that is there changed after `after` (seconds
	// since 1970): what the launcher says and a report holds, as the boot log's [crash] line for the other sides.
	// Empty when it ended well, or is older.
	std::string LastCrash(const std::vector<std::string>& traces, int64_t after);

	// The app's RetroAchievements account, in Porpoise's own file (<data>/retroachievements.ini), so it signs in with it
	void WriteAchievementsAccount(const std::string& user, const std::string& token, int points);

	// The file's "key = value" settings (empty when there is none)
	std::map<std::string, std::string> Read(const std::string& file);
	// values over the file's (made with Porpoise's header when new), the rest kept
	bool Write(const std::string& file, const std::map<std::string, std::string>& values, std::string& error);
	// keys taken out of a game's own file, so the game has every game's again; the file removed once empty
	bool Remove(const std::string& file, const std::vector<std::string>& keys, std::string& error);
}
