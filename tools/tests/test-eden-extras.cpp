// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden/extras.h"
#include <rapidjson/document.h>
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace ps5eden::extras;
static int failures = 0;
static void Check(bool ok, const char* what) { std::cout << (ok ? "PASS " : "FAIL ") << what << '\n'; failures += !ok; }
static void Put(const fs::path& path, const std::string& text) { fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << text; }
static std::string Get(const fs::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
static rapidjson::Document Json(const fs::path& path) { rapidjson::Document json; const auto text = Get(path); json.Parse(text.c_str()); return json; }

int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	const fs::path root = argv[1];
	Context context{(root / "config/prosperoeden.json").string(), (root / "user").string(),
		(root / "files").string(), (root / "files/roms/My Game").string(), 0x0100ABCDEF123000};
	std::string error;
	const std::string title = "0100ABCDEF123000";
	Put(root / "files/mods" / title / "Textures/romfs/file", "texture");
	Put(root / "files/cheats" / title / "1234567890ABCDEF.txt", "[30 FPS]\n04000000 00000000 00000001\n[60 FPS]\n04000000 00000000 00000002\n");
	Put(context.settings, "{\"unknown\":{\"keep\":42},\"games\":{\"OTHER\":{\"keep\":true}}}");
	auto view = Read(context);
	Check(view.error.empty() && view.mods.size() == 1 && view.cheats.size() == 2, "standalone mod and cheat discovery");
	Put(fs::path(context.user) / "post_presets/random-file.fxp", "name=Global Custom\nchain=effect.fx\n");
	Put(fs::path(context.gameFolder) / "ReShade/not-the-name.fxp", "name=Game Custom\nchain=other.fx\n");
	Put(fs::path(context.gameFolder) / "ReShade/invalid.fxp", "name=No Chain\n");
	view = Read(context);
	Check(std::find(view.presets.begin(), view.presets.end(), "Global Custom") != view.presets.end() &&
		std::find(view.presets.begin(), view.presets.end(), "Game Custom") != view.presets.end(), "custom presets use declared names from global and game folders");
	Check(std::find(view.presets.begin(), view.presets.end(), "No Chain") == view.presets.end(), "incomplete preset not offered");
	Check(!view.cheats[0].on && !view.cheats[1].on, "multi-cheat collections start unselected");
	Check(SetMod(context, "Textures", false, error) && !Read(context).mods[0].on, "individual mod switch persists");
	Check(SetMaster(context, false, false, error) && !Read(context).modsOn, "mods master off");
	Check(SetMaster(context, false, true, error) && !Read(context).mods[0].on, "master preserves individual choices");
	Check(SetCheat(context, "Cheats folder", "30 FPS", true, error), "choose 30 FPS");
	Check(SetCheat(context, "Cheats folder", "60 FPS", true, error), "choose 60 FPS");
	view = Read(context);
	Check(!view.cheats[0].on && view.cheats[1].on, "mutually exclusive FPS choices use standalone rules");
	Check(SetMaster(context, true, false, error) && !Read(context).cheatsOn && Read(context).cheats[1].on, "cheat master retains chosen cheat");
	Check(SetPreset(context, "Cinematic", false, error) && Read(context).ownPreset && Read(context).preset == "Cinematic", "per-game post-processing");
	Check(SetPreset(context, "", false, error) && Read(context).ownPreset && Read(context).preset.empty(), "explicit Off differs from inheritance");
	Check(SetPreset(context, "", true, error) && !Read(context).ownPreset, "restore inherited post-processing");
	auto json = Json(context.settings);
	Check(json["unknown"]["keep"].GetInt() == 42 && json["games"]["OTHER"]["keep"].GetBool(), "unrelated JSON fields preserved");
	Check(Get(context.settings + ".ecam-before").find("unknown") != std::string::npos, "first original retained");
	const std::string before = Get(context.settings);
	Check(!SetMod(context, "missing", false, error) && Get(context.settings) == before, "vanished mod does not rewrite settings");
	Check(!SetPreset(context, "../bad", false, error) && Get(context.settings) == before, "unsafe preset name refused");
	Put(context.settings + ".ecam-new", "another writer");
	Check(!SetMaster(context, true, true, error) && Get(context.settings) == before, "exclusive staging refuses stale/concurrent writer");
	fs::remove(context.settings + ".ecam-new");
	Put(context.settings, "{broken");
	Check(!SetMaster(context, true, true, error) && Get(context.settings) == "{broken", "corrupt JSON never overwritten");
	Put(context.settings, "{\"games\":[]}");
	Check(!SetMaster(context, true, true, error) && Get(context.settings) == "{\"games\":[]}", "unexpected shape never overwritten");
	const std::string first(32, '1'), second(32, '2');
	Put(context.settings, "{\"profiles\":{\"current\":\"" + second + "\",\"first\":\"" + first + "\"},\"games\":{\"FIRST\":{\"keep\":true}},\"profile_settings\":{\"" + second + "\":{\"games\":{\"OTHER\":{\"keep\":true}}}}}");
	Check(SetMod(context, "Textures", false, error), "second profile mod setting saved");
	json = Json(context.settings);
	Check(json["profile_settings"][second.c_str()]["games"][title.c_str()]["mods_off"].IsArray() &&
		json["games"]["FIRST"]["keep"].GetBool() && !json["games"].HasMember(title.c_str()), "profile isolation");
	Context global = context; global.title = 0;
	Check(SetPreset(global, "Clean", false, error) && Read(global).preset == "Clean", "profile-wide preset");
	Check(Read(context).preset == "Clean", "game inherits selected profile preset");
	std::array<char, 0x10 + 8 * 0xC8> profiles{};
	for (int i = 0; i < 2; i++)
	{
		char* entry = profiles.data() + 0x10 + i * 0xC8;
		std::fill(entry, entry + 16, i + 1);
		const std::string name = i ? "Player Two" : "Player One";
		std::copy(name.begin(), name.end(), entry + 40);
	}
	const auto file = root / "user/nand/system/save/8000000000000010/su/avators/profiles.dat";
	Put(file, std::string(profiles.data(), profiles.size()));
	view = Read(global);
	Check(view.profiles.size() == 2 && view.profiles[1].name == "Player Two", "reads standalone binary profile layout");
	const std::string profileBytes = Get(file);
	Check(SelectProfile(global, view.profiles[1].id, 7, error), "select existing player");
	json = Json(context.settings);
	Check(std::string(json["profiles"]["current"].GetString()) == view.profiles[1].id &&
		std::string(json["profiles"]["users"]["7"].GetString()) == view.profiles[1].id, "choice remembered for PS5 user");
	Check(Get(file) == profileBytes, "profile binary and save data left untouched");
	// A PS5 user can have remembered another profile than the last console-wide choice.
	const auto selected = view.profiles[1].id, owner = view.profiles[0].id;
	Put(context.settings, "{\"profiles\":{\"current\":\"" + owner + "\",\"first\":\"" + owner + "\",\"users\":{\"7\":\"" + selected + "\"}}}");
	context.ps5User = 7;
	Check(Read(context).profile == 1, "profile view follows remembered PS5 user like standalone Resolve");
	Check(SetMod(context, "Textures", false, error), "remembered player's mod choice saved");
	json = Json(context.settings);
	Check(json["profile_settings"][selected.c_str()]["games"][title.c_str()]["mods_off"].IsArray() &&
		!json.HasMember("games"), "remembered user cannot accidentally edit previous player's settings");
	Check(!SelectProfile(global, "missing", 7, error), "missing profile refused");
	Put(file, "short");
	Check(Read(global).profiles.empty(), "truncated profile file is not trusted");
	fs::remove(context.settings);
	const auto outside = root / "outside.json";
	Put(outside, "{}");
	fs::create_symlink(outside, context.settings);
	Check(!SetMaster(context, false, false, error) && Get(outside) == "{}", "symlinked settings refused");
	return failures ? 1 : 0;
}
