// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Wii U games in archives (wiiu_archives.h). Read with pugixml, as Cemu reads a title's
// XML files, so the scan needs nothing of Cemu's but the folders it looks in.

#include "wiiu_archives.h"
#include "game_archives.h"
#include "lang.h"
#include "paths.h"
#include "../ps5/log.h"

#include <pugixml.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace fs = std::filesystem;

namespace ps5emu
{
	std::string BoxId(const std::string& productCode, const std::string& companyCode)
	{
		const size_t dash = productCode.rfind('-');
		const std::string product = dash == std::string::npos ? productCode : productCode.substr(dash + 1);
		if (product.size() != 4 || companyCode.size() < 2)
			return {};
		std::string id = product + companyCode.substr(companyCode.size() - 2);
		for (char& c : id)
		{
			c = (char)std::toupper((unsigned char)c);
			if (!std::isalnum((unsigned char)c))
				return {};
		}
		return id;
	}

	namespace archives
	{
		namespace
		{
			constexpr int kScanDepth = 4; // folders in folders, as the 3DS and DS sides look
			// the most read of a title's files: app.xml and meta.xml, its icon (128 x 128) and its boot
			// screen (1280 x 720)
			constexpr uint64_t kXmlLimit = 1 << 20, kIconLimit = 1 << 20, kBootLimit = 8 << 20;

			std::mutex s_mutex;
			std::vector<Game> s_games;			// under s_mutex
			std::vector<std::string> s_folders; // the next scan's, under s_mutex
			bool s_again = false;				// under s_mutex: StartScan asked again while a scan ran
			std::thread s_thread;				// under s_mutex
			std::atomic<bool> s_scanning{false}, s_stop{false};

			std::string Lower(std::string text)
			{
				for (char& c : text)
					c = (char)std::tolower((unsigned char)c);
				return text;
			}

			// What archives.bin keeps of an archive: whether it holds a Wii U game, the game's folder in it
			// and all its files' size, its app.xml and meta.xml as they are (its name in the menus' language
			// is read from them at each scan), and whether it has an icon (written in covers/ when read)
			enum ArchiveField
			{
				kHasGame, // "1", or "0": the archive holds no Wii U game
				kMember,
				kSize,
				kAppXml,
				kMetaXml,
				kHasIcon, // "1" or "0"
				kArchiveFields,
			};

			struct AppXml
			{
				uint64_t titleId = 0;
				uint16_t version = 0;
			};

			// code/app.xml's title ID and version, in hex, as Cemu reads them (TitleInfo::ParseAppXml)
			bool ParseApp(const std::string& xml, AppXml& app)
			{
				pugi::xml_document document;
				if (!document.load_buffer(xml.data(), xml.size()))
					return false;
				const pugi::xml_node root = document.child("app");
				app.titleId = std::strtoull(root.child("title_id").text().as_string(), nullptr, 16);
				app.version = (uint16_t)std::strtoul(root.child("title_version").text().as_string(), nullptr, 16);
				return app.titleId != 0;
			}

			// A game's or a demo's title, not an update's or DLC's
			bool IsBase(uint64_t titleId)
			{
				const uint32_t high = (uint32_t)(titleId >> 32);
				return high == 0x00050000 || high == 0x00050002;
			}

			// meta.xml's name in the menus' language, else its English one (as Cemu's GetLongName), and the
			// ID on the box
			void ReadMeta(const std::string& xml, Game& game)
			{
				pugi::xml_document document;
				if (!document.load_buffer(xml.data(), xml.size()))
					return;
				const pugi::xml_node menu = document.child("menu");
				// its languages in the Wii U's order, which NintendoLanguage numbers
				static constexpr const char* kLanguages[] = {"ja", "en", "fr", "de", "it", "es", "zhs", "ko", "nl", "pt", "ru", "zht"};
				const int language = ps5lang::NintendoLanguage();
				for (const char* code : {language >= 0 && language < 12 ? kLanguages[language] : "en", "en"})
				{
					std::string name = menu.child((std::string("longname_") + code).c_str()).text().as_string();
					std::replace_if(name.begin(), name.end(), [](char c) { return c == '\r' || c == '\n'; }, ' ');
					if (!name.empty())
					{
						game.name = name;
						break;
					}
				}
				game.gameId = BoxId(menu.child("product_code").text().as_string(), menu.child("company_code").text().as_string());
			}

