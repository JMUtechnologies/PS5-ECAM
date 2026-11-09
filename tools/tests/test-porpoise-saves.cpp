// SPDX-License-Identifier: GPL-3.0-or-later
// port/retro/porpoise_saves.cpp on made-up cards and folders. Argument: an empty folder to work in.
#include "retro/porpoise_saves.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ps5porpoise;
using ps5titlesaves::Container;
using ps5titlesaves::TitleSave;

static int failures = 0;
#define CHECK(what) do { const bool ok_ = (what); std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", #what); failures += !ok_; } while (0)

static void Put(std::vector<uint8_t>& v, size_t at, const char* text) { std::memcpy(v.data() + at, text, std::strlen(text)); }
static void Be(std::vector<uint8_t>& v, size_t at, uint32_t x, int n) { for (int i = 0; i < n; i++) v[at + i] = uint8_t(x >> (8 * (n - 1 - i))); }

static std::vector<uint8_t> Entry(const char* code, const char* name, int blocks, uint32_t comment)
{
	std::vector<uint8_t> e(64, 0);
	Put(e, 0, code);
	Put(e, 8, name);
	Be(e, 0x38, blocks, 2);
	Be(e, 0x3C, comment, 4);
	return e;
}
static void Write(const fs::path& path, const std::vector<uint8_t>& data)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary).write((const char*)data.data(), data.size());
}
static std::vector<uint8_t> Gci(const char* code, const char* title, uint32_t comment = 0)
{
	std::vector<uint8_t> file = Entry(code, "file", 2, comment);
	file.resize(64 + 0x4000, 0);
	if (comment < 0x4000)
		Put(file, 64 + comment, title);
	return file;
}

