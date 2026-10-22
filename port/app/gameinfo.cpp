// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameinfo.h"
#include "lang.h"
#include "paths.h"
#include "../ps5/log.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ps5gameinfo
{
	using ps5lang::TrMarkC;

	namespace
	{
		// One system's database: its text, and where each game's line starts
		struct Database
		{
			bool loaded = false;
			std::string text;
			std::unordered_map<std::string, size_t> lines;
			bool titled = false;
			std::unordered_map<std::string, std::string> titles; // a title (Simplified) -> its game's ID, once FindByTitle asks
		};

		std::mutex s_mutex;
		std::string s_folder;
		std::map<ps5boxart::System, Database> s_databases;

		std::string Folder()
		{
			return s_folder.empty() ? ps5paths::Assets() + "/gametdb" : s_folder;
		}

		// The file whole (gzip, as render-gametdb.py writes it), and each line's ID
		void Load(ps5boxart::System system, Database& database)
		{
			database.loaded = true;
			const std::string path = Folder() + (system == ps5boxart::System::WiiU ? "/wiiu.tsv.gz" :
				system == ps5boxart::System::Nds ? "/ds.tsv.gz" : system == ps5boxart::System::Switch ? "/switch.tsv.gz" : "/3ds.tsv.gz");
			gzFile file = gzopen(path.c_str(), "rb");
			if (!file)
			{
				ps5log::Line("[gameinfo] {} cannot be read: no game information", path);
				return;
			}
			std::vector<char> buffer(1 << 16);
			for (int read; (read = gzread(file, buffer.data(), (unsigned)buffer.size())) > 0;)
				database.text.append(buffer.data(), (size_t)read);
			gzclose(file);
			for (size_t start = 0; start < database.text.size();)
			{
				size_t end = database.text.find('\n', start);
				if (end == std::string::npos)
					end = database.text.size();
				const size_t tab = database.text.find('\t', start);
				if (tab != std::string::npos && tab < end)
					database.lines.emplace(database.text.substr(start, tab - start), start);
				start = end + 1;
			}
			ps5log::Line("[gameinfo] {}: {} games", path, database.lines.size());
		}

		// A field as written: \t, \n and \\ back to what they stand for
		std::string Unescape(std::string_view field)
		{
			std::string out;
			out.reserve(field.size());
			for (size_t i = 0; i < field.size(); i++)
			{
				if (field[i] != '\\' || i + 1 == field.size())
				{
					out += field[i];
					continue;
				}
				const char next = field[++i];
				out += next == 't' ? '\t' : next == 'n' ? '\n' : next;
			}
			return out;
		}

		// A title as titles are compared: its letters and digits, lower case, accents and full-width
		// letters folded ("Pokémon™ Sword" and "POKEMON SWORD" are one), "&" as "and"; another script's
		// letters as they are, its punctuation (・, 「」) left out
		std::string Simplified(std::string_view title)
		{
			// U+00C0 to U+00FF without their accents (× and ÷ are not letters)
			static constexpr char kLatin1[] = "aaaaaaaceeeeiiiidnooooo ouuuuyts"
											  "aaaaaaaceeeeiiiidnooooo ouuuuyty";
			std::string out;
			for (size_t i = 0; i < title.size();)
			{
				const unsigned char lead = (unsigned char)title[i];
				const size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
				if (i + length > title.size())
					break;
				uint32_t code = length == 4 ? lead & 0x07 : length == 3 ? lead & 0x0F : length == 2 ? lead & 0x1F : lead;
				for (size_t k = 1; k < length; k++)
					code = code << 6 | ((unsigned char)title[i + k] & 0x3F);
				const std::string_view bytes = title.substr(i, length);
				i += length;
				if (code >= 0xFF01 && code <= 0xFF5E)
					code -= 0xFEE0; // full-width ASCII
				if (code < 0x80)
				{
					if (std::isalnum((int)code))
						out += (char)std::tolower((int)code);
					else if (code == '&')
						out += "and";
				}
				else if (code >= 0xC0 && code <= 0xFF)
				{
					if (kLatin1[code - 0xC0] != ' ')
						out += kLatin1[code - 0xC0];
				}
				// symbols and punctuation (™, dashes, quotes, CJK's and full-width's) left out
				else if (code >= 0x100 && !(code >= 0x2000 && code <= 0x2BFF) && !(code >= 0x3000 && code <= 0x303F) && code != 0x30FB &&
					!(code >= 0xFE30 && code <= 0xFE4F) && !(code >= 0xFF00 && code <= 0xFF65))
					out += bytes;
			}
			return out;
		}

		// Each title's game, for FindByTitle. A title several games have (a game's editions for each
		// region: AURVA to AURVL) is the one GameTDB has for every region, its ID ending in A, else one
		// for all regions or America's, else the first
		void IndexTitles(Database& database)
		{
			database.titled = true;
			std::unordered_map<std::string, int> ranks;
			for (size_t start = 0; start < database.text.size();)
			{
				size_t end = database.text.find('\n', start);
				if (end == std::string::npos)
					end = database.text.size();
				const std::string_view line = std::string_view(database.text).substr(start, end - start);
				start = end + 1;
				const size_t idEnd = line.find('\t');
				const size_t titleEnd = idEnd == std::string_view::npos ? idEnd : line.find('\t', idEnd + 1);
				if (titleEnd == std::string_view::npos)
					continue;
				const size_t regionEnd = std::min(line.find('\t', titleEnd + 1), line.size());
				const std::string_view id = line.substr(0, idEnd), region = line.substr(titleEnd + 1, regionEnd - titleEnd - 1);
				const std::string key = Simplified(Unescape(line.substr(idEnd + 1, titleEnd - idEnd - 1)));
				if (id.empty() || key.empty())
					continue;
				const int rank = (id.back() == 'A' ? 0 : 2) + (region == "ALL" || region.find("USA") != std::string_view::npos ? 0 : 1);
				const auto [it, added] = ranks.emplace(key, rank);
				if (added || rank < it->second)
				{
					it->second = rank;
					database.titles[key] = std::string(id);
				}
			}
		}
	}

	std::string FindByTitle(ps5boxart::System system, const std::string& title)
	{
		const std::string key = Simplified(title);
		if (key.empty())
			return {};
		std::lock_guard lock(s_mutex);
		Database& database = s_databases[system];
		if (!database.loaded)
			Load(system, database);
		if (!database.titled)
			IndexTitles(database);
		const auto it = database.titles.find(key);
		return it == database.titles.end() ? std::string() : it->second;
	}

	bool Find(ps5boxart::System system, const std::string& id, Info& info)
	{
		if (id.empty())
			return false;
		std::lock_guard lock(s_mutex);
		Database& database = s_databases[system];
		if (!database.loaded)
			Load(system, database);
		const auto it = database.lines.find(id);
		if (it == database.lines.end())
			return false;
		size_t end = database.text.find('\n', it->second);
		if (end == std::string::npos)
			end = database.text.size();
		std::vector<std::string> fields;
		for (size_t start = it->second; start <= end;)
		{
			size_t tab = database.text.find('\t', start);
			if (tab == std::string::npos || tab > end)
				tab = end;
			fields.push_back(Unescape(std::string_view(database.text).substr(start, tab - start)));
			start = tab + 1;
		}
		fields.resize(10);
		info = {};
		info.id = fields[0];
		info.title = fields[1];
		info.region = fields[2];
		info.synopsis = fields[3];
		info.developer = fields[4];
		info.publisher = fields[5];
		info.released = fields[6];
		info.genre = fields[7];
		info.players = std::atoi(fields[8].c_str());
		info.rating = fields[9];
		return true;
	}

	std::string ReleaseDate(const std::string& released)
	{
		int year = 0, month = 0, day = 0;
		const int parts = std::sscanf(released.c_str(), "%d-%d-%d", &year, &month, &day);
		if (parts < 1 || year <= 0)
			return {};
		if (parts < 2 || month < 1 || month > 12)
			return std::to_string(year);
		if (parts < 3 || day < 1)
			return ps5lang::MonthAndYear(year, month);
		return ps5lang::Date(year, month, day);
	}

	std::string Year(const std::string& released)
	{
		return released.size() >= 4 ? released.substr(0, 4) : std::string();
	}

	std::string Genres(const std::string& genre, int most)
	{
		// GameTDB's genres, as its data names them, and how the hub says them (tr: a game's genre)
		static const std::unordered_map<std::string, const char*> kNames = {
			{"2d platformer", TrMarkC("genre", "2D platformer")}, {"3d fighting", TrMarkC("genre", "3D fighting")},
			{"3d platformer", TrMarkC("genre", "3D platformer")}, {"action", TrMarkC("genre", "Action")},
			{"action rpg", TrMarkC("genre", "Action RPG")}, {"adventure", TrMarkC("genre", "Adventure")}, {"arcade", TrMarkC("genre", "Arcade")},
			{"baseball", TrMarkC("genre", "Baseball")}, {"basketball", TrMarkC("genre", "Basketball")},
			{"beat 'em up", TrMarkC("genre", "Beat 'em up")}, {"billiards", TrMarkC("genre", "Billiards")},
			{"board game", TrMarkC("genre", "Board game")}, {"bowling", TrMarkC("genre", "Bowling")}, {"boxing", TrMarkC("genre", "Boxing")},
			{"business simulation", TrMarkC("genre", "Business simulation")}, {"cards", TrMarkC("genre", "Cards")},
			{"chess", TrMarkC("genre", "Chess")}, {"coaching", TrMarkC("genre", "Coaching")}, {"compilation", TrMarkC("genre", "Compilation")},
			{"construction simulation", TrMarkC("genre", "Construction simulation")}, {"cooking", TrMarkC("genre", "Cooking")},
			{"dance", TrMarkC("genre", "Dance")}, {"darts", TrMarkC("genre", "Darts")}, {"demo", TrMarkC("genre", "Demo")},
			{"drawing", TrMarkC("genre", "Drawing")}, {"educational", TrMarkC("genre", "Educational")}, {"exercise", TrMarkC("genre", "Exercise")},
			{"fantasy", TrMarkC("genre", "Fantasy")}, {"fighting", TrMarkC("genre", "Fighting")},
			{"first-person shooter", TrMarkC("genre", "First-person shooter")}, {"fishing", TrMarkC("genre", "Fishing")},
			{"fitness", TrMarkC("genre", "Fitness")}, {"flight simulation", TrMarkC("genre", "Flight simulation")},
			{"football", TrMarkC("genre", "American football")}, {"futuristic racing", TrMarkC("genre", "Futuristic racing")},
			{"golf", TrMarkC("genre", "Golf")}, {"health", TrMarkC("genre", "Health")}, {"hidden object", TrMarkC("genre", "Hidden object")},
			{"historic", TrMarkC("genre", "Historic")}, {"hockey", TrMarkC("genre", "Hockey")}, {"horror", TrMarkC("genre", "Horror")},
			{"hunting", TrMarkC("genre", "Hunting")}, {"interactive movie", TrMarkC("genre", "Interactive movie")},
			{"karaoke", TrMarkC("genre", "Karaoke")}, {"kart racing", TrMarkC("genre", "Kart racing")},
			{"life simulation", TrMarkC("genre", "Life simulation")}, {"management simulation", TrMarkC("genre", "Management simulation")},
			{"motorcycle racing", TrMarkC("genre", "Motorcycle racing")}, {"multimedia", TrMarkC("genre", "Multimedia")},
			{"music", TrMarkC("genre", "Music")}, {"off-road racing", TrMarkC("genre", "Off-road racing")}, {"party", TrMarkC("genre", "Party")},
			{"pinball", TrMarkC("genre", "Pinball")}, {"platformer", TrMarkC("genre", "Platformer")},
			{"point-and-click", TrMarkC("genre", "Point-and-click")}, {"poker", TrMarkC("genre", "Poker")}, {"puzzle", TrMarkC("genre", "Puzzle")},
			{"racing", TrMarkC("genre", "Racing")}, {"rail shooter", TrMarkC("genre", "Rail shooter")},
			{"real-time strategy", TrMarkC("genre", "Real-time strategy")}, {"rhythm", TrMarkC("genre", "Rhythm")},
			{"roguelike", TrMarkC("genre", "Roguelike")}, {"role-playing", TrMarkC("genre", "Role-playing")},
			{"run and gun", TrMarkC("genre", "Run and gun")}, {"sci-fi", TrMarkC("genre", "Sci-fi")},
			{"shoot 'em up", TrMarkC("genre", "Shoot 'em up")}, {"shooter", TrMarkC("genre", "Shooter")},
			{"simulation", TrMarkC("genre", "Simulation")}, {"skateboarding", TrMarkC("genre", "Skateboarding")},
			{"snowboarding", TrMarkC("genre", "Snowboarding")}, {"soccer", TrMarkC("genre", "Football")},
			{"software", TrMarkC("genre", "Software")}, {"sports", TrMarkC("genre", "Sports")},
			{"stealth action", TrMarkC("genre", "Stealth action")}, {"strategy", TrMarkC("genre", "Strategy")},
			{"strategy rpg", TrMarkC("genre", "Strategy RPG")}, {"survival horror", TrMarkC("genre", "Survival horror")},
			{"table tennis", TrMarkC("genre", "Table tennis")}, {"tactical rpg", TrMarkC("genre", "Tactical RPG")},
			{"tennis", TrMarkC("genre", "Tennis")}, {"third-person shooter", TrMarkC("genre", "Third-person shooter")},
			{"tower defense", TrMarkC("genre", "Tower defence")}, {"train simulation", TrMarkC("genre", "Train simulation")},
			{"trivia", TrMarkC("genre", "Trivia")}, {"truck racing", TrMarkC("genre", "Truck racing")},
			{"turn-based strategy", TrMarkC("genre", "Turn-based strategy")}, {"virtual pet", TrMarkC("genre", "Virtual pet")},
			{"volleyball", TrMarkC("genre", "Volleyball")}, {"wargame", TrMarkC("genre", "Wargame")},
			{"watercraft racing", TrMarkC("genre", "Watercraft racing")}, {"wrestling", TrMarkC("genre", "Wrestling")}};
		std::vector<std::string> names;
		int count = 0;
		for (size_t start = 0; start <= genre.size();)
		{
			size_t end = genre.find(',', start);
			if (end == std::string::npos)
				end = genre.size();
			std::string name = genre.substr(start, end - start);
			start = end + 1;
			if (name.empty())
				continue;
			if (most > 0 && ++count > most)
				break;
			const auto known = kNames.find(name);
			if (known != kNames.end())
				name = ps5lang::TrC("genre", known->second);
			else
				name[0] = (char)std::toupper((unsigned char)name[0]);
			names.push_back(name);
		}
		return ps5lang::List(names);
	}

	void SetFolder(const std::string& folder)
	{
		std::lock_guard lock(s_mutex);
		s_folder = folder;
		s_databases.clear();
	}
}
