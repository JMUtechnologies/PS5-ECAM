// SPDX-License-Identifier: GPL-3.0-or-later
// port/retro/porpoise_game.cpp on made-up files. Argument: an empty folder to work in.
#include "retro/porpoise_game.h"
#include "retro/porpoise_settings.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace ps5porpoise;

static int failures = 0;
#define CHECK(what) do { const bool ok_ = (what); std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", #what); failures += !ok_; } while (0)

int main(int, char** argv)
{
	const fs::path work = argv[1];
	const std::string sys = (work / "Sys").string(), data = (work / "data").string();
	fs::create_directories(sys);
	fs::create_directories(data + "/cheats");
	std::ofstream(sys + "/GZL.ini") << "[Video_Hacks]\n# A note\nEFBToTextureEnable = False\n[Video_Settings]\nSafeTextureCacheColorSamples = 512\n[OnFrame]\n$60 FPS\n0x1=word:0x1\n[Gecko]\n$Cheat [Me]\n";
	std::ofstream(sys + "/GZLE01.ini") << "[OnFrame_Enabled]\n$On by itself\n";
	std::ofstream(data + "/cheats/GZLE01.txt") << "My code\n01234567 89ABCDEF\n";

	const auto cheats = CheatsFor(sys, data + "/cheats", "GZLE01");
	CHECK(cheats.size() == 4);
	CHECK(cheats[0].defaultOn);
	bool own = false;
	for (const Cheat& cheat : cheats)
		own = own || (cheat.own && cheat.name == "$My code" && cheat.defaultOn && cheat.kind == "Gecko");
	CHECK(own);
	CHECK(CheatKey(cheats[0], false).starts_with("dolphin.") && CheatKey(cheats[0], false).find("_Disabled.") != std::string::npos);
	CHECK(CheatsFor(sys, data + "/cheats", "").empty());

	const auto fixes = DolphinFixes(sys, "GZLE01");
	CHECK(fixes.size() == 2);
	CHECK(fixes[0].overrideKey == "dolphin.Video_Hacks.EFBToTextureEnable" && fixes[0].offValue == "True" && fixes[0].why == "A note");
	CHECK(fixes[1].overrideKey.empty() && fixes[1].value == "Middle");

	std::ofstream(data + "/bundled.ini") << "[GZL]\nnote = old\nresolution = 2\n[GALE01]\nnote = x\nwide = 1\n";
	std::ofstream(data + "/feed.ini") << "[GZL]\nnote = new\nresolution = 3\n";
	Pick pick;
	CHECK(PickFor(data + "/feed.ini", data + "/bundled.ini", "GZLE01", pick) && pick.note == "new" && pick.values.size() == 1 && pick.values[0].second == "3");
	CHECK(!PickFor(data + "/feed.ini", data + "/bundled.ini", "ZZZZ01", pick));

	// Auto Boost: every game's on (or not said), a game with nothing of its own, once
	Pick boost;
	boost.values = {{"resolution", "3"}, {"fast_float", "1"}};
	const std::string every = data + "/settings.ini", game = data + "/boost.ini";
	std::ofstream(every) << "volume = 5\n";
	CHECK(ApplyAutoBoost(every, game, boost) && ps5porpoise::Read(game).at("resolution") == "3" && ps5porpoise::Read(game).at(kAutoBoostKey) == "1");
	std::ofstream(game) << "auto_boost = 0\n";
	CHECK(!ApplyAutoBoost(every, game, boost) && ps5porpoise::Read(game).size() == 1);
	std::ofstream(game) << "wide = 1\n";
	CHECK(!ApplyAutoBoost(every, game, boost) && ps5porpoise::Read(game).size() == 1);
	fs::remove(game);
	std::ofstream(every) << "auto_boost = 0\n";
	CHECK(!ApplyAutoBoost(every, game, boost) && !fs::exists(game));
	CHECK(!ApplyAutoBoost(every + ".none", game, Pick{}) && !fs::exists(game));

	// the widescreen collection, and a code of the player's
	const std::string wideDir = data + "/wide";
	fs::create_directories(wideDir);
	std::ofstream(wideDir + "/codes.ini") << "[[GAAE01]]\n[Gecko]\n$16:9\n04000000 00000000\n[Gecko_Enabled]\n$16:9\n[[GBBE01]]\n[Gecko]\n$Other\n";
	std::ofstream(wideDir + "/native.txt") << "GCCE01\n";
	CHECK(WidescreenKind(sys, wideDir, "GAAE01", false) == WideKind::Patch);
	CHECK(WidescreenKind(sys, wideDir, "GCCE01", false) == WideKind::Native);
	CHECK(WidescreenKind(sys, wideDir, "GDDE01", false) == WideKind::None);
	CHECK(WidescreenKind(sys, wideDir, "GDDE01", true) == WideKind::Native);
	const auto packed = CheatsFor(sys, data + "/none", "GAAE01", wideDir);
	CHECK(packed.size() == 1 && packed[0].pack && packed[0].defaultOn);
	std::string why;
	CHECK(!AddOwnCode(data + "/typed", "GAAE01", "x", "12G", why) && why == "digits");
	CHECK(!AddOwnCode(data + "/typed", "GAAE01", "x", "1234", why) && why == "length");
	CHECK(AddOwnCode(data + "/typed", "GAAE01", "My [one]", "04000000 0000000a", why));
	CHECK(AddOwnCode(data + "/typed", "GAAE01", "My [one]", "0400000000000001", why));
	const auto typed = CheatsFor(sys, data + "/typed", "GAAE01");
	CHECK(typed.size() == 2 && typed[0].own && typed[0].defaultOn && typed[1].name == "$My (one) 2");

	CHECK(!GetSetup(data, 0).exists);
	CHECK(SaveSetup(data, 0, {{"resolution", "4"}, {"volume", "1"}}, "Zelda\nsecond"));
	const Setup setup = GetSetup(data, 0);
	CHECK(setup.exists && setup.from == "Zeldasecond");
	const auto values = SetupValues(setup);
	CHECK(values.size() == 1 && values.at("resolution") == "4");
	CHECK(!SaveSetup(data, 9, {}, "x"));

	CHECK(!HasOwnDolphinFile(data + "/saves", "GZLE01"));
	fs::create_directories(data + "/saves/User/GameSettings");
	std::ofstream(data + "/saves/User/GameSettings/GZLE01.ini") << "# Written by Porpoise\n";
	CHECK(!HasOwnDolphinFile(data + "/saves", "GZLE01"));
	std::ofstream(data + "/saves/User/GameSettings/GZLE01.ini") << "[Core]\n";
	CHECK(HasOwnDolphinFile(data + "/saves", "GZLE01"));
	return failures ? 1 : 0;
}
