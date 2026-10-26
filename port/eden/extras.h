// SPDX-License-Identifier: GPL-3.0-or-later
// ECAM's pre-launch controls for features already in the pinned standalone Eden.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ps5eden::extras
{
	struct Item
	{
		std::string source, name;
		bool on = false;
	};
	struct Profile
	{
		std::string id, name;
	};
	struct View
	{
		bool modsOn = true, cheatsOn = true;
		std::vector<Item> mods, cheats;
		std::vector<std::string> presets;
		std::string preset, error;
		bool ownPreset = false;
		std::vector<Profile> profiles;
		int profile = 0;
	};
	// Paths explicit for host tests. settings is Eden's config/prosperoeden.json; user is its user/.
	// gameFolder is empty for a loose ROM, otherwise the game's own directory as Eden resolves it.
	struct Context
	{
		std::string settings, user, gameFiles, gameFolder;
		uint64_t title = 0;
		int ps5User = -1;
	};
	View Read(const Context& context);
	// Persist only the requested field, preserving unknown keys and other profiles. Invalid JSON
	// or an unexpected object shape is refused. Writes stage beside the original and keep the first
	// original in .ecam-before. Never changes mod/cheat files or profile save data.
	bool SetMaster(const Context& context, bool cheats, bool on, std::string& error);
	bool SetMod(const Context& context, const std::string& name, bool on, std::string& error);
	bool SetCheat(const Context& context, const std::string& source, const std::string& name, bool on, std::string& error);
	bool SetPreset(const Context& context, const std::string& name, bool inherit, std::string& error);
	bool SelectProfile(const Context& context, const std::string& id, int ps5User, std::string& error);
}
