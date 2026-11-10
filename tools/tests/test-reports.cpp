// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the reports (port/app/reports.cpp) on a PC, run by test-reports.sh: what the scrubber takes
// out and what it leaves, a report made from a log full of made-up personal data with none of it in the
// result, the session's four endings, the player's no deleting everything at once, and the outbox's
// limits. The folder is argv[1], a folder of the test's.
#include "app/reports.h"

#include <rapidjson/document.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ps5reports;
namespace fs = std::filesystem;

static int failures = 0;
static void Check(bool ok, const std::string& what)
{
	std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
	failures += !ok;
}
static bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Everything here is made up. Each is something a log could hold and a report must not.
static const char* const kPrivate[] = {
	"Jordan Okafor",		  // a PS5 user's name
	"jordan_ok_77",			  // a PSN online ID
	"Okafor",				  // in a folder's name
	"RetroJordan",			  // a RetroAchievements user
	"kq8Zt2pLm4Xw9vBn",		  // its token
	"0f1e2d3c4b5a69788796a5b4c3d2e1f0", // a SteamGridDB key
	"JordansRoom",			  // a room's name
	"JO-Player",			  // a room nickname
	"OkaforHome-5G",		  // a Wi-Fi name
	"192.168.1.37",			  // the console's address
	"10.0.0.2",
	"fe80::1c2d:3eff:fe4f:5a6b",
	"2a02:8108:1234:5678:9abc:def0:1234:5678",
	"a4:5e:60:c1:9b:7d",	  // its MAC address
	"A4-5E-60-C1-9B-7D",
	"F31A01A9B10234567",	  // its serial
	"jordan.okafor@example.com",
	"nas-okafor",			  // a share's machine
	"hunter2secret",		  // a share's password
	"Family Games",			  // folders
	"jordans stuff",
	"Private (Jordan)",
	"8123456789012345678",	  // a PSN account ID
};

static const char* const kLog =
	"[main] firmware 11.60 (0x11600000)\n"
	"[pad] P1: user \"Jordan Okafor\" (0x1000a2b3), DualSense\n"
	"[pad] P1 signed in as jordan_ok_77\n"
	"[psn] online id: jordan_ok_77, account id: 8123456789012345678\n"
	"[paths] games in /mnt/usb0/Family Games/Okafor/Wii U\n"
	"[main] Mario Kart 8 starts from its copy unpacked from /mnt/usb0/jordans stuff/Private (Jordan)/Mario Kart 8.wua\n"
	"[net] address 192.168.1.37, gateway 10.0.0.2, mac a4:5e:60:c1:9b:7d (A4-5E-60-C1-9B-7D)\n"
	"[net] ipv6 fe80::1c2d:3eff:fe4f:5a6b%eth0 and 2a02:8108:1234:5678:9abc:def0:1234:5678\n"
	"[net] wifi ssid: OkaforHome-5G\n"
	"[console] serial: F31A01A9B10234567\n"
	"[shares] smb://jordan:hunter2secret@nas-okafor/Family Games/Okafor/Zelda.wua mounted\n"
	"[shares] smb://nas-okafor\n"
	"[shares] \\\\nas-okafor\\Family Games\\Okafor\n"
	"[ra] signed in as RetroJordan, token kq8Zt2pLm4Xw9vBn\n"
	"[ra] user=RetroJordan&token=kq8Zt2pLm4Xw9vBn\n"
	"[covers] SteamGridDB key 0f1e2d3c4b5a69788796a5b4c3d2e1f0\n"
	"[rooms] room: JordansRoom, nickname: JO-Player\n"
	"[rooms] joined JordansRoom as JO-Player at 192.168.1.37:24872\n"
	"[mail] jordan.okafor@example.com\n"
	"[saves] /data/ps5cemu/mlc01/usr/save/00050000/1010ec00/user/80000001/userdata.dat written\n"
	"[driver] radv/ps5 submissions: 120 in 10 s, 30/60 fps\n"
	"[crash] SIGSEGV at 0x0000000801234567 in ps5emu::Run, fault address 0x10\n"
	"[main] Mario Kart 8 played for 42 min\n";

