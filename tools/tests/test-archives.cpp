// SPDX-License-Identifier: GPL-3.0-or-later
// A PC test of games in archives (tools/tests/test-archives.sh): port/app/game_archives.cpp's walk and
// unpacking, and what the 3DS side's library (port/azahar/library.cpp) and the Wii U side's
// (port/app/wiiu_archives.cpp) list of the archives the script made, against stand-ins for the boot log,
// the menus' words and covers.cpp; and a .zip on a network share, through the /net layer
// (port/ps5/netfs_files.cpp, linked with the app's --wrap list) over a stand-in share as test-netfs.cpp's.
// Prints "ok" at the end.

#include "../../port/app/game_archives.h"
#include "../../port/app/lang.h"
#include "../../port/app/netfs.h"
#include "../../port/app/paths.h"
#include "../../port/app/wiiu_archives.h"
#include "../../port/azahar/library.h"
#include "../../port/ps5/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

#define CHECK(condition)                                                                                       \
	do                                                                                                         \
	{                                                                                                          \
		if (!(condition))                                                                                      \
		{                                                                                                      \
			std::fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition);                     \
			std::_Exit(1); /* (a scan's thread may still be there) */                                          \
		}                                                                                                      \
	} while (0)

// -- stand-ins --------------------------------------------------------------------------------------------

static std::mutex s_logMutex;
static std::vector<std::string> s_log;

namespace ps5log
{
	void Write(std::string_view line)
	{
		std::lock_guard lock(s_logMutex);
		s_log.emplace_back(line);
		std::fprintf(stderr, "%.*s\n", (int)line.size(), line.data());
	}
}

namespace ps5lang
{
	const char* Tr(const char* english) { return english; }
	std::string Tr(const std::string& english) { return english; }
	const char* TrC(const char*, const char* english) { return english; }
	std::string TrC(const char*, const std::string& english) { return english; }
	std::string Decimal(double value, int decimals)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%.*f", decimals, value);
		return text;
	}
	int NintendoLanguage() { return 1; } // English
	namespace detail
	{
		const char* Plural(long long count, const char*, const char* one, const char* other) { return count == 1 ? one : other; }
	}
}

namespace ps5emu
{
	// covers.cpp's, without the conversion: the TGA as the archive has it
	std::string CoverFile(uint64_t titleId, bool bootScreen)
	{
		return fmt::format("{}{}/{:016x}.tga", ps5paths::Covers(), bootScreen ? "/boot" : "", titleId);
	}
	bool WriteCover(uint64_t titleId, bool bootScreen, const std::vector<uint8_t>& tga)
	{
		const std::string path = CoverFile(titleId, bootScreen);
		fs::create_directories(fs::path(path).parent_path());
		std::ofstream(path, std::ios::binary).write((const char*)tga.data(), (std::streamsize)tga.size());
		return tga.size() > 18;
	}
}

// the stand-in share: /net/test is the 3DS side's folder of archives (s_share)
static std::string s_share;

static std::string Local(const std::string& path)
{
	const std::string prefix = "/net/test";
	return path.compare(0, prefix.size(), prefix) == 0 ? s_share + path.substr(prefix.size()) : std::string();
}

namespace ps5netfs
{
	bool stat(const std::string& path, bool* is_dir, std::uint64_t* size, std::int64_t* mtime)
	{
		const std::string local = Local(path);
		struct ::stat st{};
		if (local.empty() || ::stat(local.c_str(), &st) != 0) // a local path: the C library's own, past the layer
			return false;
		if (is_dir)
			*is_dir = S_ISDIR(st.st_mode);
		if (size)
			*size = (std::uint64_t)st.st_size;
		if (mtime)
			*mtime = st.st_mtime;
		return true;
	}

	bool list(const std::string& dir, std::vector<Entry>& out)
	{
		out.clear();
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(Local(dir), ec))
			out.push_back({entry.path().filename().string(), entry.is_directory(ec)});
		return !ec;
	}

	struct Reader::Impl
	{
		std::FILE* file = nullptr;
		std::uint64_t size = 0;
	};
	Reader::Reader() : impl_(new Impl) {}
	Reader::~Reader() { close(); }
	bool Reader::open(const std::string& path)
	{
		close();
		bool dir = false;
		if (!ps5netfs::stat(path, &dir, &impl_->size) || dir)
			return false;
		impl_->file = std::fopen(Local(path).c_str(), "rb");
		return impl_->file != nullptr;
	}
	void Reader::close()
	{
		if (impl_->file)
			std::fclose(impl_->file);
		impl_->file = nullptr;
	}
	bool Reader::is_open() const { return impl_->file != nullptr; }
	std::uint64_t Reader::size() const { return impl_->size; }
	std::int64_t Reader::read_some(std::uint64_t offset, void* out, std::uint64_t n)
	{
		if (std::fseek(impl_->file, (long)offset, SEEK_SET) != 0)
			return -1;
		return (std::int64_t)std::fread(out, 1, n, impl_->file);
	}
	bool Reader::read(std::uint64_t offset, void* out, std::uint64_t n)
	{
		return read_some(offset, out, n) == (std::int64_t)n;
	}
}

