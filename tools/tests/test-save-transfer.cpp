// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden/save_transfer.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
using namespace ps5eden::transfer;
static int failures = 0;
static void Check(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
static void Put(const fs::path& path, const std::string& text) { fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << text; }
static std::string Get(const fs::path& path) { std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	fs::create_directories(argv[1]);
	const auto root = fs::canonical(argv[1]);
	const auto input = root / "input";
	const std::string title = "0100ABCDEF123000";
	const uint64_t id = 0x0100ABCDEF123000;
	Put(input / title / "account/main", "new account");
	Put(input / title / "device/main", "new device");
	Put(input / "0100ABCDEF124000/account/other", "other title");
	std::string error, kept;
	auto sources = Discover(input.string(), id, error);
	Check(sources.size() == 2 && error.empty(), "discover only requested title in split folder layout");
	const auto account = root / "nand/player/title", device = root / "nand/zero/title", recovery = root / "recovery";
	Put(account / "main", "old account"); Put(account / "stale", "old leftover"); Put(device / "main", "old device");
	Check(Import(sources, account.string(), device.string(), recovery.string(), kept, error), "account and device imported together");
	Check(Get(account / "main") == "new account" && Get(device / "main") == "new device" && !fs::exists(account / "stale"), "replace complete directories instead of merging stale files");
	Check(Get(fs::path(kept) / "account.before/main") == "old account" && Get(fs::path(kept) / "device.before/main") == "old device", "both prior saves retained");
	Check(Get(input / title / "account/main") == "new account", "source never modified");
	Check(Export(account.string(), device.string(), (root / "export" / title).string(), error), "plain-folder export");
	Check(Discover((root / "export").string(), id, error).size() == 2, "export is discoverable as a new import");
	Check(!Export(account.string(), device.string(), (root / "export" / title).string(), error) && Get(root / "export" / title / "account/main") == "new account", "existing exports never replaced");
	const auto firstBefore = Get(account / "main");
	const auto malformed = std::vector<Source>{{Kind::Account, (input / title / "account").string()}, {Kind::Device, (root / "missing").string()}};
	Check(!Import(malformed, account.string(), device.string(), recovery.string(), kept, error) && Get(account / "main") == firstBefore, "failure in second source leaves first destination unchanged");
	Check(!Import({sources[0], sources[0]}, account.string(), device.string(), recovery.string(), kept, error), "duplicate save types rejected");
	Check(!Import(sources, account.string(), account.string(), recovery.string(), kept, error), "overlapping destinations rejected");
	Check(!Import({{Kind::Account, account.string()}}, account.string(), device.string(), recovery.string(), kept, error), "source destination overlap rejected");
	fs::create_directory(root / "empty");
	Check(!Import({{Kind::Account, (root / "empty").string()}}, account.string(), device.string(), recovery.string(), kept, error), "empty save cannot erase existing files");
	fs::create_symlink(input / title / "account/main", input / title / "account/linked");
	Check(!Import(sources, account.string(), device.string(), recovery.string(), kept, error) && Get(account / "main") == firstBefore, "symlink inside source refused before replacement");
	fs::remove(input / title / "account/linked");
	fs::create_directory_symlink(input, root / "linked-root");
	Check(Discover((root / "linked-root").string(), id, error).empty(), "symlinked source ancestor refused");
	Check(Discover(input.string(), 0x5A12345678901234, error).empty(), "synthetic title rejected");
	// Standalone Ryujinx index layout, generated synthetic fixture; no copyrighted content.
	std::array<char, 12 + 0x8C * 2> index{};
	std::memcpy(index.data(), "IMKV", 4);
	for (int i = 0; i < 2; i++)
	{
		char* entry = index.data() + 12 + i * 0x8C;
		std::memcpy(entry, "IMEN", 4); std::memcpy(entry + 0x0C, &id, 8);
		entry[0x2C] = i ? 3 : 1;
		const uint64_t saveId = i + 1;
		std::memcpy(entry + 0x4C, &saveId, 8);
	}
	const auto ryujinx = root / "ryujinx";
	Put(ryujinx / "bis/system/save/8000000000000000/0/imkvdb.arc", std::string(index.data(), index.size()));
	Put(ryujinx / "bis/user/save/0000000000000001/0/main", "ryu account");
	Put(ryujinx / "bis/user/save/0000000000000002/0/main", "ryu device");
	sources = Discover(ryujinx.string(), id, error);
	Check(sources.size() == 2, "Ryujinx index identifies account and device by program ID");
	Check(Import(sources, account.string(), device.string(), recovery.string(), kept, error) && Get(account / "main") == "ryu account" && Get(device / "main") == "ryu device", "Ryujinx transfer uses exact indexed folders");
	Put(ryujinx / "bis/system/save/8000000000000000/0/imkvdb.arc", "bad index");
	Check(Discover(ryujinx.string(), id, error).empty(), "malformed Ryujinx index refused");
	return failures ? 1 : 0;
}
