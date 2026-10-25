// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Back up to USB and Restore from USB (port/app/saves.cpp) on a PC, run by test-saves.sh:
// every side's saves, the Switch's and its profiles, and the settings go to /mnt/usb0, never the games;
// a restore puts them back, keeps what it replaced in saves-before and says the settings came back;
// a game's own backup has its saves alone. The data folder is PS5CEMU_DATA, a folder of the test's.
#include "app/saves.h"
#include "app/paths.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>
namespace ps5lang { const char* Tr(const char* e) { return e; } std::string Tr(const std::string& e) { return e; } }
namespace ps5log { void Write(std::string_view) {} }
namespace ps5eden { std::string DataFolder() { return std::string(PS5CEMU_DATA) + "/eden"; } }
namespace fs = std::filesystem;
static void Put(const fs::path& p, const std::string& text) { fs::create_directories(p.parent_path()); std::ofstream(p) << text; }
static std::string Get(const fs::path& p) { std::ifstream f(p); return std::string(std::istreambuf_iterator<char>(f), {}); }
static int failures = 0;
static void Check(bool ok, const char* what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what); failures += !ok; }
static ps5saves::Status Wait() { for (;;) { auto s = ps5saves::Current(); if (s.state != ps5saves::Status::State::Running) return s; std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }
int main() {
	const fs::path root = PS5CEMU_DATA, usb = "/mnt/usb0";
	Put(root / "ps5cemu.json", "{\"theme\": \"midnight\"}");
	Put(root / "settings.xml", "<content/>");
	Put(root / "controllerProfiles/player1.xml", "<p1/>");
	Put(root / "themes/mine.json", "{}");
	Put(root / "melonds/saves/Mario.sav", "save");
	Put(root / "eden/user/nand/user/save/0000000000000000/ABCD/0100000000010000/data.bin", "switch");
	Put(root / "eden/user/nand/system/save/8000000000000010/su/avators/profiles.dat", "profiles");
	Put(root / "games/Big Game.wua", "not backed up");
	std::string error;
	Check(ps5saves::StartBackup(error), "a backup starts");
	auto status = Wait();
	Check(status.state == ps5saves::Status::State::Done && !status.settingsRestored, "a backup: done, no settings restored");
	auto backups = ps5saves::FindBackups();
	Check(backups.size() == 1, "the backup is found");
	const fs::path folder = backups.front().folder;
	Check(Get(folder / "settings/ps5cemu.json").find("midnight") != std::string::npos, "ps5cemu.json in it");
	Check(fs::exists(folder / "settings/controllerProfiles/player1.xml") && fs::exists(folder / "settings/themes/mine.json") &&
		fs::exists(folder / "settings/settings.xml"), "Cemu's settings, its controller profiles and the themes in it");
	Check(fs::exists(folder / "switch/save/0000000000000000/ABCD/0100000000010000/data.bin") &&
		fs::exists(folder / "switch/profiles/su/avators/profiles.dat"), "the Switch's saves and profiles in it");
	Check(!fs::exists(folder / "games"), "never the games");
	// changed since, then restored
	Put(root / "ps5cemu.json", "{\"theme\": \"terminal\"}");
	Put(root / "melonds/saves/Mario.sav", "newer");
	Check(ps5saves::StartRestore(backups.front(), error), "a restore starts");
	status = Wait();
	Check(status.state == ps5saves::Status::State::Done && status.settingsRestored, "a restore: done, the settings restored, said");
	Check(Get(root / "ps5cemu.json").find("midnight") != std::string::npos, "ps5cemu.json is the backup's again");
	Check(Get(root / "melonds/saves/Mario.sav") == "save", "a save is the backup's again");
	bool kept = false;
	for (auto& e : fs::recursive_directory_iterator(root / "saves-before"))
		kept = kept || (e.path().filename() == "ps5cemu.json" && Get(e.path()).find("terminal") != std::string::npos);
	Check(kept, "the settings it replaced are kept in saves-before");
	// a game's backup: its saves alone
	ps5saves::GameRef game; game.side = 2; game.file = "/games/Mario.nds";
	Check(ps5saves::StartGameBackup(game, error), "a game's backup starts");
	Wait();
	bool settingsInGame = false;
	for (auto& b : ps5saves::FindBackups())
		settingsInGame = settingsInGame || (b.folder != folder.string() && fs::exists(fs::path(b.folder) / "settings"));
	Check(!settingsInGame, "a game's backup has no settings");
	ps5saves::Finish();
	// Resume where you left off's states: Azahar's slot 11 by the title ID, melonDS's by the file (an archive's without .tar)
	ps5saves::GameRef n3ds; n3ds.side = 1; n3ds.titleId = 0x0004000000055d00;
	Check(ps5saves::ResumeState(n3ds) == ps5paths::Azahar() + "/states/0004000000055D00.11.cst", "the 3DS's resume state");
	ps5saves::GameRef tar; tar.side = 2; tar.file = "/games/Mario.tar.gz";
	Check(ps5saves::ResumeState(tar) == ps5paths::MelonDs() + "/states/Mario.ml11", "the DS's, an archive's by its name");
	ps5saves::GameRef wiiu; wiiu.side = 0;
	Check(ps5saves::ResumeState(wiiu).empty(), "none for the Wii U");
	return failures != 0;
}
