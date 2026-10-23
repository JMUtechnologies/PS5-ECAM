// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: an update's files (port/app/update_files.cpp) on a PC, run by test-update.sh over the ZIPs it
// makes: a release unpacked whole, each file as it was and in the archive's order, nothing outside its
// folder; a ZIP without eboot.bin, a file that is not a ZIP, a file that cannot be written and a stop for
// a game each end with their message; RemoveAll leaves nothing; InParallel takes each item once.
#include "app/update_files.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>

namespace ps5lang
{
	const char* Tr(const char* english) { return english; }
	std::string Tr(const std::string& english) { return english; }
}
namespace fs = std::filesystem;

static int failures = 0;
static void Check(bool ok, const char* what)
{
	std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}
static std::string Get(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), {});
}

int main(int argc, char** argv)
{
	using namespace ps5update;
	if (argc != 2)
		return 2;
	const fs::path work = argv[1];
	std::atomic<bool> stop{false};
	{
		std::vector<fs::path> files;
		std::string error;
		const bool ok = Unpack(work / "release.zip", work / "out", files, error, stop);
		Check(ok && error.empty(), "a release unpacks");
		std::ifstream list(work / "expected.txt");
		bool same = true, order = true, open = true;
		size_t i = 0;
		for (std::string name; std::getline(list, name); i++)
		{
			order = order && i < files.size() && files[i] == fs::path(name);
			same = same && Get(work / "out" / name) == Get(work / "source" / name);
			open = open && (fs::status(work / "out" / name).permissions() & fs::perms::all) == fs::perms::all;
		}
		Check(order && i == files.size(), "its files, in the archive's order");
		Check(same, "each file as it was");
		Check(open, "each file readable, writable and runnable by all");
		Check(!fs::exists(work / "evil.txt") && !fs::exists(work / "out/PPSA99360"), "nothing outside its folder, and no PPSA99360/ in it");
	}
	RemoveAll(work / "out");
	Check(!fs::exists(work / "out"), "RemoveAll leaves nothing");
	{
		std::vector<fs::path> files;
		std::string error;
		Check(!Unpack(work / "noeboot.zip", work / "out2", files, error, stop) && error == "the download has no eboot.bin" &&
				!fs::exists(work / "out2"),
			"a ZIP without eboot.bin: refused before anything is written");
	}
	{
		std::vector<fs::path> files;
		std::string error;
		Check(!Unpack(work / "expected.txt", work / "out3", files, error, stop) && error.rfind("the download is not a ZIP", 0) == 0,
			"a file that is not a ZIP: said so");
	}
	{
		// a file where a folder should be
		fs::create_directories(work / "out4");
		std::ofstream(work / "out4/assets") << "in the way";
		std::vector<fs::path> files;
		std::string error;
		const bool ok = Unpack(work / "release.zip", work / "out4", files, error, stop);
		Check(!ok && error.find("could not be unpacked") != std::string::npos, "a file that cannot be written: one message");
	}
	{
		stop = true;
		std::vector<fs::path> files;
		std::string error;
		Check(!Unpack(work / "release.zip", work / "out5", files, error, stop) && error == "stopped for a game", "a game starting: stopped");
		stop = false;
	}
	{
		std::vector<std::atomic<int>> seen(10000);
		bool workers = true;
		const bool ok = InParallel(seen.size(), [&](int worker, size_t i) {
			if (worker < 0 || worker >= kWorkers)
				workers = false;
			seen[i]++;
			return true;
		});
		bool once = true;
		for (auto& count : seen)
			once = once && count == 1;
		Check(ok && once && workers, "InParallel: each item once");
		std::atomic<size_t> done{0};
		Check(!InParallel(10000, [&](int, size_t i) {
			done++;
			return i != 50;
		}) && done < 10000,
			"InParallel: false once one fails, the rest left");
		Check(InParallel(0, [](int, size_t) { return false; }), "InParallel: nothing to do");
	}
	return failures ? 1 : 0;
}
