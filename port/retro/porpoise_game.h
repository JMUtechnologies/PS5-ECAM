// SPDX-License-Identifier: GPL-3.0-or-later
// What a GameCube or Wii game's own settings offer beyond plain keys, read from Porpoise-X's files as its own
// menus do (its src/ui_setups.cpp, ui_cheats.cpp, ui_recommend.cpp): saved setups, the game's cheats and patches,
// and Auto Boost, its tested picks and Dolphin's fixes. All of it ends up as keys in the game's own file.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ps5porpoise
{
	// -- setups: <data>/setups/setup<N>.ini, the Video and Graphics keys and the game they came from
	inline constexpr int kSetups = 4;
	struct Setup { bool exists = false; std::string from, path; };
	Setup GetSetup(const std::string& data, int index);
	bool SaveSetup(const std::string& data, int index, const std::map<std::string, std::string>& values, const std::string& from);
	std::map<std::string, std::string> SetupValues(const Setup& setup);

	// -- cheats and patches: Dolphin's list for the game, then the player's own in <data>/cheats
	struct Cheat
	{
		std::string kind, name; // OnFrame, ActionReplay or Gecko; with its '$'
		bool defaultOn = false, own = false, pack = false; // pack: the widescreen collection's
	};
	std::vector<Cheat> CheatsFor(const std::string& gameSettings, const std::string& ownDir, const std::string& id,
		const std::string& widescreenDir = {}, bool wii = false);
	// How the game plays in 16:9 (ui_widescreen.cpp's kind_of)
	enum class WideKind { None, Patch, Native };
	WideKind WidescreenKind(const std::string& gameSettings, const std::string& widescreenDir, const std::string& id, bool wii);
	// A Gecko code typed by the player, into <ownDir>/<id>.ini: digits only, sixteen to a line
	bool AddOwnCode(const std::string& ownDir, const std::string& id, std::string name, const std::string& typed, std::string& error);
	// The key that holds its switch: on: turned on; off: a default-on one turned off
	std::string CheatKey(const Cheat& cheat, bool on);

	// -- Auto Boost
	struct Fix
	{
		std::string label, value, why, section, key, raw;
		std::string overrideKey, offValue; // empty when it isn't an on or off
	};
	struct Pick
	{
		std::string note;
		std::vector<std::pair<std::string, std::string>> values;
	};
	inline constexpr const char* kAutoBoostKey = "auto_boost";
	// A game starting, with every game's Auto Boost on and nothing of its own yet: the pick is put in its
	// file, as its Auto Boost switch would. Writes nothing for a game with any setting of its own.
	bool ApplyAutoBoost(const std::string& settingsFile, const std::string& gameFile, const Pick& pick);
	std::vector<Fix> DolphinFixes(const std::string& gameSettings, const std::string& id);
	bool PickFor(const std::string& feed, const std::string& bundled, const std::string& id, Pick& out);
	// Its key, said plainly (dolphin.<Section>.<Key>)
	void DescribeDolphin(const std::string& key, const std::string& value, std::string& label, std::string& text);
	// A Dolphin file of the player's own for the game, which Porpoise leaves alone
	bool HasOwnDolphinFile(const std::string& saves, const std::string& id);
}
