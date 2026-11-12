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
#include <cstring>
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
				system == ps5boxart::System::Nds ? "/ds.tsv.gz" : system == ps5boxart::System::Switch ? "/switch.tsv.gz" :
				system == ps5boxart::System::GameCubeWii ? "/wii.tsv.gz" : "/3ds.tsv.gz");
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

	namespace
	{
		bool s_manualsLoaded = false;							// under s_mutex
		std::unordered_map<std::string, std::string> s_manuals; // under s_mutex: "<system>\t<name>" -> its address
		bool s_cartsLoaded = false;							  // under s_mutex
		std::unordered_map<std::string, std::string> s_carts; // under s_mutex: "<system>\t<name>" -> the rest of its line

		// The table whole (tools/render-carts.py), under s_mutex
		void LoadCarts()
		{
			s_cartsLoaded = true;
			const std::string path = Folder() + "/launchbox-carts.tsv.gz";
			gzFile file = gzopen(path.c_str(), "rb");
			if (!file)
			{
				ps5log::Line("[gameinfo] {} cannot be read: the Timeline draws the cartridges", path);
				return;
			}
			std::string text;
			std::vector<char> buffer(1 << 16);
			for (int read; (read = gzread(file, buffer.data(), (unsigned)buffer.size())) > 0;)
				text.append(buffer.data(), (size_t)read);
			gzclose(file);
			for (size_t start = 0; start < text.size();)
			{
				size_t end = text.find('\n', start);
				if (end == std::string::npos)
					end = text.size();
				const size_t first = text.find('\t', start);
				const size_t second = first == std::string::npos ? first : text.find('\t', first + 1);
				if (second != std::string::npos && second < end)
					s_carts.emplace(text.substr(start, second - start), text.substr(second + 1, end - second - 1));
				start = end + 1;
			}
			ps5log::Line("[gameinfo] {}: {} names", path, s_carts.size());
		}

		// A game's name as the table has it (as boxart.cpp's Words match libretro's): lower case, what is in
		// brackets left out (regions, revisions), letters and digits run together, without "the", "of", "and",
		// "a", "version", "edition"
		std::string CartName(std::string name)
		{
			for (const auto& [from, to] : {std::pair<const char*, const char*>{"\xc3\xa9", "e"}, {"\xc3\x89", "e"}, {"&", " and "}})
				for (size_t at = 0; (at = name.find(from, at)) != std::string::npos; at += std::strlen(to))
					name.replace(at, std::strlen(from), to);
			static const char* const kLeftOut[] = {"the", "version", "edition", "a", "and", "of"};
			std::string out, word;
			int depth = 0;
			const auto flush = [&] {
				if (std::none_of(std::begin(kLeftOut), std::end(kLeftOut), [&](const char* left) { return word == left; }))
					out += word;
				word.clear();
			};
			for (const char c : name)
			{
				if (c == '(' || c == '[')
					flush(), depth++;
				else if ((c == ')' || c == ']') && depth)
					depth--;
				else if (!depth && std::isalnum((unsigned char)c))
					word += (char)std::tolower((unsigned char)c);
				else
					flush();
			}
			flush();
			return out;
		}

		// The region a game file's name gives (No-Intro's): U America (and the world's), E Europe (its
		// countries, Australia), J Japan; 0 when it names none
		char CartRegion(const std::string& name)
		{
			const auto has = [&](const char* tag) { return name.find(tag) != std::string::npos; };
			if (has("(USA") || has(", USA") || has("(World"))
				return 'U';
			for (const char* europe : {"(Europe", "(Germany", "(France", "(Spain", "(Italy", "(UK", "(Australia", "(Netherlands", "(Sweden", "(Scandinavia"})
				if (has(europe))
					return 'E';
			if (has("(Japan"))
				return 'J';
			return 0;
		}

		// Which shape a region's cartridges have: the Famicom's not the NES's, the Super Famicom's (Japan's and
		// Europe's) not the Super NES's (America's); every Game Boy's and Nintendo 64's one
		int CartShape(const std::string& system, char region)
		{
			if (system == "nes")
				return region == 'J' ? 1 : 0;
			if (system == "snes")
				return region == 'U' || region == 'W' ? 0 : 1;
			return 0;
		}
	}

	std::string FindManual(const std::string& system, const std::vector<std::string>& names)
	{
		std::lock_guard lock(s_mutex);
		if (!s_manualsLoaded)
		{
			// the table whole: system, name, title, address a line
			s_manualsLoaded = true;
			const std::string path = Folder() + "/manuals.tsv.gz";
			if (gzFile file = gzopen(path.c_str(), "rb"))
			{
				std::string text;
				std::vector<char> buffer(1 << 14);
				for (int read; (read = gzread(file, buffer.data(), (unsigned)buffer.size())) > 0;)
					text.append(buffer.data(), (size_t)read);
				gzclose(file);
				for (size_t start = 0; start < text.size();)
				{
					size_t end = text.find('\n', start);
					if (end == std::string::npos)
						end = text.size();
					const size_t first = text.find('\t', start);
					const size_t second = first == std::string::npos ? first : text.find('\t', first + 1);
					const size_t third = second == std::string::npos ? second : text.find('\t', second + 1);
					if (third != std::string::npos && third < end)
						s_manuals.emplace(text.substr(start, second - start), text.substr(third + 1, end - third - 1));
					start = end + 1;
				}
			}
			else
				ps5log::Line("[gameinfo] {} cannot be read: no manuals", path);
		}
		for (const std::string& name : names)
			if (const std::string key = CartName(name); key.size() >= 2)
				if (const auto at = s_manuals.find(system + '\t' + key); at != s_manuals.end())
					return at->second;
		return {};
	}

	bool FindCart(const std::string& system, const std::string& fileName, Cart& cart)
	{
		cart = {};
		std::string name = CartName(fileName);
		if (system == "gw" && name.rfind("gamewatch", 0) == 0)
			name.erase(0, 9); // libretro's names, "Game & Watch - Donkey Kong II": the database's, "Donkey Kong II"
		if (name.size() < 2)
			return false;
		std::string line;
		{
			std::lock_guard lock(s_mutex);
			if (!s_cartsLoaded)
				LoadCarts();
			// a Game Boy game in the Game Boy Color's list, or one the other way round, as libretro's covers are
			for (const std::string& in : {system, std::string(system == "gbc" ? "gb" : system == "gb" ? "gbc" : "")})
				if (const auto at = s_carts.find(in + '\t' + name); !in.empty() && at != s_carts.end())
				{
					line = at->second;
					break;
				}
		}
		// year, publisher, the pictures as region=file
		const size_t first = line.find('\t'), second = first == std::string::npos ? first : line.find('\t', first + 1);
		if (second == std::string::npos)
			return false;
		cart.year = std::atoi(line.substr(0, first).c_str());
		cart.publisher = line.substr(first + 1, second - first - 1);
		// the cartridge of the file's own region, else the world's or another shaped as it is; with no region
		// named, America's first
		const char own = CartRegion(fileName) ? CartRegion(fileName) : 'U';
		int best = 0;
		for (size_t at = second + 1; at < line.size();)
		{
			size_t end = line.find(',', at);
			if (end == std::string::npos)
				end = line.size();
			if (end - at > 2 && line[at + 1] == '=')
			{
				const char region = line[at];
				const int score = region == own ? 4 : CartShape(system, region) != CartShape(system, own) ? 0 : region == 'W' ? 3 : region == 'U' ? 2 : 1;
				if (score > best)
					best = score, cart.picture = line.substr(at + 2, end - at - 2);
			}
			at = end + 1;
		}
		return true;
	}

	void SetFolder(const std::string& folder)
	{
		std::lock_guard lock(s_mutex);
		s_folder = folder;
		s_databases.clear();
		s_carts.clear();
		s_cartsLoaded = false;
		s_manuals.clear();
		s_manualsLoaded = false;
	}
}
