// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: how games run (port/app/compatibility.cpp) on a PC, run by test-compat.sh: the feed
// tools/compat_feed.py made (argv[1]: the list and made-up reports), found by side and title ID, by name
// whatever its case and punctuation, never on another side; the kept copy chosen over the bundled one,
// a fetched feed kept with its ETag, one that does not parse turned away, and the reports found before a
// newer feed still there after it. argv[2] is a folder of the test's.
#include "app/compatibility.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace ps5compat;
namespace fs = std::filesystem;

static int failures = 0;
static void Check(bool ok, const std::string& what)
{
	std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
	failures += !ok;
}

static std::string Read(const std::string& path)
{
	std::ifstream file(path);
	std::ostringstream text;
	text << file.rdbuf();
	return text.str();
}

int main(int argc, char* argv[])
{
	if (argc != 3)
		return 2;
	const std::string feed = argv[1], folder = argv[2];
	const std::string cache = folder + "/cache/compatibility.json";

	// no kept copy: the bundled one
	SetPaths(feed, cache);
	const Report* kart = Find("wiiu", 0x000500001010ec00ULL, "something else");
	Check(kart && kart->status == "Playable" && kart->sessions == 14 && kart->ended == 13, "by side and title ID, with the reports' counts");
	const Report* botw = Find("wiiu", 0, "the legend of zelda breath of the wild");
	Check(botw && botw->status == "Playable", "by name, without case or punctuation");
	Check(Find("3ds", 0, "Mario Kart 8") == nullptr, "never another side's");
	const Report* world = Find("wiiu", 0x0005000010145d00ULL, "");
	Check(world && world->status == "Not playable" && std::string(Kind(world->status)) == "bad", "Not playable, marked bad");
	const Report* splat = Find("wiiu", 0, "Splatoon");
	Check(splat && std::string(Kind(splat->status)) == "warn", "Issues, marked warn");
	Check(Find("ds", 0, "Mario Kart DS") == nullptr, "a game one session told about: no status");
	Check(Find("wiiu", 0, "") == nullptr && Find("wiiu", 0x1234, "") == nullptr, "nothing for no name and an unknown ID");
	const int first = Generation();

	// a fetched feed: kept with its tag, read, the old reports still where they were
	const std::string fresh = R"({"schema":1,"generated":"2026-11-12T00:00:00Z","games":[)"
							  R"({"side":"wiiu","titleId":"000500001010ec00","name":"Mario Kart 8","status":"issues"},)"
							  R"({"side":"nes","name":"Super Mario Bros.","status":"playable"}]})";
	Check(Keep(fresh, "W/\"abc\""), "a fetched feed kept");
	Check(Generation() == first + 1, "a newer feed: the generation goes up");
	Check(Read(cache) == fresh && CachedTag() == "W/\"abc\"", "kept with its ETag");
	Check(kart->status == "Playable", "a report found before stays where it was");
	const Report* kartNow = Find("wiiu", 0x000500001010ec00ULL, "");
	Check(kartNow && kartNow->status == "Issues", "the newer feed's status found after");
	Check(Find("nes", 0, "SUPER MARIO BROS") != nullptr, "any side the feed names (a libretro one's)");
	Check(Find("wiiu", 0, "Splatoon") == nullptr, "what the newer feed lacks: gone");

	// one that does not parse: turned away, the one before still read
	Check(!Keep("<html>a captive portal</html>", "x"), "a page that is not the feed turned away");
	Check(!Take(R"({"schema":2,"games":[]})"), "another schema turned away");
	Check(Find("nes", 0, "Super Mario Bros.") != nullptr && CachedTag() == "W/\"abc\"", "the kept feed still in use, its tag kept");

	// a new launch: the kept copy over the bundled one
	SetPaths(feed, cache);
	const Report* kept = Find("wiiu", 0x000500001010ec00ULL, "");
	Check(kept && kept->status == "Issues", "a launch reads the kept copy before the bundled one");
	// a kept copy that does not read: the bundled one, and no tag sent
	{
		std::ofstream broken(cache, std::ios::trunc);
		broken << "{";
	}
	SetPaths(feed, cache);
	const Report* bundled = Find("wiiu", 0x000500001010ec00ULL, "");
	Check(bundled && bundled->status == "Playable" && CachedTag().empty(), "a broken kept copy: the bundled one, the whole feed asked for");
	// neither: nothing, no crash
	SetPaths(folder + "/none.json", "");
	Check(Find("wiiu", 0, "Mario Kart 8") == nullptr && CachePath().empty(), "no feed at all: no status");

	std::printf("%s\n", failures ? "test-compat: FAILED" : "test-compat: all passed");
	return failures ? 1 : 0;
}
