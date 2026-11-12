// SPDX-License-Identifier: GPL-3.0-or-later
#include "cheats.h"
#include "paths.h"
#include "../eden/library.h"
#include "../frontend/launcher.h"
#include "../melonds/melonds.h"
#include "../retro/porpoise_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <strings.h>

namespace fs = std::filesystem;

namespace ps5cheats
{
	namespace
	{
		using ps5launcher::System;

		std::string Trim(std::string text)
		{
			while (!text.empty() && std::isspace((unsigned char)text.back()))
				text.pop_back();
			size_t start = 0;
			while (start < text.size() && std::isspace((unsigned char)text[start]))
				start++;
			return text.substr(start);
		}

		std::string ReadText(const std::string& path)
		{
			std::ifstream in(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), {});
		}

		std::vector<std::string> Lines(const std::string& text)
		{
			std::vector<std::string> lines;
			std::istringstream in(text);
			for (std::string line; std::getline(in, line);)
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				lines.push_back(std::move(line));
			}
			return lines;
		}

		// written beside, then put in place: a cheat file half written is never read
		bool WriteText(const std::string& path, const std::string& text, std::string& error)
		{
			std::error_code ec;
			fs::create_directories(fs::path(path).parent_path(), ec);
			const std::string staged = path + ".new";
			{
				std::ofstream out(staged, std::ios::binary | std::ios::trunc);
				out << text;
				if (!out.good())
				{
					error = "could not write " + staged;
					return false;
				}
			}
			fs::rename(staged, path, ec);
			if (ec)
			{
				error = ec.message();
				fs::remove(staged, ec);
				return false;
			}
			return true;
		}

		std::string Stem(const std::string& file) { return fs::path(file).stem().string(); }

		bool Exists(const std::string& path)
		{
			std::error_code ec;
			return !path.empty() && fs::is_regular_file(path, ec);
		}

		std::string Hex16(uint64_t id)
		{
			char text[17];
			std::snprintf(text, sizeof(text), "%016llX", (unsigned long long)id);
			return text;
		}

		// melonDS's: its own folder's file, else one beside the game (port/melonds/core.cpp's CheatPath)
		std::string NdsFile(const std::string& game)
		{
			const std::string own = ps5melonds::Root() + "/cheats/" + Stem(game) + ".mch";
			const std::string beside = (fs::path(game).parent_path() / (Stem(game) + ".mch")).string();
			return !Exists(own) && Exists(beside) ? beside : own;
		}
	}

	Kind KindOf(int side)
	{
		const System system = (System)side;
		if (system == System::N3ds)
			return Kind::N3ds;
		if (system == System::Nds)
			return Kind::Nds;
		if (system == System::Switch)
			return Kind::Switch;
		if (!ps5retro::IsRetro(side))
			return Kind::None; // the Wii U, the Switch 2
		if (ps5retro::Is(side, "gamecube") || ps5retro::Is(side, "wii"))
			return Kind::Porpoise;
		// the cores whose retro_cheat_set does something (Beetle VB's, PokeMini's and the Game & Watch's are empty)
		const std::string core = ps5retro::Of(side).core;
		return core == "mesen" || core == "mesens" || core == "mgba" || core == "paralleln64" ? Kind::Retro : Kind::None;
	}

	std::string FileFor(const Game& game)
	{
		switch (KindOf(game.side))
		{
		case Kind::Retro: return std::string(ps5retro::Of(game.side).data) + "/cheats/" + Stem(game.file) + ".cht";
		case Kind::Nds: return NdsFile(game.file);
		case Kind::N3ds: return game.titleId ? ps5paths::Azahar() + "/cheats/" + Hex16(game.titleId) + ".txt" : std::string();
		case Kind::Switch: return game.titleId ? ps5eden::GameFilesFolder() + "/cheats/" + Hex16(game.titleId) : std::string();
		case Kind::Porpoise:
		{
			const std::string id = ps5porpoise::GameKey(game.file);
			return id.size() == 6 ? ps5porpoise::Data() + "/cheats/" + id + ".txt" : std::string();
		}
		case Kind::None: break;
		}
		return {};
	}

	// -- RetroArch's .cht -----------------------------------------------------------------------------

	std::vector<Cheat> ParseCht(const std::string& text)
	{
		std::map<int, Cheat> byIndex;
		for (const std::string& raw : Lines(text))
		{
			const std::string line = Trim(raw);
			const size_t equals = line.find('=');
			if (line.rfind("cheat", 0) != 0 || equals == std::string::npos)
				continue;
			const std::string key = Trim(line.substr(0, equals));
			std::string value = Trim(line.substr(equals + 1));
			if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
				value = value.substr(1, value.size() - 2);
			const size_t underscore = key.find('_');
			if (underscore == std::string::npos || underscore <= 5)
				continue;
			const std::string number = key.substr(5, underscore - 5);
			if (!std::all_of(number.begin(), number.end(), [](char c) { return std::isdigit((unsigned char)c); }))
				continue;
			Cheat& cheat = byIndex[std::atoi(number.c_str())];
			const std::string field = key.substr(underscore + 1);
			if (field == "desc")
				cheat.name = value;
			else if (field == "code")
				cheat.code = value;
			else if (field == "enable")
				cheat.on = value == "true" || value == "1";
		}
		std::vector<Cheat> cheats;
		for (auto& [index, cheat] : byIndex)
			cheats.push_back(std::move(cheat));
		return cheats;
	}

	std::string FormatCht(const std::vector<Cheat>& cheats)
	{
		std::string out = "cheats = " + std::to_string(cheats.size()) + "\n";
		for (size_t i = 0; i < cheats.size(); i++)
		{
			std::string name = cheats[i].name;
			std::replace(name.begin(), name.end(), '"', '\'');
			out += "\ncheat" + std::to_string(i) + "_desc = \"" + name + "\"\n";
			if (!cheats[i].code.empty())
			{
				out += "cheat" + std::to_string(i) + "_code = \"" + cheats[i].code + "\"\n";
				out += "cheat" + std::to_string(i) + "_enable = " + (cheats[i].on ? "true" : "false") + "\n";
			}
		}
		return out;
	}

	// -- melonDS's .mch -------------------------------------------------------------------------------

	std::string ChtToMch(const std::string& text)
	{
		std::string out;
		bool category = false;
		const auto name = [](std::string n) {
			std::replace(n.begin(), n.end(), '\n', ' ');
			return n.substr(0, 120); // melonDS reads 127 characters of a name
		};
		for (const Cheat& cheat : ParseCht(text))
		{
			if (cheat.code.empty())
			{
				out += "\nCAT " + name(cheat.name.empty() ? "Cheats" : cheat.name) + "\n";
				category = true;
				continue;
			}
			// the code's words, two to a line, as an Action Replay lists them
			std::vector<std::string> words;
			std::string word;
			for (const char c : cheat.code + "+")
			{
				if (std::isxdigit((unsigned char)c))
					word += (char)std::toupper((unsigned char)c);
				else if (!word.empty())
					words.push_back(std::move(word)), word.clear();
			}
			if (words.empty() || words.size() % 2 || std::any_of(words.begin(), words.end(), [](const std::string& w) { return w.size() != 8; }))
				continue; // not an Action Replay code
			if (!category)
			{
				out += "CAT Cheats\n";
				category = true;
			}
			out += "\nCODE " + std::string(cheat.on ? "1 " : "0 ") + name(cheat.name.empty() ? "Cheat" : cheat.name) + "\n";
			for (size_t i = 0; i < words.size(); i += 2)
				out += words[i] + " " + words[i + 1] + "\n";
		}
		return out;
	}

	namespace
	{
		// The CODE lines of a .mch, in melonDS's order (its categories' codes where they are)
		std::vector<Cheat> ParseMch(const std::string& text, std::vector<size_t>* lineOf = nullptr)
		{
			std::vector<Cheat> cheats;
			const std::vector<std::string> lines = Lines(text);
			for (size_t i = 0; i < lines.size(); i++)
			{
				const std::string line = Trim(lines[i]);
				if (line.size() < 7 || strncasecmp(line.c_str(), "CODE", 4) != 0 || !std::isspace((unsigned char)line[4]))
					continue;
				std::string rest = Trim(line.substr(4));
				const size_t space = rest.find_first_of(" \t");
				if (space == std::string::npos)
					continue;
				cheats.push_back({Trim(rest.substr(space)), "", std::atoi(rest.substr(0, space).c_str()) != 0});
				if (lineOf)
					lineOf->push_back(i);
			}
			return cheats;
		}

		// Azahar's cheats with the line each one's [name] is on, in its order (gateway_cheat.cpp's LoadFile: a
		// name with no code lines is no cheat)
		std::vector<Cheat> ParseGatewayLines(const std::string& text, std::vector<size_t>* lineOf)
		{
			std::vector<Cheat> cheats;
			const std::vector<std::string> lines = Lines(text);
			Cheat current;
			size_t at = 0;
			bool any = false;
			const auto flush = [&] {
				if (any)
				{
					cheats.push_back(current);
					if (lineOf)
						lineOf->push_back(at);
				}
				current = {};
				any = false;
			};
			for (size_t i = 0; i < lines.size(); i++)
			{
				const std::string line = Trim(lines[i]);
				if (line.size() >= 2 && line.front() == '[')
				{
					flush();
					current.name = line.substr(1, line.size() - 2);
					at = i;
				}
				else if (!line.empty() && line.front() == '*')
					current.on |= line == "*citra_enabled";
				else if (!line.empty())
				{
					current.code += (current.code.empty() ? "" : "+") + line;
					any = true;
				}
			}
			flush();
			return cheats;
		}
	}

	std::vector<Cheat> ParseGateway(const std::string& text) { return ParseGatewayLines(text, nullptr); }

	// -- codes.rc24.xyz's Gecko codes, as Porpoise's .txt ---------------------------------------------

	std::string GeckoTxt(const std::string& text, const std::string& id)
	{
		const std::vector<std::string> lines = Lines(text);
		if (lines.empty() || Trim(lines[0]) != id)
			return {}; // not the game's (an unknown ID, an error page)
		const auto codeLine = [](const std::string& line) {
			std::istringstream words(line);
			std::string address, data, more;
			words >> address >> data;
			const auto hex = [](const std::string& w) { return w.size() == 8 && std::all_of(w.begin(), w.end(), [](char c) { return std::isxdigit((unsigned char)c); }); };
			return hex(address) && hex(data) && !(words >> more);
		};
		size_t versions = 0;
		for (const std::string& line : lines)
			versions += Trim(line).rfind("Version ", 0) == 0;
		// a block: lines between blank ones; a "Version" heading names the codes after it
		std::string out = "# " + id + ": Gecko codes downloaded from codes.rc24.xyz by Playtendoh, off until turned on\n";
		std::string version;
		std::vector<std::string> block;
		size_t codes = 0;
		const auto flush = [&] {
			if (block.empty())
				return;
			size_t first = 0;
			while (first < block.size() && !codeLine(block[first]))
				first++;
			if (block.size() == 1 && block[0].rfind("Version ", 0) == 0)
				version = block[0].substr(8);
			else if (first > 0 && first < block.size())
			{
				// the name (the last line before the codes: Dolphin reads [..] as the author)
				std::string name = block[first - 1];
				if (versions > 1 && !version.empty())
					name = "(" + version + ") " + name;
				if (name.front() == '[')
					name = "(" + name.substr(1, name.find(']') == std::string::npos ? std::string::npos : name.find(']') - 1) + ")" +
						(name.find(']') == std::string::npos ? "" : name.substr(name.find(']') + 1));
				out += "\n$" + name + "\n";
				for (size_t i = first; i < block.size(); i++)
					out += (codeLine(block[i]) || block[i].front() == '*' ? "" : "*") + block[i] + "\n";
				codes++;
			}
			block.clear();
		};
		for (size_t i = 2; i < lines.size(); i++)
		{
			const std::string line = Trim(lines[i]);
			if (line.empty())
				flush();
			else
				block.push_back(line);
		}
		flush();
		return codes ? out : std::string();
	}

	// -- a game's cheats --------------------------------------------------------------------------------

	std::vector<Cheat> List(const Game& game) { return ListFile(KindOf(game.side), FileFor(game)); }

	bool SetOn(const Game& game, size_t index, bool on, std::string& error) { return SetOnInFile(KindOf(game.side), FileFor(game), index, on, error); }

	std::vector<Cheat> ListFile(Kind kind, const std::string& path)
	{
		if (!Exists(path))
			return {};
		std::vector<Cheat> cheats;
		switch (kind)
		{
		case Kind::Retro:
			for (Cheat& cheat : ParseCht(ReadText(path)))
				if (!cheat.code.empty())
					cheats.push_back(std::move(cheat));
			break;
		case Kind::Nds: cheats = ParseMch(ReadText(path)); break;
		case Kind::N3ds: cheats = ParseGateway(ReadText(path)); break;
		default: break;
		}
		return cheats;
	}

	bool SetOnInFile(Kind kind, const std::string& path, size_t index, bool on, std::string& error)
	{
		const std::string text = ReadText(path);
		switch (kind)
		{
		case Kind::Retro:
		{
			std::vector<Cheat> all = ParseCht(text);
			size_t n = 0;
			for (Cheat& cheat : all)
				if (!cheat.code.empty() && n++ == index)
				{
					cheat.on = on;
					return WriteText(path, FormatCht(all), error);
				}
			break;
		}
		case Kind::Nds:
		case Kind::N3ds:
		{
			std::vector<size_t> at;
			const bool nds = kind == Kind::Nds;
			nds ? ParseMch(text, &at) : ParseGatewayLines(text, &at);
			if (index >= at.size())
				break;
			std::vector<std::string> lines = Lines(text);
			if (nds)
			{
				// CODE 1 <name> or CODE 0 <name>
				std::string& line = lines[at[index]];
				const size_t code = line.find_first_not_of(" \t");
				std::string rest = Trim(line.substr(code + 4));
				line = line.substr(0, code) + "CODE " + (on ? "1" : "0") + rest.substr(rest.find_first_of(" \t"));
			}
			else
			{
				// *citra_enabled right after the [name], as Azahar writes it
				size_t end = at[index] + 1;
				while (end < lines.size() && Trim(lines[end]).rfind('[', 0) != 0)
					end++;
				for (size_t i = end; i-- > at[index] + 1;)
					if (Trim(lines[i]) == "*citra_enabled")
						lines.erase(lines.begin() + i);
				if (on)
					lines.insert(lines.begin() + at[index] + 1, "*citra_enabled");
			}
			std::string out;
			for (const std::string& line : lines)
				out += line + "\n";
			return WriteText(path, out, error);
		}
		default: break;
		}
		error = "no such cheat";
		return false;
	}

	size_t Count(const Game& game)
	{
		const std::string path = FileFor(game);
		switch (KindOf(game.side))
		{
		case Kind::Switch:
		{
			// the most any one build's file has: a cheat a [name], as Atmosphère's (the {master} code not one)
			size_t most = 0;
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(path, ec))
				if (entry.path().extension() == ".txt")
				{
					size_t count = 0;
					for (const std::string& line : Lines(ReadText(entry.path().string())))
						count += Trim(line).rfind('[', 0) == 0;
					most = std::max(most, count);
				}
			return most;
		}
		case Kind::Porpoise:
		{
			size_t count = 0;
			for (const std::string& line : Lines(ReadText(path)))
				count += !line.empty() && line[0] == '$';
			return count;
		}
		default: return List(game).size();
		}
	}
}
