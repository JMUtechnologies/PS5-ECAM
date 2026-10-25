// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"
#include "nx_files.h"
#include "../app/gameinfo.h"
#include "../app/paths.h"
#include "../ps5/log.h"

#include <rapidjson/document.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace fs = std::filesystem;

namespace ps5eden
{
	namespace
	{
		std::atomic<bool> s_scanning{false};
		std::mutex s_mutex;
		std::vector<ps5emu::Game> s_games; // under s_mutex
		std::atomic<uint32_t> s_coverArrivals{0};
		std::mutex s_covers; // one pass over the games' icons at a time

		// a standalone ProsperoEden's data, whose game files Eden takes while ours has no keys
		constexpr const char* kStandaloneData = "/data/prosperoeden";

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = (char)std::tolower((unsigned char)c);
			return text;
		}

		bool Exists(const std::string& path)
		{
			std::error_code ec;
			return fs::exists(path, ec);
		}

		// Eden's key for a game's cover and name (its storage_paths.h, HashPath): FNV-1a of the file's
		// path in roms/
		uint32_t HashPath(const std::string& path)
		{
			uint32_t hash = 2166136261u;
			for (const unsigned char byte : path)
				hash = (hash ^ byte) * 16777619u;
			return hash;
		}

		// The game's path as Eden keys it: in its roms/ ("Game.nsp", "Game/Game.nsp"), else the file's name
		std::string CoverKey(const fs::path& path)
		{
			const std::string roms = GameFilesFolder() + "/roms/", full = path.string();
			return full.compare(0, roms.size(), roms) == 0 ? full.substr(roms.size()) : path.filename().string();
		}

		std::string CoverFile(const fs::path& path, const char* extension)
		{
			char name[24];
			std::snprintf(name, sizeof(name), "/%08x.%s", HashPath(CoverKey(path)), extension);
			return DataFolder() + "/covers" + name;
		}

		// The title ID in a dump's name ("[0100ABCD12345000]"), else 0
		uint64_t TaggedId(const std::string& name)
		{
			for (size_t open = name.find('['); open != std::string::npos; open = name.find('[', open + 1))
			{
				if (open + 17 >= name.size() || name[open + 17] != ']' || name.compare(open + 1, 2, "01") != 0)
					continue;
				if (std::all_of(name.begin() + open + 1, name.begin() + open + 17, [](char c) { return std::isxdigit((unsigned char)c) != 0; }))
					return std::strtoull(name.substr(open + 1, 16).c_str(), nullptr, 16);
			}
			return 0;
		}

		// An update or DLC rather than a game, by its name: its title ID (a game's ends in 000, its
		// update's in 800, a DLC's otherwise), or the tags dumps give them
		bool AddOn(const std::string& name)
		{
			if (const uint64_t id = TaggedId(name))
				return (id & 0xFFF) != 0;
			const std::string lower = Lower(name);
			for (const char* tag : {"[upd", "[dlc", "(upd", "(dlc", "[update", "(update"})
				if (lower.find(tag) != std::string::npos)
					return true;
			return false;
		}

		// A name from the file's: without its extension and the tags dumps carry ("Name [0100...][v0]",
		// "Name (USA)"), as Eden's library makes one
		std::string CleanTitle(const fs::path& path)
		{
			std::string title = path.stem().string();
			if (title.rfind("[Game] ", 0) == 0)
				title.erase(0, 7);
			if (const size_t tag = title.find_first_of("[("); tag != std::string::npos && tag > 0)
				title.erase(tag);
			while (!title.empty() && (title.back() == ' ' || title.back() == '_' || title.back() == '-'))
				title.pop_back();
			return title.empty() ? path.stem().string() : title;
		}

		// The game's own name, as Eden keeps it once it has read the game; empty when it has not
		std::string SavedTitle(const fs::path& path)
		{
			char text[513] = {};
			size_t size = 0;
			if (std::FILE* file = std::fopen(CoverFile(path, "name").c_str(), "rb"))
			{
				size = std::fread(text, 1, sizeof(text) - 1, file);
				std::fclose(file);
			}
			std::string title(text, size);
			// one line of readable text, as Eden shows it
			if (std::any_of(title.begin(), title.end(), [](char c) { return (unsigned char)c < 0x20; }))
				return {};
			while (!title.empty() && title.back() == ' ')
				title.pop_back();
			return title;
		}