			// An entry's contents, all of it, when it is no larger than limit
			std::string ReadAll(ps5archive::Stream& stream, uint64_t size, uint64_t limit)
			{
				if (size == 0 || size > limit)
					return {};
				std::string data(size, '\0');
				return stream.ReadAt(0, data.data(), data.size()) ? data : std::string();
			}

			// What a title's folder in an archive has of what Cemu needs: code with app.xml, cos.xml and an
			// .rpx, meta with meta.xml (content is looked for among the archive's files)
			struct Folder
			{
				std::string app, meta, icon, boot;
				bool cos = false, rpx = false;
			};

			// What an archive's Wii U game is: its first base title by its folder's name (case aside), with
			// its icon and boot screen written in covers/. Empty when the archive cannot be read
			std::vector<std::string> ProbeArchive(const std::string& path)
			{
				const auto started = std::chrono::steady_clock::now();
				std::map<std::string, Folder> folders; // by the folder's path in the archive with its '/' ("" at the top)
				std::vector<ps5archive::Entry> files;
				std::string error;
				const bool walked = ps5archive::Visit(path, [&](const ps5archive::Entry& entry, ps5archive::Stream& stream) {
					if (entry.name.empty())
						return !s_stop; // a name libarchive dropped: no title's file can be told by it
					files.push_back(entry);
					const std::string lower = Lower(entry.name);
					// the title folder of one of its files: "Game/" for "Game/code/app.xml", "" at the top
					auto in = [&](const std::string& file) -> std::optional<std::string> {
						if (lower.size() < file.size() || lower.compare(lower.size() - file.size(), file.size(), file) != 0)
							return std::nullopt;
						const size_t folder = lower.size() - file.size();
						if (folder > 0 && lower[folder - 1] != '/')
							return std::nullopt;
						return entry.name.substr(0, folder);
					};
					const size_t slash = lower.rfind('/');
					const std::string parent = slash == std::string::npos ? std::string() : lower.substr(0, slash);
					if (const auto folder = in("code/app.xml"))
						folders[*folder].app = ReadAll(stream, entry.size, kXmlLimit);
					else if (const auto folder = in("code/cos.xml"))
						folders[*folder].cos = true;
					else if (const auto folder = in("meta/meta.xml"))
						folders[*folder].meta = ReadAll(stream, entry.size, kXmlLimit);
					else if (const auto folder = in("meta/icontex.tga"))
						folders[*folder].icon = ReadAll(stream, entry.size, kIconLimit);
					else if (const auto folder = in("meta/boottvtex.tga"))
						folders[*folder].boot = ReadAll(stream, entry.size, kBootLimit);
					else if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".rpx") == 0 &&
						(parent == "code" || (parent.size() > 5 && parent.compare(parent.size() - 5, 5, "/code") == 0)))
						folders[entry.name.substr(0, parent.size() - 4)].rpx = true;
					return !s_stop;
				}, &error);
				if (s_stop)
					return {};
				if (!walked)
				{
					ps5log::Line("[emu] {} could not be read as an archive: {}", path, error);
					return {};
				}
				// the base titles whose folder has what Cemu needs, the first by its folder's name
				std::vector<std::pair<std::string, AppXml>> titles;
				for (const auto& [folder, has] : folders)
				{
					AppXml app;
					const std::string content = Lower(folder) + "content/";
					const bool hasContent = std::any_of(files.begin(), files.end(),
						[&](const ps5archive::Entry& file) { return Lower(file.name).compare(0, content.size(), content) == 0; });
					if (has.cos && has.rpx && !has.meta.empty() && hasContent && ParseApp(has.app, app) && IsBase(app.titleId))
						titles.emplace_back(folder, app);
				}
				std::stable_sort(titles.begin(), titles.end(), [](const auto& a, const auto& b) { return Lower(a.first) < Lower(b.first); });
				std::vector<std::string> fields(kArchiveFields);
				fields[kHasGame] = titles.empty() ? "0" : "1";
				if (!titles.empty())
				{
					const auto& [member, app] = titles.front();
					const Folder& folder = folders[member];
					uint64_t size = 0;
					for (const ps5archive::Entry& file : files)
						if (file.name.size() > member.size() && file.name.compare(0, member.size(), member) == 0)
							size += file.size;
					fields[kMember] = member;
					fields[kSize] = std::to_string(size);
					fields[kAppXml] = folder.app;
					fields[kMetaXml] = folder.meta;
					fields[kHasIcon] = !folder.icon.empty() && WriteCover(app.titleId, false, std::vector<uint8_t>(folder.icon.begin(), folder.icon.end())) ? "1" : "0";
					if (!folder.boot.empty())
						WriteCover(app.titleId, true, std::vector<uint8_t>(folder.boot.begin(), folder.boot.end()));
				}
				ps5log::Line("[emu] {}: {} ({} ms)", path,
					titles.empty() ? std::string("no Wii U game in it") :
									 fmt::format("{:016x} in \"{}\"", titles.front().second.titleId, titles.front().first),
					(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
				return fields;
			}

			// An archive's game for the library: from what an earlier scan read of it, when the archive has
			// not changed since (and its icon is still where it was written), else from the archive. False
			// when it holds no Wii U game
			bool ReadArchiveGame(const std::string& path, ps5archive::Probes& probes, Game& game)
			{
				uint64_t size = 0;
				int64_t time = 0;
				if (!ps5archive::Probes::Stamp(path, size, time))
					return false;
				std::vector<std::string> fields;
				AppXml app;
				std::error_code ec;
				if (const std::vector<std::string>* known = probes.Find(path, size, time); known && known->size() == kArchiveFields)
					fields = *known;
				if (!fields.empty() && fields[kHasIcon] == "1" && (!ParseApp(fields[kAppXml], app) || !fs::exists(CoverFile(app.titleId, false), ec)))
					fields.clear();
				if (fields.empty())
					fields = ProbeArchive(path);
				if (fields.size() != kArchiveFields)
					return false; // unreadable: read again at the next scan
				probes.Keep(path, size, time, fields);
				if (fields[kHasGame] != "1" || !ParseApp(fields[kAppXml], app))
					return false;
				game.titleId = app.titleId;
				game.version = app.version;
				game.path = path;
				game.format = ps5archive::Kind(path);
				ReadMeta(fields[kMetaXml], game);
				if (game.name.empty())
					game.name = ps5archive::Stem(path);
				game.archiveFolder = true;
				game.archiveMember = fields[kMember];
				game.archiveSize = std::strtoull(fields[kSize].c_str(), nullptr, 10);
				return true;
			}

			void Scan(const fs::path& folder, int depth, std::vector<Game>& games, ps5archive::Probes& probes)
			{
				std::error_code ec;
				for (const auto& entry : fs::directory_iterator(folder, ec))
				{
					if (s_stop)
						return;
					if (entry.is_directory(ec))
					{
						// (the copies unpacked from archives are no games of their own)
						if (depth < kScanDepth && entry.path() != ps5archive::UnpackedRoot())
							Scan(entry.path(), depth + 1, games, probes);
						continue;
					}
					const std::string path = entry.path().string();
					Game game;
					if (entry.is_regular_file(ec) && ps5archive::IsArchive(path) && ReadArchiveGame(path, probes, game))
						games.push_back(std::move(game));
				}
			}

			void Run()
			{
				for (;;)
				{
					std::vector<std::string> folders;
					{
						std::lock_guard lock(s_mutex);
						folders = s_folders;
						s_again = false;
					}
					const auto started = std::chrono::steady_clock::now();
					std::vector<Game> games;
					ps5archive::Probes probes(ps5paths::Root() + "/archives.bin");
					for (const std::string& folder : folders)
						Scan(folder, 0, games, probes);
					if (!s_stop)
						probes.Save();
					std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return Lower(a.name) < Lower(b.name); });
					// the same game twice (a copy in another archive): listed once
					std::set<uint64_t> listed;
					std::erase_if(games, [&listed](const Game& game) { return !listed.insert(game.titleId).second; });
					ps5log::Line("[emu] {} Wii U games in archives{} ({} ms)", games.size(), s_stop ? ", the scan stopped" : "",
						(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
					std::lock_guard lock(s_mutex);
					s_games = std::move(games);
					if (!s_again || s_stop)
					{
						s_scanning = false;
						return;
					}
				}
			}
		}

		void StartScan(const std::vector<std::string>& folders)
		{
			std::lock_guard lock(s_mutex);
			s_folders = folders;
			if (s_scanning)
			{
				s_again = true;
				return;
			}
			if (s_thread.joinable())
				s_thread.join(); // the last scan's, done
			s_stop = false;
			s_scanning = true;
			s_thread = std::thread(Run);
		}

		bool Scanning()
		{
			return s_scanning;
		}

		void Stop()
		{
			s_stop = true;
			std::thread thread;
			{
				std::lock_guard lock(s_mutex);
				thread = std::move(s_thread);
			}
			if (thread.joinable())
				thread.join();
		}

		std::vector<Game> Games()
		{
			std::lock_guard lock(s_mutex);
			return s_games;
		}
	}
}