static void Scrubbing()
{
	const std::vector<std::string> secrets = {"Jordan Okafor", "jordan_ok_77", "RetroJordan", "kq8Zt2pLm4Xw9vBn", "0f1e2d3c4b5a69788796a5b4c3d2e1f0",
		"JordansRoom", "JO-Player", "nas-okafor", "hunter2secret", "jordan"};
	const std::string out = Scrub(kLog, secrets);
	for (const char* item : kPrivate)
		Check(!Has(out, item), std::string("the scrubbed log has no \"") + item + "\"");
	// with no list of what the app knows, the patterns alone still take the addresses, the folders and
	// what is named
	const std::string bare = Scrub(kLog);
	for (const char* item : {"192.168.1.37", "10.0.0.2", "fe80::1c2d", "2a02:8108", "a4:5e:60:c1:9b:7d", "A4-5E-60-C1-9B-7D", "F31A01A9B10234567",
			 "jordan.okafor@example.com", "Family Games", "jordans stuff", "Private (Jordan)", "OkaforHome-5G", "hunter2secret", "nas-okafor",
			 "0f1e2d3c4b5a69788796a5b4c3d2e1f0", "8123456789012345678", "Jordan Okafor"})
		Check(!Has(bare, item), std::string("the patterns alone take out \"") + item + "\"");
	// what a report is for stays
	Check(Has(out, "[main] firmware 11.60"), "the firmware line stays");
	Check(Has(out, "<path>/Mario Kart 8.wua"), "a game's path is cut to its file's name");
	Check(Has(out, "<path>/userdata.dat written"), "and a save's to its file's name, with what the line says after");
		Check(Has(out, "[crash] SIGSEGV at 0x0000000801234567 in ps5emu::Run, fault address 0x10"), "the crash line stays whole");
	Check(Has(out, "played for 42 min"), "the play time stays");
	Check(Has(out, "<ip>") && Has(out, "<mac>") && Has(out, "<email>") && Has(bare, "<hex>") && Has(out, "<redacted>"), "each kind is marked where it was");
	Check(Has(out, "[driver] radv/ps5 submissions: 120 in 10 s, 30/60 fps"), "the driver's line stays whole");
	Check(Scrub("looked in games/3ds/Okafor/Zelda.3ds and 2026/11/09 is a date") == "looked in <path>", "a path with no slash before it goes too");
	Check(Scrub("12:34:56 ps5log::Line v1.2.3.4 0100f2c0115b6000") == "12:34:56 ps5log::Line v1.2.3.4 0100f2c0115b6000",
		"a time, a C++ name, a version and a title ID are left alone");
	Check(Scrub("") == "" && Scrub("\n") == "\n" && Scrub("a\r\nb") == "a\nb", "empty text and line ends");
	Check(Scrub("Mario and Luigi", {"Mar", "io"}) == "Mario and Luigi", "a name is taken out as a whole word, never from inside one");
	Check(Scrub("see https://example.com/reports/Jordan/x for more") == "see <path>", "a URL's path goes");
	Check(Scrub("C:\\Users\\Jordan\\game.iso failed") == "<path>/game.iso failed", "a Windows path is cut to its file's name");

	Check(ModelClass("CFI-1216A") == "base" && ModelClass("CFI-2016") == "slim" && ModelClass("CFI-7016B 01Y") == "pro" && ModelClass("AMD Ryzen") == "unknown" &&
			  ModelClass("") == "unknown" && ModelClass("CFI-") == "unknown",
		"the model's class");
	Check(FirmwareShort("11.60 (0x11600000)") == "11.60" && FirmwareShort(" 09.000.000") == "9.00" && FirmwareShort("unknown") == "unknown" &&
			  FirmwareShort("4.03") == "4.03",
		"the firmware as its settings show it");
}

static Session Game(int64_t started, int nth = 0)
{
	Session session;
	session.side = "wiiu";
	session.emulator = "cemu";
	session.titleId = 0x000500001010ec00;
	session.name = "Mario Kart 8";
	session.started = started + nth;
	session.settings = {{"frameGeneration", 1}, {"upscaleFilter", 2}};
	return session;
}
static const Console kConsole = {"6.0.0", "76f7fd0", "11.60 (0x11600000)", "pro", true, true};
constexpr int64_t kNow = 1762700000;

static rapidjson::Document Parse(const std::string& path)
{
	rapidjson::Document json;
	const std::string text = Read(path);
	json.Parse(text.c_str(), text.size());
	return json;
}