		// What an earlier scan read of a file, so each is read once (until it changes): the lines of
		// cache/switch-games.txt, "<size> <time> <title ID> <kind> <path>"
		struct Known
		{
			uint64_t size = 0;
			int64_t time = 0;
			uint64_t titleId = 0;
			nx::Kind kind = nx::Kind::Unknown;
		};

		std::string KnownFile()
		{
			return ps5paths::Cache() + "/switch-games.txt";
		}

		std::map<std::string, Known> LoadKnown()
		{
			std::map<std::string, Known> known;
			std::ifstream in(KnownFile());
			for (std::string line; std::getline(in, line);)
			{
				Known file;
				unsigned long long size = 0, id = 0;
				long long time = 0;
				int kind = 0, read = 0;
				if (std::sscanf(line.c_str(), "%llu %lld %llx %d %n", &size, &time, &id, &kind, &read) == 4 && read > 0 &&
					(size_t)read < line.size() && kind > 0 && kind <= (int)nx::Kind::Dlc)
					known[line.substr(read)] = {size, time, id, (nx::Kind)kind};
			}
			return known;
		}

		void SaveKnown(const std::map<std::string, Known>& known)
		{
			std::error_code ec;
			fs::create_directories(ps5paths::Cache(), ec);
			const std::string staged = KnownFile() + ".new";
			{
				std::ofstream out(staged, std::ios::trunc);
				for (const auto& [path, file] : known)
					out << file.size << ' ' << file.time << ' ' << fmt::format("{:016x}", file.titleId) << ' ' << (int)file.kind << ' ' << path << '\n';
				if (!out)
					return;
			}
			std::rename(staged.c_str(), KnownFile().c_str());
		}

		bool Stamp(const fs::path& path, uint64_t& size, int64_t& time)
		{
			struct stat info{};
			if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
				return false;
			size = (uint64_t)info.st_size;
			time = (int64_t)info.st_mtim.tv_sec * 1000000000 + info.st_mtim.tv_nsec;
			return true;
		}

		// The game's own name where Eden keeps it (covers/<key>.name), unless it has one there already
		void KeepName(const fs::path& path, const std::string& name)
		{
			if (name.empty() || !SavedTitle(path).empty())
				return;
			std::error_code ec;
			fs::create_directories(DataFolder() + "/covers", ec);
			const std::string file = CoverFile(path, "name"), staged = file + ".new";
			{
				std::ofstream out(staged, std::ios::binary | std::ios::trunc);
				out << name;
				if (!out)
					return;
			}
			std::rename(staged.c_str(), file.c_str());
		}

		void Add(const fs::path& path, std::vector<ps5emu::Game>& games, uint64_t readId = 0)
		{
			ps5emu::Game game;
			const uint64_t tagged = TaggedId(path.filename().string());
			// the same game keeps one ID wherever it is listed: its settings, recent games and cover. Its
			// own, read from the file with the keys, else the one in its name, else one made from its path
			game.titleId = readId ? readId : tagged ? tagged : kIdTag | HashPath(CoverKey(path));
			const std::string saved = SavedTitle(path);
			game.name = !saved.empty() ? saved : CleanTitle(path);
			game.path = path;
			std::string extension = path.extension().string();
			for (char& c : extension)
				c = (char)std::toupper((unsigned char)c);
			game.format = extension.size() > 1 ? extension.substr(1) : extension;
			games.push_back(std::move(game));
		}

