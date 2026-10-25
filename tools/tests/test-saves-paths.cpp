// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real backup/restore implementation against a temporary backup folder. Including
// the implementation lets this test bypass USB discovery only; no console mount is touched.
#include "app/paths.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace ps5lang { const char* Tr(const char* e) { return e; } std::string Tr(const std::string& e) { return e; } }
namespace ps5log { void Write(std::string_view) {} }
namespace ps5eden { std::string DataFolder() { return ecam_layout::System(ps5paths::Root(), "eden"); } }
#include "app/saves.cpp"
static void Check(bool ok, const char* what)
{
    if (!ok) throw std::runtime_error(what);
    std::cout << "PASS  " << what << '\n';
}
static void Put(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
static std::string Get(const fs::path& p)
{
    std::ifstream file(p);
    return {std::istreambuf_iterator<char>(file), {}};
}
static void Scenario(const fs::path& base, bool legacy, bool retained = false)
{
    const fs::path root = base / "data", backup = base / "backup";
    ps5paths::detail::RootSlot() = root.string();
    const fs::path cemu = legacy ? root : base / "cemu";
    const fs::path azahar = legacy ? root / "azahar" : base / "azahar";
    const fs::path melonds = legacy ? root / "melonds" : base / "melonds";
    const fs::path eden = legacy ? root / "eden" : base / "eden";
    const fs::path save = cemu / "mlc01/usr/save/00050000/12345678/user/80000001/save.dat";
    Put(save, "original save");
    Put(cemu / "settings.xml", "original settings");
    Put(cemu / "controllerProfiles/player.xml", "original profile");
    Put(cemu / "mlc01/usr/title/0005000e/12345678/content/update.bin", "installed update");
    Put(cemu / "mlc01/usr/title/0005000c/12345678/content/dlc.bin", "installed DLC");
    Put(azahar / "nand/data/save.bin", "3DS system save");
    Put(melonds / "saves/Game.sav", "DS save");
    Put(eden / "user/nand/user/save/game/data.bin", "Switch save");
    Put(root / "ps5cemu.json", "launcher settings");
    Put(base / "cemu/games/Game.wua", "game");
    if (retained)
    {
        Put(root / "settings.xml", "retained legacy settings");
        Put(root / "mlc01/usr/save/00050000/12345678/user/80000001/save.dat", "retained legacy save");
        Put(root / "azahar/nand/data/save.bin", "retained legacy 3DS save");
        Put(root / "melonds/saves/Game.sav", "retained legacy DS save");
        Put(root / "eden/user/nand/user/save/game/data.bin", "retained legacy Switch save");
        for (const fs::path& folder : {cemu, azahar, melonds, eden})
            Put(folder / ecam_layout::kMarker, ecam_layout::kVersion);
    }
    ps5saves::Run(backup, false, std::nullopt);
    Check(ps5saves::Current().state == ps5saves::Status::State::Done, legacy ? "legacy backup completed" : "system-folder backup completed");
    Check(Get(backup / "wiiu/save/00050000/12345678/user/80000001/save.dat") == "original save", "Wii U backup format remains compatible");
    Check(Get(backup / "settings/settings.xml") == "original settings" && Get(backup / "settings/controllerProfiles/player.xml") == "original profile", "Cemu settings and profiles use the selected layout");
    Check(Get(backup / "3ds/nand/save.bin") == "3DS system save" && Get(backup / "ds/saves/Game.sav") == "DS save" && Get(backup / "switch/save/game/data.bin") == "Switch save", "other systems' saves remain isolated");
    Check(!fs::exists(backup / "games") && !fs::exists(backup / "wiiu/save/../title"), "backup excludes games, installed updates and DLC");
    ps5saves::GameRef game; game.side = 0; game.titleId = 0x0005000012345678;
    auto saves = ps5saves::FindGameSaves(game);
    Check(saves.size() == 1 && saves[0].files == 1 && saves[0].paths[0] == save.parent_path().string(), "individual Wii U saves use the selected MLC");
    Put(save, "newer save");
    Put(cemu / "settings.xml", "newer settings");
    ps5saves::Backup restore; restore.folder = backup.string();
    std::string error;
    Check(ps5saves::StartRestore(restore, error), "compatible backup restore starts");
    ps5saves::Finish();
    Check(ps5saves::Current().state == ps5saves::Status::State::Done && Get(save) == "original save" && Get(cemu / "settings.xml") == "original settings", "restored files return to the selected layout");
    bool keptSave = false, keptSettings = false;
    for (const auto& entry : fs::recursive_directory_iterator(base))
    {
        if (!entry.is_regular_file() || entry.path().string().find("/saves-before/") == std::string::npos) continue;
        keptSave |= Get(entry.path()) == "newer save";
        keptSettings |= Get(entry.path()) == "newer settings";
    }
    Check(keptSave && keptSettings, "restore preserves replaced saves and settings in saves-before");
    if (retained)
        Check(Get(root / "mlc01/usr/save/00050000/12345678/user/80000001/save.dat") == "retained legacy save" && Get(root / "settings.xml") == "retained legacy settings" && Get(root / "azahar/nand/data/save.bin") == "retained legacy 3DS save" && Get(root / "melonds/saves/Game.sav") == "retained legacy DS save" && Get(root / "eden/user/nand/user/save/game/data.bin") == "retained legacy Switch save", "activated backup/restore leaves all four retained original installations untouched");
    Check(Get(cemu / "mlc01/usr/title/0005000e/12345678/content/update.bin") == "installed update" && Get(cemu / "mlc01/usr/title/0005000c/12345678/content/dlc.bin") == "installed DLC", "restore leaves installed updates and DLC intact");
}
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const std::string mode = argv[2];
    if (mode == "legacy" || mode == "new" || mode == "migrated")
    {
        Scenario(fs::path(argv[1]) / mode, mode == "legacy", mode == "migrated");
        return 0;
    }
    if (mode != "restore") return 2;
    // A pre-layout backup can be restored directly into a fresh system-folder install.
    const fs::path root = fs::path(argv[1]) / "fresh";
    ps5paths::detail::RootSlot() = root.string();
    ps5saves::Backup backup; backup.folder = (fs::path(argv[1]) / "legacy/backup").string();
    std::string error;
    Check(ps5saves::StartRestore(backup, error), "legacy-format restore into a fresh install starts");
    ps5saves::Finish();
    Check(Get(root.parent_path() / "cemu/settings.xml") == "original settings" && Get(root.parent_path() / "cemu/mlc01/usr/save/00050000/12345678/user/80000001/save.dat") == "original save", "legacy-format backups restore into cemu without root-level Cemu files");
}
