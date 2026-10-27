// SPDX-License-Identifier: GPL-3.0-or-later
// app/reorganize.cpp on a PC: an install from before given a folder for each system and put back, in a
// folder of its own (PS5CEMU_DATA, its ps5cemu/ as /data/ps5cemu): nothing lost or changed either way,
// a file in the way stops it before anything moves, a move that fails puts its system back, and the
// start after a power cut finishes. Prints PASS lines; exits 1 on a failure.
#include "app/paths.h"
#include "app/reorganize.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;

#ifdef REFUSED_LSTAT
// /data as ShadowMountPlus 1.7 mounts it into an app without a HEN: every lstat refused with EPERM, stat
// answered. Linked with --wrap=lstat, so the code under test gets this one (test-reorganize.sh)
extern "C" int __wrap_lstat(const char*, struct stat*)
{
	errno = EPERM;
	return -1;
}
#endif

static void Check(bool ok, const std::string& what)
{
	if (!ok)
		throw std::runtime_error(what);
	std::cout << "PASS  " << what << '\n';
}

static void Put(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path) << text;
}

static std::string Read(const fs::path& path)
{
	std::ifstream file(path);
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

// every file under a folder, by its path in it, with what it holds (markers and the request left out)
static std::map<std::string, std::string> Files(const fs::path& base)
{
	std::map<std::string, std::string> files;
	for (const auto& entry : fs::recursive_directory_iterator(base))
		if (entry.is_regular_file() && entry.path().filename() != ecam_layout::kMarker)
			files[fs::relative(entry.path(), base).string()] = Read(entry.path());
	return files;
}

static void Clear(const fs::path& base)
{
	std::error_code ec;
	fs::permissions(base, fs::perms::owner_all, fs::perm_options::add, ec);
	for (const auto& entry : fs::recursive_directory_iterator(base, ec))
		fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::add, ec);
	fs::remove_all(base);
}