		// The Switch files in a folder: its .nsp and .xci files, and those in its folders (one level
		// down, as Eden's library looks: a folder per game, with its update and DLC beside it)
		void Collect(const std::string& folder, std::vector<fs::path>& files)
		{
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(folder, ec))
			{
				const std::string name = entry.path().filename().string();
				if (name.empty() || name[0] == '.')
					continue;
				if (entry.is_directory(ec))
				{
					std::error_code inner;
					for (const auto& file : fs::directory_iterator(entry.path(), inner))
						if (file.is_regular_file(inner) && IsSwitchFile(file.path().filename().string()))
							files.push_back(file.path());
				}
				else if (entry.is_regular_file(ec) && IsSwitchFile(name))
					files.push_back(entry.path());
			}
		}

		// The games in a folder, by their names alone (a game named like an update is taken for one)
		void Scan(const std::string& folder, std::vector<ps5emu::Game>& games)
		{
			std::vector<fs::path> files;
			Collect(folder, files);
			for (const fs::path& file : files)
				if (!AddOn(file.filename().string()))
					Add(file, games);
		}

		// The games in a folder as their files say with the keys: an update or DLC left out whatever its
		// name, a game listed whatever its name, its own title ID (the same game in an NSP and an XCI listed
		// once) and name. Without keys, or for a file they do not open, its name says. covers: the games
		// whose icons are to be read after (no cover yet).
		void ScanWithKeys(const std::string& folder, std::vector<ps5emu::Game>& games, std::vector<fs::path>& covers)
		{
			std::vector<fs::path> files;
			Collect(folder, files);
			nx::Keys keys;
			const bool keyed = nx::LoadKeys(GameFilesFolder() + "/keys", keys);
			std::map<std::string, Known> known = LoadKnown();
			size_t read = 0, failed = 0, addOns = 0;
			for (const fs::path& path : files)
			{
				Known file;
				const bool stamped = Stamp(path, file.size, file.time);
				if (const auto earlier = known.find(path.string());
					stamped && earlier != known.end() && earlier->second.size == file.size && earlier->second.time == file.time)
					file = earlier->second;
				else if (keyed && stamped)
				{
					nx::Info info;
					std::string why;
					if (nx::Read(path.string(), keys, false, info, why))
					{
						file.titleId = info.titleId;
						file.kind = info.kind;
						known[path.string()] = file;
						KeepName(path, info.name);
						read++;
					}
					else
						failed++;
					if (!why.empty())
						ps5log::Line("[switch] {}: {}", path.filename().string(), why);
				}
				const bool addOn = file.kind != nx::Kind::Unknown ? file.kind != nx::Kind::Game : AddOn(path.filename().string());
				if (addOn)
				{
					addOns++;
					continue;
				}
				Add(path, games, file.kind == nx::Kind::Game ? file.titleId : 0);
				if (keyed && CoverPath(games.back()).empty())
					covers.push_back(path);
			}
			if (read)
				SaveKnown(known);
			ps5log::Line("[switch] {} files: {} read with the keys now{}, {} updates and DLC left out{}", files.size(), read,
				failed ? fmt::format(", {} that could not be", failed) : std::string(), addOns, keyed ? "" : " (no keys: by their names)");
		}

		// The icons of the games that have no cover yet, from their files, as covers Eden keeps
		void ReadCovers(const std::vector<fs::path>& paths)
		{
			std::unique_lock lock(s_covers, std::try_to_lock);
			if (!lock.owns_lock() || paths.empty())
				return;
			nx::Keys keys;
			if (!nx::LoadKeys(GameFilesFolder() + "/keys", keys))
				return;
			std::error_code ec;
			fs::create_directories(DataFolder() + "/covers", ec);
			size_t made = 0;
			for (const fs::path& path : paths)
			{
				nx::Info info;
				std::string why;
				const std::string cover = CoverFile(path, "tga");
				if (Exists(cover) || !nx::Read(path.string(), keys, true, info, why) || info.icon.empty() || !nx::WriteIconTga(info.icon, cover))
					continue;
				KeepName(path, info.name);
				made++;
				s_coverArrivals++;
			}
			ps5log::Line("[switch] {} covers read from {} games' files", made, paths.size());
		}
	}

	std::string DataFolder()
	{
		return ps5paths::Eden();
	}

	std::string DefaultGamesFolder()
	{
		return DataFolder() + "/roms";
	}

	std::string GameFilesFolder()
	{
		// prosperoeden.json's game_files, which its Settings > Game files saves (one for every profile)
		if (std::FILE* file = std::fopen((DataFolder() + "/config/prosperoeden.json").c_str(), "rb"))
		{
			std::string text;
			char buffer[4096];
			for (size_t got; (got = std::fread(buffer, 1, sizeof(buffer), file)) > 0;)
				text.append(buffer, got);
			std::fclose(file);
			rapidjson::Document json;
			if (!json.Parse(text.c_str()).HasParseError() && json.IsObject() && json.HasMember("game_files") && json["game_files"].IsString())
			{
				const std::string saved = json["game_files"].GetString();
				if (saved.size() > 1 && saved[0] == '/' && saved.back() != '/')
					return saved == std::string(ps5paths::kDefaultRoot) + "/eden" ? DataFolder() : saved;
			}
		}
		const std::string keys = "/keys/prod.keys";
		if (!Exists(DataFolder() + keys) && Exists(kStandaloneData + keys))
			return kStandaloneData;
		return DataFolder();
	}

	std::string ScanFolder(const std::string& folder)
	{
		return folder == DefaultGamesFolder() || folder.empty() ? GameFilesFolder() + "/roms" : folder;
	}

	bool KeysFound()
	{
		return Exists(GameFilesFolder() + "/keys/prod.keys");
	}

	bool FirmwareFound()
	{
		std::error_code ec;
		const fs::path firmware = GameFilesFolder() + "/firmware";
		return fs::is_directory(firmware, ec) && !fs::is_empty(firmware, ec) && !ec;
	}

	bool Available()
	{
		return Exists(ps5paths::AppDir() + "/eden/eboot.bin");
	}

	bool IsSwitchFile(const std::string& name)
	{
		const std::string lower = Lower(name);
		return lower.size() > 4 && (lower.ends_with(".nsp") || lower.ends_with(".xci"));
	}

	void StartScan(const std::string& folder)
	{
		if (s_scanning.exchange(true))
			return;
		// Eden's folders from the first start, so there is somewhere to put keys, firmware and games
		std::error_code ec;
		for (const char* sub : {"keys", "firmware", "roms"})
			fs::create_directories(fs::path(DataFolder()) / sub, ec);
		const std::string scanned = ScanFolder(folder);
		std::thread([scanned] {
			const auto started = std::chrono::steady_clock::now();
			std::vector<ps5emu::Game> games;
			std::vector<fs::path> covers;
			ScanWithKeys(scanned, games, covers);
			// the same game twice (an NSP and an XCI, a copy in a folder): listed once
			std::set<uint64_t> listed;
			std::erase_if(games, [&](const ps5emu::Game& game) { return !listed.insert(game.titleId).second; });
			std::sort(games.begin(), games.end(), [](const ps5emu::Game& a, const ps5emu::Game& b) { return Lower(a.name) < Lower(b.name); });
			// GameTDB's ID for each, for its box art and About page: the game it has by this one's name,
			// else by its file's
			size_t found = 0;
			for (ps5emu::Game& game : games)
			{
				game.gameId = ps5gameinfo::FindByTitle(ps5boxart::System::Switch, game.name);
				if (game.gameId.empty())
					game.gameId = ps5gameinfo::FindByTitle(ps5boxart::System::Switch, CleanTitle(game.path));
				found += !game.gameId.empty();
			}
			ps5log::Line("[switch] {} Switch games in {}, {} of them GameTDB's ({} ms)", games.size(), scanned, found, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
			{
				std::lock_guard lock(s_mutex);
				s_games = std::move(games);
			}
			s_scanning = false;
			// then their pictures, which the launcher shows as each arrives (CoverArrivals)
			ReadCovers(covers);
		}).detach();
	}

	bool Scanning()
	{
		return s_scanning;
	}

	std::vector<ps5emu::Game> ListGames()
	{
		std::lock_guard lock(s_mutex);
		return s_games;
	}

	int CountGames(const std::string& folder)
	{
		std::error_code ec;
		const std::string scanned = ScanFolder(folder);
		if (!fs::is_directory(scanned, ec))
			return -1;
		std::vector<ps5emu::Game> games;
		Scan(scanned, games);
		return (int)games.size();
	}

	std::string CoverPath(const ps5emu::Game& game)
	{
		const std::string cover = CoverFile(game.path, "tga");
		return Exists(cover) ? cover : std::string();
	}

	uint32_t CoverArrivals()
	{
		return s_coverArrivals;
	}

	std::string BoxArt(const ps5emu::Game& game)
	{
		const fs::path& path = game.path;
		for (const char* extension : {".png", ".jpg", ".jpeg"})
		{
			fs::path beside = path;
			beside.replace_extension(extension);
			if (Exists(beside.string()))
				return beside.string();
		}
		return {};
	}
}
