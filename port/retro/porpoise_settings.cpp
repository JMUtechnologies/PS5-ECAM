// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Porpoise-X's settings files (porpoise_settings.h)
#include "porpoise_settings.h"
#include "registry.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace ps5porpoise
{
	namespace
	{
		std::string Trim(std::string s)
		{
			while (!s.empty() && std::isspace((unsigned char)s.back()))
				s.pop_back();
			size_t i = 0;
			while (i < s.size() && std::isspace((unsigned char)s[i]))
				i++;
			return s.substr(i);
		}

		// "key = value", as Porpoise's Settings::load reads it; false for a comment or anything else
		bool Split(const std::string& line, std::string& key, std::string& value)
		{
			const std::string s = Trim(line);
			const size_t equals = s.find('=');
			if (s.empty() || s[0] == '#' || equals == std::string::npos)
				return false;
			key = Trim(s.substr(0, equals));
			value = Trim(s.substr(equals + 1));
			return !key.empty();
		}

		std::vector<std::string> Lines(const std::string& file)
		{
			std::vector<std::string> lines;
			std::ifstream in(file);
			for (std::string line; std::getline(in, line);)
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				lines.push_back(line);
			}
			return lines;
		}

		// Written beside it, then put in its place, as Porpoise's own open_atomic
		bool Save(const std::string& file, const std::vector<std::string>& lines, std::string& error)
		{
			std::error_code ec;
			fs::create_directories(fs::path(file).parent_path(), ec);
			const std::string temporary = file + ".tmp";
			{
				std::ofstream out(temporary, std::ios::trunc);
				for (const std::string& line : lines)
					out << line << '\n';
				if (!out.flush())
				{
					error = "cannot write " + temporary;
					return false;
				}
			}
			fs::rename(temporary, file, ec);
			if (ec)
			{
				error = "cannot replace " + file + ": " + ec.message();
				fs::remove(temporary, ec);
				return false;
			}
			return true;
		}

		bool IdCharacters(const char* id)
		{
			for (int i = 0; i < 6; i++)
				if (!std::isupper((unsigned char)id[i]) && !std::isdigit((unsigned char)id[i]))
					return false;
			return true;
		}
	}

	bool Plays(int side, int backend) { return ps5retro::IsRetro(side) && std::strcmp(ps5retro::BackendFor(side, backend).runtime, "retro/porpoise/eboot.bin") == 0; }
	std::string Data() { return ps5retro::Find("gamecube")->data; }
	std::string SettingsFile() { return Data() + "/settings.ini"; }
	std::string LogsFolder() { return Data() + "/logs"; }

	std::string LastCrash(const std::vector<std::string>& traces, int64_t after)
	{
		for (const std::string& trace : traces)
		{
			struct stat info{};
			if (stat(trace.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
				continue;
			if ((int64_t)info.st_mtime <= after)
				return {};
			// its end: a crash is the last thing Porpoise writes
			std::ifstream in(trace, std::ios::binary);
			constexpr std::streamoff kTail = 256 * 1024;
			if (info.st_size > kTail)
				in.seekg(info.st_size - kTail);
			std::string found;
			for (std::string line; std::getline(in, line);)
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.rfind("crash: signal ", 0) == 0)
					found = line.substr(7);
				else if (line.rfind("terminate: ", 0) == 0)
					found = line;
			}
			return found.empty() ? std::string() : "Porpoise: " + found.substr(0, 150);
		}
		return {};
	}

	void WriteAchievementsAccount(const std::string& user, const std::string& token, int points)
	{
		const std::string file = Data() + "/retroachievements.ini", staged = file + ".new";
		{
			std::ofstream out(staged, std::ios::trunc);
			out << "# RetroAchievements: the account, from Playtendoh (a token, not the password)\nuser=" << user << "\ntoken=" << token << "\npoints=" << points << '\n';
			if (!out)
				return;
		}
		std::rename(staged.c_str(), file.c_str());
	}

	void ClearAchievementsAccount()
	{
		std::error_code ec;
		fs::remove(Data() + "/retroachievements.ini", ec);
	}

	void WriteSession(const std::string& side, const std::string& title, const std::string& details, const std::string& cover, const std::string& banner)
	{
		const std::string file = Data() + "/playtendoh-session.txt", staged = file + ".new";
		{
			std::ofstream out(staged, std::ios::trunc);
			const auto line = [&](const char* key, std::string value) {
				std::replace(value.begin(), value.end(), '\n', ' ');
				out << key << '=' << value << '\n';
			};
			line("side", side);
			line("title", title);
			line("details", details);
			line("cover", cover);
			line("banner", banner);
			if (!out)
				return;
		}
		std::rename(staged.c_str(), file.c_str());
	}
	std::string GameFile(const std::string& gamePath) { return Data() + "/game-settings/" + GameKey(gamePath) + ".ini"; }

	std::string GameKey(const std::string& gamePath)
	{
		char head[0x8006] = {};
		std::ifstream in(gamePath, std::ios::binary);
		in.read(head, sizeof(head));
		const size_t got = (size_t)in.gcount();
		// where each format keeps the disc's own header, its ID first
		size_t at = 0;
		if (got >= 4 && std::memcmp(head, "WBFS", 4) == 0)
			at = 0x200;
		else if (got >= 4 && (std::memcmp(head, "WIA\x01", 4) == 0 || std::memcmp(head, "RVZ\x01", 4) == 0))
			at = 0x58; // the file's head (0x48 bytes), then the disc's: four fields, then its header
		else if (got >= 4 && std::memcmp(head, "CISO", 4) == 0)
			at = 0x8000;
		if (got >= at + 6 && IdCharacters(head + at))
			return std::string(head + at, 6);
		return fs::path(gamePath).filename().string();
	}

	std::map<std::string, std::string> Read(const std::string& file)
	{
		std::map<std::string, std::string> values;
		std::string key, value;
		for (const std::string& line : Lines(file))
			if (Split(line, key, value))
				values[key] = value;
		return values;
	}

	bool Write(const std::string& file, const std::map<std::string, std::string>& values, std::string& error)
	{
		std::vector<std::string> lines = Lines(file);
		if (lines.empty() && file.find("/game-settings/") != std::string::npos)
			lines.push_back("# This game's own settings (Playtendoh, as Porpoise > Details > Game settings)");
		else if (lines.empty())
		{
			// with the version of the settings Porpoise writes: a settings.ini without one is an older
			// Porpoise's, whose values it moves (an output resolution up one, the shaders' mode)
			lines.push_back("# Porpoise settings (written by Playtendoh's Settings)");
			lines.push_back(std::string("settings_version = ") + kSettingsVersion);
		}
		std::map<std::string, std::string> left = values;
		std::string key, value;
		for (std::string& line : lines)
			if (Split(line, key, value))
				if (const auto it = left.find(key); it != left.end())
				{
					line = key + " = " + it->second;
					left.erase(it);
				}
		for (const auto& [k, v] : left)
			lines.push_back(k + " = " + v);
		return Save(file, lines, error);
	}

	bool Remove(const std::string& file, const std::vector<std::string>& keys, std::string& error)
	{
		std::vector<std::string> lines = Lines(file), kept;
		bool any = false;
		std::string key, value;
		for (const std::string& line : lines)
		{
			const bool setting = Split(line, key, value);
			if (setting && std::find(keys.begin(), keys.end(), key) != keys.end())
				continue;
			any |= setting;
			kept.push_back(line);
		}
		std::error_code ec;
		if (!any)
			return fs::remove(file, ec) || !ec;
		return Save(file, kept, error);
	}
}