// An install from before: Cemu's files at the top among the launcher's, the others in folders of theirs
static void Install(const fs::path& shared)
{
	Put(shared / "ps5cemu.json", "{}");
	Put(shared / "covers/boxart/game.png", "cover");
	Put(shared / "logs/boot.log", "log");
	Put(shared / "cache/radv/pipelines.bin", "radv");
	Put(shared / "cache/archives/game/code/game.rpx", "unpacked");
	Put(shared / "title_list_cache.xml", "<titles/>");
	Put(shared / "settings.xml", "<cemu/>");
	Put(shared / "keys.txt", "abcdef");
	Put(shared / "mlc01/usr/save/00050000/101c9400/user/80000001/game.sav", "save");
	Put(shared / "games/Zelda.wua", "game");
	Put(shared / "graphicPacks/mods/Mod/rules.txt", "[Definition]");
	Put(shared / "controllerProfiles/controller0.xml", "<pad/>");
	Put(shared / "cache/shaderCache/transferable/101c9400.bin", "shaders");
	Put(shared / "azahar/sdmc/Nintendo 3DS/save.bin", "3ds");
	Put(shared / "azahar/games/Game.3ds", "3ds game");
	Put(shared / "melonds/saves/Game.sav", "ds");
	Put(shared / "eden/keys/prod.keys", "keys");
	Put(shared / "eden/roms/Game.nsp", "switch");
}

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	const fs::path base = argv[1], shared = base / "ps5cemu";
	const std::string root = shared.string(), console = ps5paths::kDefaultRoot;
	if (root != console)
		throw std::runtime_error("build with -DPS5CEMU_DATA=\"" + root + "\"");
	ps5paths::detail::RootSlot() = root;
	std::vector<std::string> log;
	const std::vector<std::string> all{"cemu", "azahar", "melonds", "eden"};

	// separated, then put back
	Install(shared);
	fs::create_directories(base / "cemu/games"); // as every start makes it (ps5paths::SystemDirectories)
	Put(base / "cemu/games/Mario.wua", "put there since");
	auto before = Files(base);
	auto layout = ps5reorganize::Look(root, console);
	Check(layout.separate == all && layout.separated.empty() && layout.blocked.empty(), "an install from before has all four to separate, nothing in the way");
	Check(ps5reorganize::Ask(root, ps5reorganize::Request::Separate) && ps5reorganize::Asked(root) == ps5reorganize::Request::Separate,
		"the request is written down for the next start");
	auto outcome = ps5reorganize::Run(root, console, log);
	Check(outcome.moved == all && outcome.failed.empty() && ps5reorganize::Asked(root) == ps5reorganize::Request::None, "every system moved, the request done");
	for (const char* name : {"cemu", "azahar", "melonds", "eden"})
		Check(ecam_layout::Activated((base / name).string()), std::string(name) + " is in its own folder, marked");
	Check(ecam_layout::Cemu(root) == (base / "cemu").string() && ecam_layout::System(root, "azahar") == (base / "azahar").string() &&
			  ecam_layout::System(root, "melonds") == (base / "melonds").string() && ecam_layout::System(console, "eden") == (base / "eden").string(),
		"the app and Eden's program take the new folders");
	Check(Read(base / "cemu/mlc01/usr/save/00050000/101c9400/user/80000001/game.sav") == "save" && Read(base / "cemu/settings.xml") == "<cemu/>" &&
			  Read(base / "cemu/cache/shaderCache/transferable/101c9400.bin") == "shaders" && Read(base / "cemu/games/Zelda.wua") == "game" &&
			  Read(base / "cemu/games/Mario.wua") == "put there since",
		"Cemu's saves, settings, shader cache and games are in /data/cemu, merged with what was there");
	Check(Read(shared / "ps5cemu.json") == "{}" && Read(shared / "cache/radv/pipelines.bin") == "radv" && fs::exists(shared / "cache/archives/game") &&
			  Read(shared / "title_list_cache.xml") == "<titles/>" && fs::exists(shared / "covers/boxart/game.png"),
		"the launcher's files and the shared cache stay in ps5cemu");
	Check(!fs::exists(shared / "settings.xml") && !fs::exists(shared / "mlc01") && !fs::exists(shared / "azahar") && !fs::exists(shared / "eden"),
		"nothing of the systems is left behind");
	auto after = Files(base);
	Check(after.size() == before.size(), "no file lost or added: " + std::to_string(after.size()));
	Check(ps5paths::Moved(root + "/games/Zelda.wua") == (base / "cemu/games/Zelda.wua").string() &&
			  ps5paths::Moved(root + "/azahar/games/Game.3ds") == (base / "azahar/games/Game.3ds").string() &&
			  ps5paths::Moved(console + "/eden/roms/Game.nsp") == (base / "eden/roms/Game.nsp").string() &&
			  ps5paths::Moved(root + "/covers/boxart/game.png") == root + "/covers/boxart/game.png",
		"a home screen tile's old path finds its game where it went");
	layout = ps5reorganize::Look(root, console);
	Check(layout.separate.empty() && layout.separated == all && layout.blocked.empty(), "then all four are separated, and can be put back");
	ps5reorganize::Ask(root, ps5reorganize::Request::PutBack);
	outcome = ps5reorganize::Run(root, console, log);
	Check(outcome.moved == all && outcome.failed.empty(), "every system put back");
	for (const char* name : {"cemu", "azahar", "melonds", "eden"})
		Check(!ecam_layout::Activated((base / name).string()), std::string(name) + "'s own folder let go of");
	Check(ecam_layout::Cemu(root) == root && ecam_layout::System(root, "azahar") == root + "/azahar", "the app takes the old places again");
	Check(Read(shared / "mlc01/usr/save/00050000/101c9400/user/80000001/game.sav") == "save" && Read(shared / "games/Mario.wua") == "put there since",
		"the saves are back in ps5cemu");
	auto back = Files(base);
	bool same = back.size() == before.size();
	for (const auto& [path, text] : before)
	{
		auto moved = path;
		if (moved.starts_with("cemu/games/"))
			moved = "ps5cemu/games/" + moved.substr(11); // the merged games folder goes back as one
		same = same && (back.count(moved) ? back[moved] == text : back.count(path) && back[path] == text);
	}
	Check(same, "every file is back, as it was");
	Clear(base);

	// a file in the way: nothing moves
	Install(shared);
	Put(base / "cemu/settings.xml", "another");
	before = Files(base);
	layout = ps5reorganize::Look(root, console);
	Check(layout.blocked == (base / "cemu/settings.xml").string() && !layout.otherDrive, "a file where one would go is found before anything moves");
	ps5reorganize::Ask(root, ps5reorganize::Request::Separate);
	outcome = ps5reorganize::Run(root, console, log);
	Check(outcome.moved.empty() && outcome.failed == all && Files(base) == before && ps5reorganize::Asked(root) == ps5reorganize::Request::None,
		"with one in the way, nothing moves and the request is let go of");
	Clear(base);

	// a move that fails part way: that system goes back whole, the others move
	if (geteuid() != 0)
	{
		Install(shared);
		fs::create_directories(base / "cemu/controllerProfiles");
		fs::permissions(base / "cemu/controllerProfiles", fs::perms::owner_read | fs::perms::owner_exec);
		before = Files(base);
		ps5reorganize::Ask(root, ps5reorganize::Request::Separate);
		outcome = ps5reorganize::Run(root, console, log);
		Check(outcome.failed == std::vector<std::string>{"cemu"} && outcome.moved == std::vector<std::string>{"azahar", "melonds", "eden"},
			"a system whose move fails is left out, the others move");
		Check(!ecam_layout::Activated((base / "cemu").string()) && ecam_layout::Cemu(root) == root && Read(shared / "settings.xml") == "<cemu/>" &&
				  Read(shared / "mlc01/usr/save/00050000/101c9400/user/80000001/game.sav") == "save",
			"Cemu is whole in its old place, and used there");
		Check(Files(base).size() == before.size(), "nothing lost");
		Clear(base);
	}

	// a power cut part way: the next start finishes what reorganize.txt says was under way
	Install(shared);
	before = Files(base);
	fs::create_directories(base / "cemu");
	fs::rename(shared / "settings.xml", base / "cemu/settings.xml");
	fs::rename(shared / "mlc01", base / "cemu/mlc01");
	fs::rename(shared / "azahar", base / "azahar");
	std::ofstream(shared / "reorganize.txt") << "separate\ncemu\nazahar\nmelonds\neden\n";
	outcome = ps5reorganize::Run(root, console, log);
	Check(outcome.moved == all && outcome.failed.empty(), "the start after a power cut finishes every system");
	Check(ecam_layout::Cemu(root) == (base / "cemu").string() && ecam_layout::System(root, "azahar") == (base / "azahar").string() &&
			  Read(base / "cemu/mlc01/usr/save/00050000/101c9400/user/80000001/game.sav") == "save" && Read(base / "cemu/keys.txt") == "abcdef",
		"what had moved and what had not are together in the new folders");
	Check(Files(base).size() == before.size(), "nothing lost");
	Clear(base);

	// an install from a development build, Cemu's files in ps5cemu/cemu
	Put(shared / "cemu/settings.xml", "<nested/>");
	Put(shared / "cemu/mlc01/sys/title.bin", "system");
	ps5reorganize::Ask(root, ps5reorganize::Request::Separate);
	outcome = ps5reorganize::Run(root, console, log);
	Check(outcome.moved == std::vector<std::string>{"cemu"} && Read(base / "cemu/settings.xml") == "<nested/>" && !fs::exists(shared / "cemu/settings.xml"),
		"a nested ps5cemu/cemu moves too");
	Clear(base);

	// nothing from before: nothing offered
	fs::create_directories(shared);
	layout = ps5reorganize::Look(root, console);
	Check(layout.separate.empty() && layout.separated.empty(), "a new install has nothing to separate");
	for (const std::string& line : log)
		std::cout << "      " << line << '\n';
	return 0;
}
