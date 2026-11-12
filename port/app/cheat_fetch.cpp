// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: cheats downloaded for the games in the library (cheats.h says from where, and into which file),
// one game at a time on a thread of its own, as the box art is (boxart.cpp).
#include "cheats.h"
#include "boxart.h"
#include "paths.h"
#include "../frontend/launcher.h"
#include "../ps5/curl_socket.h"
#include "../ps5/log.h"
#include "../ps5/net.h"
#include "../retro/porpoise_game.h"
#include "../retro/porpoise_settings.h"

#include <curl/curl.h>
#include <rapidjson/document.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace ps5cheats
{
	namespace
	{
		using ps5launcher::System;

		constexpr size_t kMaxDownload = 8 * 1024 * 1024;
		constexpr int64_t kAgainAfter = 14 * 24 * 3600; // a game with none, an index: asked again after two weeks
		constexpr const char* kApi = "api.github.com";
		constexpr const char* kRaw = "raw.githubusercontent.com";
		constexpr const char* kGecko = "codes.rc24.xyz";

		enum Result
		{
			kFailed = -2,
			kNotLooked = -1,
			kNone = 0,
			kFound = 1,
		};

		std::mutex s_mutex;
		std::deque<std::pair<Game, bool>> s_queue; // under s_mutex: the game, again
		std::set<std::string> s_queued;			  // under s_mutex
		std::map<std::string, int> s_looked;	  // under s_mutex: this session's results, by Key
		bool s_running = false;					  // under s_mutex
		std::atomic<bool> s_stopped{false}, s_busy{false};
		std::atomic<uint32_t> s_arrivals{0};

		int64_t Now() { return (int64_t)std::time(nullptr); }

		std::string ReadText(const std::string& path)
		{
			std::ifstream in(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), {});
		}

		bool Exists(const std::string& path)
		{
			std::error_code ec;
			return !path.empty() && fs::exists(path, ec);
		}

		bool Write(const std::string& path, const std::string& text)
		{
			std::error_code ec;
			fs::create_directories(fs::path(path).parent_path(), ec);
			{
				std::ofstream out(path + ".new", std::ios::binary | std::ios::trunc);
				out << text;
				if (!out.good())
					return false;
			}
			fs::rename(path + ".new", path, ec);
			return !ec;
		}

		std::string Hex16(uint64_t id)
		{
			char text[17];
			std::snprintf(text, sizeof(text), "%016llX", (unsigned long long)id);
			return text;
		}

		// What a source knows the game by: its file's name (libretro-database's), its title ID, its disc's ID
		std::string NameOf(const Game& game)
		{
			switch (KindOf(game.side))
			{
			case Kind::Retro:
			case Kind::Nds: return fs::path(game.file).stem().string();
			case Kind::N3ds:
			case Kind::Switch: return game.titleId ? Hex16(game.titleId) : std::string();
			case Kind::Porpoise: return ps5porpoise::GameKey(game.file);
			case Kind::None: break;
			}
			return {};
		}

		std::string Key(const Game& game) { return std::to_string(game.side) + ":" + NameOf(game); }

		// The side's cheats folder, where what was looked for is noted (.looked.txt) and libretro-database's
		// lists are kept (.index-<its folder>.txt)
		std::string Folder(const Game& game)
		{
			const std::string file = FileFor(game);
			if (file.empty())
				return {};
			return KindOf(game.side) == Kind::Switch ? fs::path(file).parent_path().string() :
				KindOf(game.side) == Kind::Nds		 ? ps5paths::MelonDs() + "/cheats" :
													   fs::path(file).parent_path().string();
		}

		// When the game was last found to have none ("<name>\t<seconds since 1970>" lines), 0 for never
		int64_t NoneSince(const Game& game)
		{
			std::ifstream in(Folder(game) + "/.looked.txt");
			const std::string name = NameOf(game);
			int64_t when = 0;
			for (std::string line; std::getline(in, line);)
				if (const size_t tab = line.rfind('\t'); tab != std::string::npos && line.compare(0, tab, name) == 0 && tab == name.size())
					when = std::atoll(line.c_str() + tab + 1);
			return when;
		}

		void NoteNone(const Game& game)
		{
			std::error_code ec;
			fs::create_directories(Folder(game), ec);
			std::ofstream(Folder(game) + "/.looked.txt", std::ios::app) << NameOf(game) << '\t' << Now() << '\n';
		}

		size_t Receive(char* data, size_t size, size_t count, void* user)
		{
			auto* out = static_cast<std::string*>(user);
			const size_t bytes = size * count;
			if (out->size() + bytes > kMaxDownload)
				return 0;
			out->append(data, bytes);
			return bytes;
		}

		// The HTTP status, or -1 when there was no answer (no network)
		long Get(CURL* curl, const std::string& url, std::string& out)
		{
			out.clear();
			char error[CURL_ERROR_SIZE] = {};
			static const std::string ca = ps5paths::Assets() + "/cacert.pem"; // the console has no store
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &Receive);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "PS5-ECAM/" PS5CEMU_VERSION);
			const CURLcode result = curl_easy_perform(curl);
			if (result != CURLE_OK)
			{
				ps5log::Line("[cheats] {}: curl error {} ({})", url, (int)result, error[0] ? error : curl_easy_strerror(result));
				return -1;
			}
			long status = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
			return status;
		}

		std::string Escape(CURL* curl, const std::string& text)
		{
			char* escaped = curl_easy_escape(curl, text.c_str(), (int)text.size());
			std::string out = escaped ? escaped : "";
			curl_free(escaped);
			return out;
		}

		// -- libretro-database --------------------------------------------------------------------------

		// The cheats' folders in libretro-database's cht/ for the side, its own first
		std::vector<std::string> ChtFolders(int side)
		{
			if ((System)side == System::Nds)
				return {"Nintendo - Nintendo DS"};
			if (ps5retro::Is(side, "ique"))
				return {"Nintendo - Nintendo 64 (iQue)", "Nintendo - Nintendo 64"};
			const std::string own = ps5retro::Thumbnails(side);
			if (own == "Nintendo - Game Boy Color")
				return {own, "Nintendo - Game Boy"};
			if (own == "Nintendo - Game Boy")
				return {own, "Nintendo - Game Boy Color"};
			return {own};
		}

		// The names in one of its folders (GitHub's listing of it), kept beside the side's cheats for two weeks.
		// Read on the worker's thread only
		std::map<std::string, std::vector<std::string>> s_index;
		const std::vector<std::string>& Index(CURL* curl, const std::string& folder, const std::string& cheats, bool& failed)
		{
			auto [entry, added] = s_index.try_emplace(folder);
			std::vector<std::string>& names = entry->second;
			if (!added && !names.empty())
				return names;
			const std::string file = cheats + "/.index-" + folder + ".txt";
			std::error_code ec;
			if (const auto when = fs::last_write_time(file, ec); !ec &&
				std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - when).count() < kAgainAfter)
			{
				std::ifstream in(file);
				for (std::string line; std::getline(in, line);)
					if (!line.empty())
						names.push_back(line);
				if (!names.empty())
					return names;
			}
			std::string page;
			const long status = Get(curl, fmt::format("https://{}/repos/libretro/libretro-database/git/trees/master:cht/{}", kApi, Escape(curl, folder)), page);
			rapidjson::Document json;
			if (status != 200 || json.Parse(page.data(), page.size()).HasParseError() || !json.IsObject() || !json.HasMember("tree") || !json["tree"].IsArray())
			{
				ps5log::Line("[cheats] {}: libretro-database's list did not come (HTTP {})", folder, status);
				failed = true;
				return names;
			}
			for (const auto& item : json["tree"].GetArray())
				if (item.IsObject() && item.HasMember("path") && item["path"].IsString())
				{
					const std::string path = item["path"].GetString();
					if (path.size() > 4 && path.ends_with(".cht"))
						names.push_back(path.substr(0, path.size() - 4));
				}
			fs::create_directories(cheats, ec);
			std::ofstream out(file, std::ios::trunc);
			for (const std::string& name : names)
				out << name << '\n';
			ps5log::Line("[cheats] {}: {} games in libretro-database", folder, names.size());
			return names;
		}

		// The cheats already in a file, by name, so a download adds only the new ones (a player's own and their
		// choices are kept)
		std::set<std::string> Names(const std::vector<Cheat>& cheats)
		{
			std::set<std::string> names;
			for (const Cheat& cheat : cheats)
				names.insert(cheat.name);
			return names;
		}

		Result FromLibretro(CURL* curl, const Game& game)
		{
			const std::string stem = NameOf(game), cheats = Folder(game), dest = FileFor(game);
			bool failed = false;
			for (const std::string& folder : ChtFolders(game.side))
			{
				const std::vector<std::string>& names = Index(curl, folder, cheats, failed);
				const std::string name = std::find(names.begin(), names.end(), stem) != names.end() ? stem : ps5boxart::BestName(names, stem);
				if (name.empty())
					continue;
				std::string text;
				const long status = Get(curl, fmt::format("https://{}/libretro/libretro-database/master/cht/{}/{}.cht", kRaw, Escape(curl, folder), Escape(curl, name)), text);
				if (status != 200)
				{
					ps5log::Line("[cheats] {} / {}: HTTP {}", folder, name, status);
					return status == 404 ? kNone : kFailed;
				}
				std::vector<Cheat> fetched = ParseCht(text);
				for (Cheat& cheat : fetched)
					cheat.on = false; // off until the player turns it on
				const bool nds = KindOf(game.side) == Kind::Nds;
				const std::vector<Cheat> here = List(game);
				const std::set<std::string> have = Names(here);
				std::vector<Cheat> added;
				for (Cheat& cheat : fetched)
					if (cheat.code.empty() || !have.count(cheat.name))
						added.push_back(std::move(cheat));
				size_t codes = std::count_if(added.begin(), added.end(), [](const Cheat& c) { return !c.code.empty(); });
				if (!codes)
					return here.empty() ? kNone : kFound;
				bool written;
				if (nds)
				{
					// a DS .mch: the new ones after what is there
					const std::string before = Exists(dest) ? ReadText(dest) : std::string();
					written = Write(dest, before + (before.empty() || before.back() == '\n' ? "" : "\n") + ChtToMch(FormatCht(added)));
				}
				else
				{
					std::vector<Cheat> all = ParseCht(Exists(dest) ? ReadText(dest) : std::string());
					all.insert(all.end(), added.begin(), added.end());
					written = Write(dest, FormatCht(all));
				}
				if (!written)
					return kFailed;
				ps5log::Line("[cheats] {}: {} cheats from libretro-database's {}", stem, codes, name);
				return kFound;
			}
			return failed ? kFailed : kNone;
		}

		// -- Sharkive (3DS) -------------------------------------------------------------------------------

		Result FromSharkive(CURL* curl, const Game& game)
		{
			const std::string id = NameOf(game), dest = FileFor(game);
			std::string text;
			const long status = Get(curl, fmt::format("https://{}/FlagBrew/Sharkive/master/3ds/{}.txt", kRaw, id), text);
			if (status == 404)
				return kNone;
			if (status != 200)
				return kFailed;
			// each [name] and its lines, the ones not here yet, off (Azahar's *citra_enabled left out)
			const std::set<std::string> have = Names(List(game));
			std::string out, block;
			bool keep = false;
			size_t added = 0;
			std::istringstream in(text);
			for (std::string line; std::getline(in, line);)
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.size() >= 2 && line.front() == '[')
				{
					if (keep)
						out += block;
					block.clear();
					keep = !have.count(line.substr(1, line.size() - 2));
					added += keep;
				}
				if (line != "*citra_enabled")
					block += line + "\n";
			}
			if (keep)
				out += block;
			if (!added)
				return have.empty() ? kNone : kFound;
			const std::string before = Exists(dest) ? ReadText(dest) : std::string();
			if (!Write(dest, before + (before.empty() || before.back() == '\n' ? "" : "\n") + out))
				return kFailed;
			ps5log::Line("[cheats] 3DS {}: {} cheats from Sharkive", id, added);
			return kFound;
		}

		// -- switch-cheats-db (Switch) -----------------------------------------------------------------------

		Result FromSwitchDb(CURL* curl, const Game& game)
		{
			const std::string id = NameOf(game), folder = FileFor(game);
			std::string text;
			const long status = Get(curl, fmt::format("https://{}/HamletDuFromage/switch-cheats-db/master/cheats/{}.json", kRaw, id), text);
			if (status == 404)
				return kNone;
			rapidjson::Document json;
			if (status != 200 || json.Parse(text.data(), text.size()).HasParseError() || !json.IsObject())
				return kFailed;
			// each version of the game (its build ID) a file, as Atmosphère's; one already here is left as it is
			size_t builds = 0;
			for (const auto& member : json.GetObject())
			{
				const std::string build = member.name.GetString();
				if (build.size() != 16 || !std::all_of(build.begin(), build.end(), [](char c) { return std::isxdigit((unsigned char)c); }) ||
					!member.value.IsObject())
					continue; // its "attribution"
				const std::string file = folder + "/" + build + ".txt";
				builds++;
				if (Exists(file))
					continue;
				std::string cheats;
				for (const auto& cheat : member.value.GetObject())
					if (cheat.value.IsString())
						cheats += cheat.value.GetString();
				if (!cheats.empty() && !Write(file, cheats))
					return kFailed;
			}
			if (!builds)
				return kNone;
			ps5log::Line("[cheats] Switch {}: cheats for {} versions from switch-cheats-db", id, builds);
			return kFound;
		}

		// -- codes.rc24.xyz (GameCube, Wii) ------------------------------------------------------------------

		constexpr const char* kGeckoMark = ": Gecko codes downloaded from codes.rc24.xyz by Playtendoh";

		Result FromGeckoCodes(CURL* curl, const Game& game)
		{
			const std::string id = NameOf(game), dest = FileFor(game);
			if (id.size() != 6)
				return kNone;
			// a code file of the player's own (any of Porpoise's names for the game) is theirs: nothing added
			const std::string own = ps5porpoise::Data() + "/cheats";
			for (const size_t length : {size_t(6), size_t(4), size_t(3)})
				for (const char* extension : {".ini", ".txt"})
				{
					const std::string path = own + "/" + id.substr(0, length) + extension;
					if (Exists(path) && (path != dest || ReadText(path).find(kGeckoMark) == std::string::npos))
						return kFound;
				}
			std::string text;
			const long status = Get(curl, fmt::format("https://{}/txt.php?txt={}", kGecko, id), text);
			if (status == 404)
				return kNone;
			if (status != 200)
				return kFailed;
			const std::string codes = GeckoTxt(text, id);
			if (codes.empty())
				return kNone;
			// Porpoise turns on every code in its cheats folder unless the game's own file says it is off: each new
			// one is written off there first (dolphin.Gecko_Disabled.$<name>, as the game's Cheats page writes it)
			std::set<std::string> before;
			for (const ps5porpoise::Cheat& cheat : ps5porpoise::CheatsFor("", own, id))
				if (cheat.own)
					before.insert(ps5porpoise::CheatKey(cheat, false));
			const std::string previous = Exists(dest) ? ReadText(dest) : std::string();
			if (!Write(dest, codes))
				return kFailed;
			std::map<std::string, std::string> off;
			for (const ps5porpoise::Cheat& cheat : ps5porpoise::CheatsFor("", own, id))
				if (cheat.own && !before.count(ps5porpoise::CheatKey(cheat, false)))
					off[ps5porpoise::CheatKey(cheat, false)] = "1";
			std::string error;
			if (!off.empty() && !ps5porpoise::Write(ps5porpoise::GameFile(game.file), off, error))
			{
				// never left on: the download taken back
				ps5log::Line("[cheats] {}: Porpoise's game file could not be written ({}); the codes taken out", id, error);
				std::error_code ec;
				if (previous.empty())
					fs::remove(dest, ec);
				else
					Write(dest, previous);
				return kFailed;
			}
			ps5log::Line("[cheats] {}: {} new Gecko codes from codes.rc24.xyz", id, off.size());
			return kFound;
		}

		Result FetchOne(CURL* curl, const Game& game)
		{
			switch (KindOf(game.side))
			{
			case Kind::Retro:
			case Kind::Nds: return FromLibretro(curl, game);
			case Kind::N3ds: return FromSharkive(curl, game);
			case Kind::Switch: return FromSwitchDb(curl, game);
			case Kind::Porpoise: return FromGeckoCodes(curl, game);
			case Kind::None: break;
			}
			return kNone;
		}

		void Worker()
		{
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* resolved = nullptr;
			for (const char* host : {kApi, kRaw, kGecko})
				resolved = ps5net::AddResolve(resolved, host, 443);
			static curl_slist* const headers = ps5curl::Headers();
			CURL* const curl = curl_easy_init();
			if (curl)
			{
				ps5curl::Setup(curl);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
				if (resolved)
					curl_easy_setopt(curl, CURLOPT_RESOLVE, resolved);
			}
			int found = 0, none = 0, failed = 0;
			for (;;)
			{
				std::pair<Game, bool> item;
				{
					std::lock_guard lock(s_mutex);
					if (s_queue.empty() || s_stopped || !curl)
					{
						s_queue.clear();
						s_queued.clear();
						s_running = false;
						break;
					}
					item = std::move(s_queue.front());
					s_queue.pop_front();
					s_busy = true;
				}
				const Result result = FetchOne(curl, item.first);
				if (result == kNone)
					NoteNone(item.first);
				found += result == kFound;
				none += result == kNone;
				failed += result == kFailed;
				{
					std::lock_guard lock(s_mutex);
					s_looked[Key(item.first)] = result;
					s_queued.erase(Key(item.first));
					s_busy = false;
				}
				s_arrivals++;
			}
			ps5log::Line("[cheats] {} games with cheats, {} with none, {} not reached", found, none, failed);
			if (curl)
				curl_easy_cleanup(curl);
			curl_slist_free_all(resolved);
		}
	}

	void Fetch(const std::vector<Game>& games, bool again)
	{
		if (s_stopped)
			return;
		std::lock_guard lock(s_mutex);
		size_t queued = 0;
		for (const Game& game : games)
		{
			const Kind kind = KindOf(game.side);
			if (kind == Kind::None || NameOf(game).empty() || FileFor(game).empty() || s_queued.count(Key(game)))
				continue;
			if (!again)
			{
				// here already (a file the player put there too), or found with none lately
				const std::string file = FileFor(game);
				if ((kind == Kind::Switch ? Count(game) > 0 : Exists(file)) || Now() - NoneSince(game) < kAgainAfter ||
					s_looked.count(Key(game)))
					continue;
			}
			s_queued.insert(Key(game));
			s_looked.erase(Key(game)); // looked for again: "Looking…" until it is done
			s_queue.emplace_back(game, again);
			queued++;
		}
		if (queued)
			ps5log::Line("[cheats] {} games to look for cheats for", queued);
		if (!s_running && !s_queue.empty())
		{
			s_running = true;
			std::thread(Worker).detach();
		}
	}

	size_t Pending()
	{
		std::lock_guard lock(s_mutex);
		return s_queue.size() + (s_busy ? 1 : 0);
	}

	int Looked(const Game& game)
	{
		{
			std::lock_guard lock(s_mutex);
			if (const auto it = s_looked.find(Key(game)); it != s_looked.end())
				return it->second;
		}
		if (Count(game))
			return kFound;
		return Now() - NoneSince(game) < kAgainAfter ? kNone : kNotLooked;
	}

	uint32_t Arrivals() { return s_arrivals; }

	void Stop()
	{
		s_stopped = true;
		{
			std::lock_guard lock(s_mutex);
			s_queue.clear();
		}
		// one on its way is let finish, as box art's (a few seconds at most), so nothing is written beside the game
		for (int waited = 0; s_busy && waited < 250; waited++)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}