// -- helpers ----------------------------------------------------------------------------------------------

static std::string ReadFile(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

static size_t LogLines(const std::string& part)
{
	std::lock_guard lock(s_logMutex);
	return (size_t)std::count_if(s_log.begin(), s_log.end(), [&](const std::string& line) { return line.find(part) != std::string::npos; });
}

static const ps5emu::Game* Find(const std::vector<ps5emu::Game>& games, const std::string& file)
{
	for (const ps5emu::Game& game : games)
		if (game.path.filename() == file)
			return &game;
	return nullptr;
}

static std::vector<ps5emu::Game> Scan3ds(const std::string& folder)
{
	ps5azahar::StartScan(folder);
	while (ps5azahar::Scanning())
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	return ps5azahar::ListGames();
}

static std::vector<ps5emu::Game> ScanWiiu(const std::string& folder)
{
	ps5emu::archives::StartScan({folder});
	while (ps5emu::archives::Scanning())
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	return ps5emu::archives::Games();
}

static ps5archive::Source SourceOf(const ps5emu::Game& game)
{
	return {game.path.string(), game.archiveMember, game.archiveIndex, game.archiveSize, game.archiveFolder};
}

// What an unpacked title's folder holds, but the copy's own .used: its files' size
static uint64_t FolderBytes(const std::string& folder)
{
	uint64_t bytes = 0;
	for (const auto& entry : fs::recursive_directory_iterator(folder))
		if (entry.is_regular_file() && entry.path().filename() != ".used")
			bytes += entry.file_size();
	return bytes;
}

static bool Partials()
{
	for (const auto& entry : fs::directory_iterator(ps5archive::UnpackedRoot()))
		if (entry.path().extension() == ".partial")
			return true;
	return false;
}

int main(int argc, char* argv[])
{
	if (argc < 2)
		return 2;
	const std::string work = argv[1];
	ps5paths::detail::RootSlot() = work + "/data";
	fs::create_directories(work + "/data");
	const std::string n3ds = work + "/3ds", wiiu = work + "/wiiu", samples = work + "/samples/3ds";
	const bool seven = fs::exists(n3ds + "/Pokemon.7z");

	// -- names --------------------------------------------------------------------------------------------
	CHECK(ps5archive::IsArchive("/a/Game.ZIP") && ps5archive::IsArchive("x.tar.gz") && !ps5archive::IsArchive("Game.3ds"));
	CHECK(ps5archive::Stem("/games/Game (USA).tar.gz") == "Game (USA)" && ps5archive::Stem("Game.7z") == "Game");
	CHECK(ps5archive::Kind("a.zip") == "ZIP" && ps5archive::Kind("a.7Z") == "7Z" && ps5archive::Kind("a.tgz") == "TAR" && ps5archive::Kind("a.3ds").empty());

	// -- the walk -----------------------------------------------------------------------------------------
	{
		const std::string rom = ReadFile(samples + "/Super Mario 3D Land.3ds");
		std::vector<std::string> names;
		bool behind = true;
		std::string read;
		CHECK(ps5archive::Visit(n3ds + "/Mario.zip", [&](const ps5archive::Entry& entry, ps5archive::Stream& stream) {
			names.push_back(entry.name);
			if (entry.name == "Super Mario 3D Land.3ds")
			{
				CHECK(entry.index == 1 && entry.size == rom.size()); // the Mac's copy is the first entry
				char magic[4];
				CHECK(stream.ReadAt(0x100, magic, 4) && std::string(magic, 4) == "NCSD");
				behind = stream.ReadAt(0, magic, 4); // offsets only go up
				std::string rest(entry.size - stream.Position(), '\0');
				CHECK(stream.ReadAt(stream.Position(), rest.data(), rest.size()));
				read = rom.substr(0, 0x104) + rest;
			}
			return true;
		}));
		CHECK((names == std::vector<std::string>{"Super Mario 3D Land.3ds", "readme.txt"}));
		CHECK(!behind && read == rom);
		std::string error;
		CHECK(!ps5archive::Visit(n3ds + "/Broken.zip", [](const ps5archive::Entry&, ps5archive::Stream&) { return true; }, &error) && !error.empty());
		names.clear();
		CHECK(ps5archive::Visit(n3ds + "/Checkpoint.tar.gz", [&](const ps5archive::Entry& entry, ps5archive::Stream&) {
			names.push_back(entry.name);
			return true;
		}));
		CHECK((names == std::vector<std::string>{"Checkpoint/Checkpoint.3dsx"}));
	}

	// -- Azahar's compressed files, alone and in an archive -----------------------------------------------
	{
		const std::string compressed = work + "/3ds-z", zip = compressed + "/Compressed.zip";
		// what an earlier version kept of the archive, which read nothing in a compressed ROM: read again
		{
			fs::create_directories(work + "/data/azahar");
			ps5archive::Probes earlier(work + "/data/azahar/archives.bin");
			uint64_t size = 0;
			int64_t time = 0;
			CHECK(ps5archive::Probes::Stamp(zip, size, time));
			std::vector<std::string> fields(9);
			fields[0] = "0";
			earlier.Keep(zip, size, time, fields);
			earlier.Save();
		}
		const std::vector<ps5emu::Game> games = Scan3ds(compressed);
		const ps5emu::Game* kart = Find(games, "Mario Kart 7.zcci");
		CHECK(kart && kart->name == "Mario Kart 7" && kart->titleId == 0x0004000000030600 && kart->gameId == "AMKE" && kart->format == "Z3DS");
		CHECK(!ps5azahar::CoverPath(kart->titleId).empty());
		// the icon as the dump's own, from frames past the headers'
		const ps5azahar::Title packed = ps5azahar::Inspect(compressed + "/Mario Kart 7.zcci"), plain = ps5azahar::Inspect(samples + "/Mario Kart 7.cci");
		CHECK(packed.readable && !packed.encrypted && packed.boxId == "AMKE" && !packed.icon.empty() && packed.icon == plain.icon && packed.smdh == plain.smdh);
		const ps5emu::Game* emblem = Find(games, "Fire Emblem Awakening.zcxi");
		CHECK(emblem && emblem->name == "Fire Emblem Awakening" && emblem->titleId == 0x00040000000A0500 && emblem->gameId == "AFEE" &&
			emblem->format == "ZCXI");
		const ps5emu::Game* checkpoint = Find(games, "Checkpoint.z3dsx");
		CHECK(checkpoint && checkpoint->name == "Checkpoint" && checkpoint->format == "Z3DSX" && (checkpoint->titleId >> 60) == 0xF);
		const ps5emu::Game* kirby = Find(games, "Kirby Planet Robobot.zcia");
		CHECK(kirby && kirby->name == "Kirby: Planet Robobot" && kirby->titleId == 0x000400000017C100 && kirby->version == 1040 && kirby->format == "ZCIA");
		CHECK(!Find(games, "Kirby Planet Robobot Update.zcia"));
		const ps5emu::Game* renamed = Find(games, "Renamed.cci");
		CHECK(renamed && renamed->name == "Kid Icarus: Uprising" && renamed->titleId == 0x0004000000030200 && renamed->format == "Z3DS");
		const ps5emu::Game* mario = Find(games, "Compressed.zip");
		CHECK(mario && mario->Archived() && mario->name == "Super Mario 3D Land" && mario->titleId == 0x0004000000053F00 && mario->gameId == "AREE" &&
			mario->archiveMember == "Super Mario 3D Land.zcci" && mario->format == "ZIP");
		CHECK(LogLines("Compressed.zip: Super Mario 3D Land.zcci") == 1);
		if (seven)
		{
			const ps5emu::Game* pokemon = Find(games, "Pokemon.7z");
			CHECK(pokemon && pokemon->name == "Pokémon Ultra Sun" && pokemon->titleId == 0x00040000001B5000 && pokemon->archiveMember.empty());
		}
		// again: what this version kept of the archive, nothing read out of it
		const std::vector<ps5emu::Game> again = Scan3ds(compressed);
		const ps5emu::Game* cached = Find(again, "Compressed.zip");
		CHECK(cached && cached->name == "Super Mario 3D Land" && cached->gameId == "AREE" && LogLines("Compressed.zip: Super Mario 3D Land.zcci") == 1);
		// a CIA's SMDH from its end, then its TMD ahead of it
		const ps5azahar::Title alone = ps5azahar::Inspect(work + "/3ds-z-alone/Kirby.zcia"), cia = ps5azahar::Inspect(samples + "/Kirby Planet Robobot.cia");
		CHECK(alone.readable && alone.format == "ZCIA" && alone.name == "Kirby: Planet Robobot" && alone.titleId == 0x000400000017C100 && alone.version == 1040 &&
			!alone.icon.empty() && alone.icon == cia.icon);
	}

	// -- the 3DS side's library ---------------------------------------------------------------------------
	{
		const std::vector<ps5emu::Game> games = Scan3ds(n3ds);
		const ps5emu::Game* mario = Find(games, "Mario.zip");
		CHECK(mario && mario->Archived() && mario->name == "Super Mario 3D Land" && mario->titleId == 0x0004000000053F00 &&
			mario->gameId == "AREE" && mario->format == "ZIP" && mario->archiveIndex == 1 && mario->archiveMember == "Super Mario 3D Land.3ds" &&
			mario->archiveSize == fs::file_size(samples + "/Super Mario 3D Land.3ds") && !mario->archiveFolder);
		CHECK(fs::exists(fmt::format("{}/3ds/{:016x}.tga", ps5paths::Covers(), mario->titleId))); // its icon
		const ps5emu::Game* two = Find(games, "Two.zip");
		CHECK(two && two->name == "Fire Emblem Awakening" && two->archiveMember == "a/Fire Emblem Awakening.cxi");
		const ps5emu::Game* checkpoint = Find(games, "Checkpoint.tar.gz");
		CHECK(checkpoint && checkpoint->name == "Checkpoint" && checkpoint->format == "TAR" && (checkpoint->titleId >> 60) == 0xF);
		const ps5emu::Game* luigi = Find(games, "Luigi.zip");
		CHECK(luigi && luigi->format == "ENCRYPTED" && luigi->name == "Luigi");
		CHECK(!Find(games, "Kirby.zip") && !Find(games, "Broken.zip") && !Find(games, "Icarus.zip"));
		const ps5emu::Game* icarus = Find(games, "Kid Icarus Uprising.3ds");
		CHECK(icarus && !icarus->Archived());
		if (seven)
		{
			const ps5emu::Game* pokemon = Find(games, "Pokemon.7z");
			CHECK(pokemon && pokemon->name == "Pokémon Ultra Sun" && pokemon->archiveMember.empty() && pokemon->archiveIndex == 0 &&
				pokemon->format == "7Z");
		}
		CHECK(fs::exists(work + "/data/azahar/archives.bin"));
		// again: nothing read out of an archive that has not changed (but the one that is none)
		const size_t read = LogLines("/3ds/"), broken = LogLines("Broken.zip could not be read");
		const std::vector<ps5emu::Game> again = Scan3ds(n3ds);
		CHECK(again.size() == games.size() && LogLines("/3ds/") == read + 1 && LogLines("Broken.zip could not be read") == broken + 1);
		const ps5emu::Game* cached = Find(again, "Mario.zip");
		CHECK(cached && cached->name == mario->name && cached->archiveIndex == 1 && cached->gameId == "AREE");
	}

	// -- the Wii U side's ---------------------------------------------------------------------------------
	std::vector<ps5emu::Game> titles = ScanWiiu(wiiu);
	{
		const ps5emu::Game* splatoon = Find(titles, "Splatoon.zip");
		CHECK(splatoon && splatoon->titleId == 0x0005000010176900 && splatoon->name == "Splatoon Ink" && splatoon->gameId == "AGME01" &&
			splatoon->version == 0x10 && splatoon->archiveFolder && splatoon->archiveMember == "Splatoon [0005000010176900]/" &&
			splatoon->format == "ZIP");
		CHECK(fs::exists(ps5emu::CoverFile(splatoon->titleId, false)));
		const ps5emu::Game* two = Find(titles, "Two.tar.gz");
		CHECK(two && two->archiveMember == "A Game/" && two->titleId == 0x000500001010EC00 && two->name == "A");
		const ps5emu::Game* cased = Find(titles, "Case.zip");
		CHECK(cased && cased->archiveMember == "Case/" && cased->name == "The Wind Waker HD");
		CHECK(Find(titles, "Evil.zip"));
		CHECK(!Find(titles, "Update.zip") && !Find(titles, "NoContent.zip"));
		if (seven)
		{
			const ps5emu::Game* top = Find(titles, "Top.7z");
			CHECK(top && top->archiveMember.empty() && top->titleId == 0x0005000010145D00);
		}
		const size_t read = LogLines("/wiiu/");
		CHECK(ScanWiiu(wiiu).size() == titles.size() && LogLines("/wiiu/") == read);
	}

	// -- unpacking ----------------------------------------------------------------------------------------
	{
		const std::vector<ps5emu::Game> games = ps5azahar::ListGames();
		std::string path, error;
		ps5archive::Progress progress;

		// a ROM, then found again
		const ps5archive::Source mario = SourceOf(*Find(games, "Mario.zip"));
		CHECK(ps5archive::Unpacked(mario).empty());
		CHECK(ps5archive::Unpack(mario, &progress, path, error) && error.empty());
		CHECK(fs::path(path).filename() == "Super Mario 3D Land.3ds" && ReadFile(path) == ReadFile(samples + "/Super Mario 3D Land.3ds"));
		CHECK(progress.done == mario.size && progress.total == mario.size);
		CHECK(ps5archive::Unpacked(mario) == path);
		if (seven)
		{
			const ps5archive::Source pokemon = SourceOf(*Find(games, "Pokemon.7z"));
			CHECK(ps5archive::Unpack(pokemon, nullptr, path, error));
			CHECK(fs::path(path).filename() == "game.3ds" && ReadFile(path) == ReadFile(samples + "/Pokémon Ultra Sun.3ds"));
		}

		// a title's folder, its code, content and meta in small letters
		for (const char* archive : {"Splatoon.zip", "Case.zip", "Two.tar.gz", "Top.7z"})
		{
			const ps5emu::Game* title = Find(titles, archive);
			if (!title)
			{
				CHECK(!seven);
				continue;
			}
			CHECK(ps5archive::Unpack(SourceOf(*title), nullptr, path, error));
			CHECK(fs::is_regular_file(path + "/code/app.xml") && fs::is_regular_file(path + "/code/cos.xml") && fs::is_regular_file(path + "/meta/meta.xml"));
			const std::string pack = ReadFile(path + "/content/Common/Pack/x.pack");
			CHECK(pack.size() == 10240 && (uint8_t)pack[300] == 300 % 256);
			CHECK(FolderBytes(path) == title->archiveSize && ps5archive::Unpacked(SourceOf(*title)) == path);
		}
		CHECK(fs::is_regular_file(ps5archive::Unpacked(SourceOf(*Find(titles, "Case.zip"))) + "/code/Zelda.RPX"));

		// an entry that would leave the copy's folder: nothing of it written, anywhere
		CHECK(!ps5archive::Unpack(SourceOf(*Find(titles, "Evil.zip")), nullptr, path, error) && error.find("unsafe") != std::string::npos);
		CHECK(!fs::exists(ps5paths::Cache() + "/escape.txt") && !fs::exists(work + "/data/escape.txt") && !Partials());

		// stopped: nothing left, nothing said
		ps5archive::Progress stopped;
		stopped.cancel = true;
		ps5archive::Source two = SourceOf(*Find(games, "Two.zip"));
		CHECK(!ps5archive::Unpack(two, &stopped, path, error) && error.empty() && !Partials() && ps5archive::Unpacked(two).empty());

		// the archive no longer as the library read it
		two.size++;
		CHECK(!ps5archive::Unpack(two, nullptr, path, error) && error.find("changed") != std::string::npos && !Partials());

		// a .zip on a network share: libzip reads it through the C library's open, pread and fstat, which the
		// /net layer answers (a copy of its own: the archive's path is another)
		s_share = n3ds;
		ps5archive::Source shared = mario;
		shared.archive = "/net/test/Mario.zip";
		std::vector<std::string> names;
		CHECK(ps5archive::Visit(shared.archive, [&](const ps5archive::Entry& entry, ps5archive::Stream&) {
			names.push_back(entry.name);
			return true;
		}));
		CHECK((names == std::vector<std::string>{"Super Mario 3D Land.3ds", "readme.txt"}));
		CHECK(ps5archive::Unpacked(shared).empty() && ps5archive::Unpack(shared, nullptr, path, error));
		CHECK(ReadFile(path) == ReadFile(samples + "/Super Mario 3D Land.3ds") && ps5archive::Unpacked(shared) == path);

		// Settings > Diagnostics
		CHECK(ps5archive::UnpackedBytes() > 0);
		CHECK(ps5archive::ClearUnpacked() == (seven ? 7 : 5) && ps5archive::UnpackedBytes() == 0 && ps5archive::Unpacked(mario).empty());
	}
	ps5emu::archives::Stop();
	std::printf("ok\n");
	return 0;
}