static void Sessions(const std::string& folder)
{
	SetFolder(folder);
	const std::string log = folder + "/boot.log";
	std::ofstream(log) << kLog;

	// off: nothing is written, whatever happens
	SetEnabled(false);
	SetConsole(kConsole);
	Begin(Game(kNow));
	Ended(kNow + 60);
	Check(!Close({}, kNow) && !fs::exists(folder + "/current.json") && !fs::exists(folder + "/outbox") && !fs::exists(folder + "/install-id") &&
			  Pending(kNow).empty() && InstallId().empty(),
		"off: no ID, no session noted, no report");

	// on: an ID of the app's own making, kept
	SetEnabled(true);
	const std::string id = InstallId();
	Check(id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos, "on: an install ID of 32 hex digits");
	SetEnabled(true);
	Check(InstallId() == id, "and the same one the next time");
	Check(!Close({}, kNow) && Pending(kNow).empty(), "no game, no report");

	// a game left by its menu
	SetSecrets({"Jordan Okafor", "jordan_ok_77", "RetroJordan", "kq8Zt2pLm4Xw9vBn", "0f1e2d3c4b5a69788796a5b4c3d2e1f0", "JordansRoom", "JO-Player", "nas-okafor",
		"hunter2secret", "jordan"});
	Begin(Game(kNow));
	Ended(kNow + 2520);
	Check(Close({"", "", log}, kNow + 2600), "a game that ended: a report");
	std::vector<std::string> pending = Pending(kNow + 2600);
	Check(pending.size() == 1 && !fs::exists(folder + "/current.json"), "one in the outbox, the note gone");
	if (!pending.empty())
	{
		const rapidjson::Document json = Parse(pending[0]);
		Check(json.IsObject() && json["schema"].GetInt() == 1 && json["install"].GetString() == id && std::string(json["app"].GetString()) == "6.0.0" &&
				  std::string(json["commit"].GetString()) == "76f7fd0" && std::string(json["side"].GetString()) == "wiiu" &&
				  std::string(json["emulator"].GetString()) == "cemu" && std::string(json["titleId"].GetString()) == "000500001010ec00" &&
				  std::string(json["name"].GetString()) == "Mario Kart 8" && std::string(json["result"].GetString()) == "ended" &&
				  json["seconds"].GetInt64() == 2520 && std::string(json["firmware"].GetString()) == "11.60" && std::string(json["model"].GetString()) == "pro" &&
				  json["jit"].GetBool() && json["hen"].GetBool() && json["settings"]["frameGeneration"].GetInt() == 1 && json["settings"]["upscaleFilter"].GetInt() == 2 &&
				  !json.HasMember("error") && !json.HasMember("ended"),
			"its fields");
		// the test the feature stands on: a report made from a log full of personal data has none of it
		const std::string whole = Read(pending[0]);
		for (const char* item : kPrivate)
			Check(!Has(whole, item), std::string("the report has no \"") + item + "\"");
		Check(Has(json["log"].GetString(), "played for 42 min") && Has(json["log"].GetString(), "<path>/Mario Kart 8.wua"), "and its log still says what happened");
		Remove(pending[0]);
	}
	Check(Pending(kNow).empty(), "sent and removed");

	// one that did not start, one that crashed, one closed under the player
	Begin(Game(kNow + 1));
	Check(Close({"", "The game's files in /mnt/usb0/Family Games/Okafor/mk8.wua could not be read by Jordan Okafor", log}, kNow + 5), "a game that did not start");
	Begin(Game(kNow + 2));
	Check(Close({"SIGSEGV at 0x801234567, user \"Jordan Okafor\"", "", log}, kNow + 40), "a game that crashed");
	Begin(Game(kNow + 3));
	Check(Close({}, kNow + 50), "a game the console was closed under");
	pending = Pending(kNow + 50);
	Check(pending.size() == 3, "three reports, oldest first");
	// a game left from its menu, then the launcher crashed: the game ended
	Begin(Game(kNow + 3, 1));
	Ended(kNow + 604); // started at kNow + 4
	Check(Close({"SIGSEGV in the launcher", "", log}, kNow + 700), "a game that ended before a crash");
	{
		const std::vector<std::string> all = Pending(kNow + 700);
		const rapidjson::Document json = Parse(all.back());
		Check(all.size() == 4 && std::string(json["result"].GetString()) == "ended" && json["seconds"].GetInt64() == 600 && !json.HasMember("error"),
			"is reported as ended, with its time and no error");
		Remove(all.back());
	}
	if (pending.size() == 3)
	{
		const rapidjson::Document failed = Parse(pending[0]), crashed = Parse(pending[1]), closed = Parse(pending[2]);
		Check(std::string(failed["result"].GetString()) == "failed" && Has(failed["error"].GetString(), "<path>/mk8.wua could not be read by <redacted>") &&
				  !failed.HasMember("seconds"),
			"failed: why, scrubbed");
		Check(std::string(crashed["result"].GetString()) == "crashed" && Has(crashed["error"].GetString(), "SIGSEGV at 0x801234567") &&
				  !Has(crashed["error"].GetString(), "Jordan") && !crashed.HasMember("seconds"),
			"crashed: the crash line, scrubbed");
		Check(std::string(closed["result"].GetString()) == "closed" && !closed.HasMember("error") && !closed.HasMember("log"), "closed: nothing says how");
		for (const std::string& path : pending)
			for (const char* item : kPrivate)
				if (Has(Read(path), item))
					Check(false, std::string("a report has \"") + item + "\"");
	}

	// a long name and a long error are cut, at a character's start
	Session longName = Game(kNow + 4);
	longName.name = std::string(78, 'x') + "\xc3\xa9\xc3\xa9\xc3\xa9";
	Begin(longName);
	Check(Close({"", std::string(1000, 'z'), ""}, kNow + 60), "a long name and a long error");
	pending = Pending(kNow + 60);
	if (pending.size() == 4)
	{
		const rapidjson::Document json = Parse(pending[3]);
		Check(std::string(json["name"].GetString()) == std::string(78, 'x') + "\xc3\xa9" && json["error"].GetStringLength() == kErrorLength, "cut to their limits");
	}

	// the player's no: everything goes at once, and a session noted before it makes no report
	Begin(Game(kNow + 5));
	SetEnabled(false);
	Check(!fs::exists(folder + "/outbox") && !fs::exists(folder + "/current.json") && !fs::exists(folder + "/install-id") && Pending(kNow).empty() &&
			  Read(pending.empty() ? "" : pending[0]).empty(),
		"turned off: the outbox, the note and the ID are deleted");
	SetEnabled(true);
	Check(InstallId() != id && InstallId().size() == 32, "turned on again: another ID");
	// a note from under the old ID is not this answer's
	std::ofstream(folder + "/current.json") << "{\"schema\":1,\"install\":\"" << id << "\",\"started\":" << kNow << "}";
	Check(!Close({}, kNow) && Pending(kNow).empty() && !fs::exists(folder + "/current.json"), "a note from before the no makes no report");
	std::ofstream(folder + "/current.json") << "not json {";
	Check(!Close({}, kNow) && Pending(kNow).empty(), "nor does a broken one");

	// the outbox's limits: 20 reports, 14 days, 512 KB
	for (int i = 0; i < 25; i++)
	{
		Begin(Game(kNow + 100 + i));
		Close({}, kNow + 200);
	}
	pending = Pending(kNow + 200);
	Check(pending.size() == (size_t)kOutboxFiles && Has(pending[0], std::to_string(kNow + 105)), "25 reports: the 20 newest are kept");
	Check(Pending(kNow + 100 + 10 + kOutboxSeconds).size() == 15, "14 days on: the older ones are dropped");
	Check(Pending(kNow + 200 + kOutboxSeconds).empty(), "and then all of them");
	std::ofstream(folder + "/big.log") << std::string(400, 'x') << "\n" << [] {
		std::string lines;
		for (int i = 0; i < 400; i++)
			lines += "[gfx] a line of the boot log, number " + std::to_string(i) + ", as long as lines are: draw calls, textures\n";
		return lines;
	}();
	for (int i = 0; i < 60; i++)
	{
		Begin(Game(kNow + 300 + i));
		Close({"", "", folder + "/big.log"}, kNow + 400);
	}
	pending = Pending(kNow + 400);
	uint64_t bytes = 0;
	for (const std::string& path : pending)
		bytes += fs::file_size(path);
	Check(!pending.empty() && bytes <= kOutboxBytes && pending.size() <= (size_t)kOutboxFiles, "large reports: the outbox stays under 512 KB");
	if (!pending.empty())
	{
		const rapidjson::Document json = Parse(pending.back());
		const std::string text = json["log"].GetString();
		Check(text.size() <= kLogBytes && Has(text, "number 399") && !Has(text, "number 100,") && text.rfind("[gfx]", 0) == 0, "a report's log: the last 120 lines, whole");
	}
	SetEnabled(false);
}

int main(int argc, char* argv[])
{
	if (argc != 2)
		return 2;
	Scrubbing();
	Sessions(argv[1]);
	std::printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
