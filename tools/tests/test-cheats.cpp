// SPDX-License-Identifier: GPL-3.0-or-later
// The cheats' files as port/app/cheats.cpp reads and writes them: a .cht read and written back, a DS .cht made a
// melonDS .mch, each kind's cheats listed in its emulator's order and one turned on and off in the file, and
// codes.rc24.xyz's text made Porpoise's .txt. tools/tests/test-cheats.sh builds and runs it.
#include "app/cheats.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// what cheats.cpp asks of the app's other files, which this test does not link
namespace ps5eden
{
	std::string GameFilesFolder() { return "/nowhere/eden"; }
}
namespace ps5porpoise
{
	std::string Data() { return "/nowhere/porpoise"; }
	std::string GameKey(const std::string&) { return "GALE01"; }
}

namespace
{
	int failures = 0;

	void Check(bool ok, const std::string& what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		failures += ok ? 0 : 1;
	}

	std::string Read(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), {});
	}

	void Write(const std::string& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }
}

int main(int argc, char* argv[])
{
	if (argc < 2)
		return 2;
	const std::string work = argv[1];
	using namespace ps5cheats;

	// RetroArch's .cht, as libretro-database has them: a heading with no code, quoted values, off
	const std::string cht = "cheats = 3\n\ncheat0_desc = \"Codes for the hard mode\"\n\ncheat1_desc = \"Infinite Lives\"\n"
							"cheat1_code = \"020C35DC+E5950170+020C35E0+E3500000\"\ncheat1_enable = false\n\n"
							"cheat2_desc = \"Max Coins\"\ncheat2_code = \"7E0DBF63\"\ncheat2_enable = true\n";
	const auto parsed = ParseCht(cht);
	Check(parsed.size() == 3 && parsed[0].code.empty() && parsed[1].name == "Infinite Lives" && !parsed[1].on && parsed[2].on,
		".cht: three entries, a heading, one off, one on");
	Check(ParseCht(FormatCht(parsed)).size() == 3 && FormatCht(ParseCht(FormatCht(parsed))) == FormatCht(parsed), ".cht: written and read back the same");

	const std::string chtPath = work + "/game.cht";
	Write(chtPath, cht);
	auto listed = ListFile(Kind::Retro, chtPath);
	Check(listed.size() == 2 && listed[0].name == "Infinite Lives", ".cht: the menu lists the two codes, not the heading");
	std::string error;
	Check(SetOnInFile(Kind::Retro, chtPath, 0, true, error) && ListFile(Kind::Retro, chtPath)[0].on && ParseCht(Read(chtPath)).size() == 3,
		".cht: Infinite Lives turned on in the file, the heading kept");
	Check(SetOnInFile(Kind::Retro, chtPath, 1, false, error) && !ListFile(Kind::Retro, chtPath)[1].on, ".cht: Max Coins turned off");

	// a DS .cht as melonDS's .mch: the heading a CAT, each code's words two to a line, off
	const std::string mch = ChtToMch(cht);
	Check(mch.find("CAT Codes for the hard mode\n") != std::string::npos && mch.find("CODE 0 Infinite Lives\n020C35DC E5950170\n020C35E0 E3500000\n") != std::string::npos,
		".mch: a CAT and the code's words in pairs");
	Check(mch.find("Max Coins") == std::string::npos, ".mch: a code that is no Action Replay code (an odd word count) left out");
	const std::string mchPath = work + "/game.mch";
	Write(mchPath, "CAT Codes\n\nCODE 0 Moon Jump\n12345678 9ABCDEF0\n\nCODE 1 Big Head\n11111111 22222222\n");
	listed = ListFile(Kind::Nds, mchPath);
	Check(listed.size() == 2 && !listed[0].on && listed[1].on && listed[1].name == "Big Head", ".mch: melonDS's order and switches");
	Check(SetOnInFile(Kind::Nds, mchPath, 0, true, error) && Read(mchPath).find("CODE 1 Moon Jump") != std::string::npos && ListFile(Kind::Nds, mchPath)[0].on,
		".mch: Moon Jump turned on (CODE 1)");

	// Azahar's Gateway file: a name with no code lines is no cheat; *citra_enabled after the name turns one on
	const std::string gatewayPath = work + "/0004000000030200.txt";
	Write(gatewayPath, "[999k Hearts]\n00EE9820 000F423F\n\n[Just a note]\n\n[Reset]\n*citra_enabled\n*a note\n00EE9824 00000000\n");
	listed = ListFile(Kind::N3ds, gatewayPath);
	Check(listed.size() == 2 && listed[0].name == "999k Hearts" && !listed[0].on && listed[1].name == "Reset" && listed[1].on,
		"Gateway: Azahar's two cheats, the second on");
	Check(SetOnInFile(Kind::N3ds, gatewayPath, 0, true, error) && Read(gatewayPath).find("[999k Hearts]\n*citra_enabled\n00EE9820") != std::string::npos,
		"Gateway: the first turned on, *citra_enabled right after its name");
	Check(SetOnInFile(Kind::N3ds, gatewayPath, 1, false, error) && Read(gatewayPath).find("[Reset]\n*a note\n") != std::string::npos && !ListFile(Kind::N3ds, gatewayPath)[1].on,
		"Gateway: the second turned off, its note kept");

	// codes.rc24.xyz's text: the ID, the title, two versions, names with authors, a note
	const std::string rc24 = "GALE01\nA Game\n\nVersion 1.0\n\nNever Ends [Someone]\n041C3374 38030000\n\nCamera [Someone]\n04450F94 42000000\n"
							 "04450F98 42000000\nCo-author: Another\n\nVersion 1.1\n\nNever Ends [Someone]\n041C3CD8 38030000\n";
	const std::string txt = GeckoTxt(rc24, "GALE01");
	Check(txt.find("$(1.0) Never Ends [Someone]\n041C3374 38030000\n") != std::string::npos && txt.find("$(1.1) Never Ends [Someone]\n041C3CD8 38030000\n") != std::string::npos,
		"Gecko: each code named with its version, its author kept for Dolphin");
	Check(txt.find("*Co-author: Another\n") != std::string::npos, "Gecko: a note made a * line");
	Check(GeckoTxt(rc24, "RMCE01").empty() && GeckoTxt("<html>error</html>", "GALE01").empty(), "Gecko: an answer that is not the game's is nothing");

	std::printf("%s\n", failures ? "SOME CHEAT CHECKS FAILED" : "EVERY CHEAT CHECK PASSED");
	return failures ? 1 : 0;
}