int main(int, char** argv)
{
	const fs::path work = argv[1];
	const std::string saves = (work / "saves").string(), drive = (work / "usb0").string();
	Write(work / "saves/User/GC/USA/Card A/01-GZLE-z.gci", Gci("GZLE01", "Zelda"));
	Write(work / "saves/User/GC/USA/Card A/01-GAFE-a.gci", Gci("GAFE01", "Animal"));
	// a comment past its end, and an entry too short, must not be read
	Write(work / "saves/User/GC/USA/Card A/bad-comment.gci", Gci("GBAD01", "", 0xFFFFFFF0));
	Write(work / "saves/User/GC/USA/Card A/short.gci", std::vector<uint8_t>(10, 0));

	// a banner and an icon in RGB5A3; another whose pictures run past the file's end
	{
		std::vector<uint8_t> file = Entry("GPIC01", "pic", 1, 0x4000);
		file[7] = 2;
		Be(file, 0x2C, 0, 4);
		Be(file, 0x30, 2, 2);
		Be(file, 0x32, 1, 2);
		file.resize(64 + 0x2000, 0xFF);
		Write(work / "saves/User/GC/USA/Card A/pic.gci", file);
		std::vector<uint8_t> past = file;
		Be(past, 0x2C, 0x1FF0, 4);
		Write(work / "saves/User/GC/USA/Card A/past.gci", past);
		const auto picked = ReadCards(saves);
		int full = 0, bare = 0;
		for (const TitleSave& save : picked[0].saves)
			if (save.code == "GPIC01")
				(save.banner.width == 96 && save.icon.width == 32 && save.icon.rgba[3] == 255 ? full : bare)++;
		CHECK(full == 1 && bare == 1);
		fs::remove(work / "saves/User/GC/USA/Card A/pic.gci");
		fs::remove(work / "saves/User/GC/USA/Card A/past.gci");
	}

	// a card image in slot B: entry 0 current, its one block at 5, the chain ended
	std::vector<uint8_t> image(0x2000 * 7, 0xFF);
	Be(image, 0x22, 4, 2);
	Be(image, 0x2000 * 3 + 4, 1, 2);
	Be(image, 0x2000 * 4 + 4, 0, 2); // the older table
	Be(image, 0x2000 * 3 + 6, 50, 2);
	std::vector<uint8_t> entry = Entry("GIMG01", "image", 1, 0);
	Be(entry, 0x36, 5, 2);
	std::memcpy(&image[0x2000], entry.data(), 64);
	std::memset(&image[0x2000 * 5], 0, 64);
	std::memcpy(&image[0x2000 * 5], "Image title", 11);
	Write(work / "saves/User/GC/MemoryCardB.USA.raw", image);

	const auto cards = ReadCards(saves);
	CHECK(cards.size() == 2);
	CHECK(cards[0].id == "A" && cards[0].saves.size() == 3);
	CHECK(cards[0].saves[1].title == "Animal" || cards[0].saves[0].title == "Animal");
	CHECK(cards[0].freeBlocks == 2043 - 6);
	CHECK(cards[1].id == "B" && cards[1].folder.empty() && cards[1].saves.size() == 1);
	CHECK(cards[1].saves[0].title == "Image title" && !cards[1].saves[0].separate);
	CHECK(cards[1].totalBlocks == 59 && cards[1].freeBlocks == 50);

	// the Wii's: a banner's name in UTF-16, a folder that isn't a title, one with nothing
	std::vector<uint8_t> banner(0xA0, 0);
	Put(banner, 0, "WIBN");
	banner[0x21] = 'M'; banner[0x23] = 'K';
	Write(work / "saves/User/Wii/title/00010000/524d4345/data/banner.bin", banner);
	Write(work / "saves/User/Wii/title/00010000/524d4345/data/sub/save.dat", {1, 2, 3});
	fs::create_directories(work / "saves/User/Wii/title/00010000/.Trashes/data");
	fs::create_directories(work / "saves/User/Wii/title/00010000/nothex!!/data");
	fs::create_directories(work / "saves/User/Wii/title/00010000/524d4346/data");
	const Container wii = ReadWiiSaves(saves);
	CHECK(wii.saves.size() == 1 && wii.saves[0].title == "MK" && wii.saves[0].code == "RMCE");

	// out to a drive, Porpoise's shape
	std::string where, error;
	fs::create_directories(drive);
	const TitleSave& gci = cards[0].saves[0];
	CHECK(ExportSave(saves, gci, drive, "stamp1", where, error));
	CHECK(fs::exists(where) && where.find("PS5CEMU-HAR-saves/stamp1/retro/porpoise/gc/USA/Card A/") != std::string::npos);
	CHECK(fs::exists(drive + "/PS5CEMU-HAR-saves/stamp1/backup.json"));
	CHECK(!ExportSave(saves, cards[1].saves[0], drive, "stamp2", where, error));
	CHECK(ExportSave(saves, wii.saves[0], drive, "stamp3", where, error));
	CHECK(fs::exists(where + "/sub/save.dat") && where.find("retro/porpoise/wii/title/00010000/524d4345/data") != std::string::npos);
	CHECK(ExportCard(saves, cards[0], drive, "stamp4", where, error) && fs::exists(where + "/01-GZLE-z.gci"));
	CHECK(ExportCard(saves, cards[1], drive, "stamp5", where, error) && fs::is_regular_file(where));
	// an empty slot is there, beside the other's card
	fs::rename(work / "saves/User/GC/MemoryCardB.USA.raw", work / "image.raw");
	const auto emptyB = ReadCards(saves);
	CHECK(emptyB.size() == 2 && emptyB[1].saves.empty() && emptyB[1].folder.find("/USA/Card B") != std::string::npos);
	// by region, USA, EUR, JAP: each card gets its other slot in its region
	Write(work / "saves/User/GC/JAP/Card A/j.gci", Gci("GJAJ01", "Japan"));
	Write(work / "saves/User/GC/EUR/Card B/e.gci", Gci("GEUP01", "Europe"));
	const auto regions = ReadCards(saves);
	CHECK(regions.size() == 6 && regions[0].region == "USA" && regions[0].id == "A" && regions[1].id == "B" && regions[1].saves.empty());
	CHECK(regions[2].region == "EUR" && regions[2].id == "A" && regions[2].saves.empty() && regions[3].id == "B" && regions[3].saves.size() == 1);
	CHECK(regions[4].region == "JAP" && regions[4].id == "A" && regions[5].id == "B" && regions[5].saves.empty());
	fs::remove_all(work / "saves/User/GC/JAP");
	fs::remove_all(work / "saves/User/GC/EUR");
	const fs::path usa = work / "saves/User/GC/USA", aside = work / "usa-aside";
	fs::rename(usa, aside);
	// a lone B: its A beside it, and no other region's
	Write(work / "saves/User/GC/EUR/Card B/e.gci", Gci("GEUP01", "Europe"));
	const auto loneB = ReadCards(saves);
	CHECK(loneB.size() == 2 && loneB[0].id == "A" && loneB[0].region == "EUR" && loneB[0].saves.empty() && loneB[1].id == "B");
	fs::remove_all(work / "saves/User/GC/EUR");
	// an A alone: its B beside it
	Write(work / "saves/User/GC/EUR/Card A/e.gci", Gci("GEUP01", "Europe"));
	const auto matched = ReadCards(saves);
	CHECK(matched.size() == 2 && matched[1].id == "B" && matched[1].region == "EUR" && matched[1].saves.empty());
	// both in EUR, nothing in USA: no spare A
	Write(work / "saves/User/GC/EUR/Card B/f.gci", Gci("GEUQ01", "Europe 2"));
	const auto noExtra = ReadCards(saves);
	CHECK(noExtra.size() == 2 && noExtra[0].region == "EUR" && noExtra[1].region == "EUR");
	// an empty folder that's there counts, and gets its B
	fs::create_directories(work / "saves/User/GC/USA/Card A");
	const auto emptyThere = ReadCards(saves);
	CHECK(emptyThere.size() == 4 && emptyThere[0].region == "USA" && emptyThere[0].saves.empty() && emptyThere[1].id == "B");
	fs::remove_all(work / "saves/User/GC/USA");
	fs::remove_all(work / "saves/User/GC/EUR");
	// no card at all
	const auto noCards = ReadCards(saves);
	CHECK(noCards.size() == 2 && noCards[0].id == "A" && noCards[0].region == "USA" && noCards[1].id == "B" && noCards[1].region == "USA");
	fs::rename(aside, usa);
	fs::rename(work / "image.raw", work / "saves/User/GC/MemoryCardB.USA.raw");
	std::string none;
	CHECK(!ExportSave(saves, gci, none, "s", where, error));

	// moved aside, never deleted; then back in from the drive
	const std::string moved = MoveSaveAside(saves, gci, "stamp", error);
	CHECK(!moved.empty() && fs::exists(moved) && !fs::exists(gci.path));
	CHECK(MoveSaveAside(saves, cards[1].saves[0], "stamp", error).empty());
	const std::string movedWii = MoveSaveAside(saves, wii.saves[0], "stamp", error);
	CHECK(!movedWii.empty() && ReadWiiSaves(saves).saves.empty());

	Imported in;
	CHECK(ImportSaves(saves, cards[0].folder, drive, in, error));
	CHECK(in.gameCube >= 1 && in.wii >= 1);
	CHECK(fs::exists(gci.path));
	CHECK(ReadWiiSaves(saves).saves.size() == 1);
	// a save from Porpoise's own export still comes in
	Write(drive + "/Porpoise Saves/GameCube/old.gci", Gci("GOLD01", "Old"));
	Imported old;
	CHECK(ImportSaves(saves, cards[0].folder, drive, old, error) && old.gameCube == 1);
	CHECK(fs::exists(fs::path(cards[0].folder) / "old.gci"));
	Imported again;
	CHECK(ImportSaves(saves, cards[0].folder, drive, again, error) && again.skipped >= 2 && again.gameCube == 0 && again.wii == 0);

	// one save, one card, from the drive
	const auto usbSaves = FindUsbSaves(drive, false);
	CHECK(!usbSaves.empty());
	const fs::path oneCard = work / "one-card";
	CHECK(ImportOneSave(saves, usbSaves[0], oneCard.string(), error) && fs::exists(oneCard / usbSaves[0].relative));
	CHECK(!ImportOneSave(saves, usbSaves[0], oneCard.string(), error));
	const auto usbCards = FindUsbCards(drive);
	CHECK(!usbCards.empty());
	Imported fromCard;
	CHECK(ImportCard(usbCards[0], (work / "two-card").string(), fromCard, error) && fromCard.gameCube >= 1);
	CHECK(!FindUsbSaves(drive, true).empty());
	// a drive's path file never leads out of the title folders
	Write(drive + "/Porpoise Saves/Wii/evil/x.dat", {1});
	std::ofstream(drive + "/Porpoise Saves/Wii/evil/porpoise-wii-path.txt") << "title/../../../evil\n";
	Imported evil;
	CHECK(ImportSaves(saves, cards[0].folder, drive, evil, error) && !fs::exists(work / "evil"));
	return failures ? 1 : 0;
}
