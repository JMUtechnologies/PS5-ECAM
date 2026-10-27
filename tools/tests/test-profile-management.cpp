// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden/extras.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
using namespace ps5eden::extras;
static int failures = 0;
static void Check(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
static void Put(const fs::path& path, const std::string& text) { fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << text; }
static std::string Get(const fs::path& path) { std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	const fs::path root = argv[1];
	Context context{(root / "config/prosperoeden.json").string(), (root / "user").string(), (root / "files").string(), ""};
	const fs::path file = fs::path(context.user) / "nand/system/save/8000000000000010/su/avators/profiles.dat";
	std::string error;
	Check(CreateProfile(context, "First", error), "create the first binary player list");
	auto view = Read(context);
	Check(view.profiles.size() == 1 && view.profiles[0].name == "First" && view.profiles[0].id.size() == 32, "created player has a valid ID and name");
	const auto first = view.profiles[0].id;
	const auto initial = Get(file);
	Check(!RenameProfile(context, "", "Ghost", error) && Get(file) == initial, "empty identity cannot rename an unused slot");
	Check(!RemoveProfile(context, first, error), "last player cannot be removed");
	Check(CreateProfile(context, "Second", error), "add another player");
	view = Read(context);
	const auto second = view.profiles[1].id;
	Check(first != second, "player IDs are distinct");
	Check(SelectProfile(context, second, 7, error), "created player can be selected");
	Put(fs::path(context.user) / "nand/user/save/0000000000000000" / second / "0100ABCDEF123000/main", "save retained");
	const std::string settings = Get(context.settings);
	const std::string bytes = Get(file);
	Check(RenameProfile(context, second, "Renamed", error), "rename an existing player");
	view = Read(context);
	Check(view.profiles[1].id == second && view.profiles[1].name == "Renamed", "rename preserves identity");
	Check(Get(context.settings) == settings, "rename preserves settings exactly");
	const auto renamed = Get(file);
	bool untouched = bytes.size() == renamed.size();
	for (size_t i = 0; untouched && i < bytes.size(); i++)
		if (i < 0x10 + 0xC8 + 40 || i >= 0x10 + 0xC8 + 72) untouched = bytes[i] == renamed[i];
	Check(untouched, "rename changes only the chosen name field, preserving header/avatar data");
	Check(fs::exists(file.string() + ".ecam-before"), "first original player list retained");
	Check(RemoveProfile(context, second, error), "remove a selected player from list");
	view = Read(context);
	Check(view.profiles.size() == 1 && view.profiles[0].id == first && view.profile == 0, "selection resolves to remaining valid player");
	Check(Get(fs::path(context.user) / "nand/user/save/0000000000000000" / second / "0100ABCDEF123000/main") == "save retained" && Get(context.settings) == settings,
		"removal does not delete saves or settings");
	for (int i = 0; i < 7; i++) Check(CreateProfile(context, "Player " + std::to_string(i), error), "fill available player slot");
	const auto full = Get(file);
	Check(Read(context).profiles.size() == 8 && !CreateProfile(context, "Ninth", error) && Get(file) == full, "eight-player limit leaves file unchanged");
	for (const auto& name : {std::string("   "), std::string("bad\nname"), std::string("\xC0\xAF"), std::string("\xED\xA0\x80")})
		Check(!RenameProfile(context, first, name, error) && Get(file) == full, "invalid player name refused without write");
	Check(RenameProfile(context, first, std::string(30, 'A') + "é", error), "UTF-8 name can be fitted to binary field");
	Check(Read(context).profiles[0].name == std::string(30, 'A'), "UTF-8 character is not cut in half");
	Put(file.string() + ".ecam-new", "stale staged data");
	const auto beforeStale = Get(file);
	Check(!RenameProfile(context, first, "blocked", error) && Get(file) == beforeStale, "stale staging blocks replacement");
	fs::remove(file.string() + ".ecam-new");
	auto inconsistent = Get(file); inconsistent[0x10 + 16] ^= 1; Put(file, inconsistent);
	Check(!RenameProfile(context, first, "blocked", error) && Get(file) == inconsistent, "inconsistent duplicated identity is refused");
	auto duplicate = full; duplicate.replace(0x10 + 0xC8, 32, full.substr(0x10, 32)); Put(file, duplicate);
	Check(!RemoveProfile(context, first, error) && Get(file) == duplicate, "duplicate player identity is refused");
	Put(file, "truncated");
	Check(!CreateProfile(context, "New", error) && Get(file) == "truncated", "malformed list is not replaced with a new one");
	fs::remove(file); Put(root / "outside", full); fs::create_symlink(root / "outside", file);
	Check(!RenameProfile(context, first, "blocked", error) && Get(root / "outside") == full, "symlinked list is refused");
	return failures ? 1 : 0;
}
