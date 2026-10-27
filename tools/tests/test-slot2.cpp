// SPDX-License-Identifier: GPL-3.0-or-later
#include "melonds/slot2.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
static int failures = 0;
static void Check(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
static void Put(const fs::path& path, const std::string& text) { fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << text; }
int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	const fs::path root = argv[1];
	using ps5melonds::slot2::Load;
	Check(Load(root.string(), "Game", false).error.empty() && Load(root.string(), "Game", false).rom.empty(), "disabled default reads no cartridge");
	Check(!Load(root.string(), "Game", true).error.empty(), "enabled missing cartridge reports error");
	Put(root / "slot2/Game.gba", std::string(0xC0, 'R'));
	Put(root / "slot2/Game.sav", "supplied");
	auto cart = Load(root.string(), "Game", true);
	Check(cart.error.empty() && cart.rom.size() == 0xC0 && std::string(cart.save.begin(), cart.save.end()) == "supplied", "bounded ROM and supplied save loaded");
	Check(cart.savePath == (root / "slot2-saves/Game.sav").string(), "writes are directed to separate working save");
	Put(root / "slot2-saves/Game.sav", "working");
	cart = Load(root.string(), "Game", true);
	Check(std::string(cart.save.begin(), cart.save.end()) == "working", "existing working save takes precedence");
	std::string data;
	std::ifstream(root / "slot2/Game.sav") >> data;
	Check(data == "supplied", "supplied save is not modified");
	Put(root / "slot2/Tiny.gba", "short");
	Check(!Load(root.string(), "Tiny", true).error.empty(), "truncated ROM refused");
	Put(root / "slot2-saves/Game.sav", std::string((1u << 20) + 1, 'S'));
	Check(!Load(root.string(), "Game", true).error.empty(), "oversized save refused");
	Check(!Load(root.string(), "../Game", true).error.empty(), "path traversal refused");
	fs::remove(root / "slot2-saves/Game.sav");
	fs::remove(root / "slot2/Game.gba");
	fs::create_symlink(root / "slot2/Tiny.gba", root / "slot2/Game.gba");
	Check(!Load(root.string(), "Game", true).error.empty(), "symlinked cartridge refused");
	return failures ? 1 : 0;
}
