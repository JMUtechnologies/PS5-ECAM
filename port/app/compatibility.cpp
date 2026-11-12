// SPDX-License-Identifier: GPL-3.0-or-later
#include "compatibility.h"
#include "lang.h"
#include "paths.h"

#include <rapidjson/document.h>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace ps5compat
{
	namespace
	{
		// a feed as read: never changed once made, never freed (Find's reports stay where they are)
		struct Table
		{
			std::vector<Report> reports;
			std::unordered_map<std::string, size_t> byId;	// "side:titleid"
			std::unordered_map<std::string, size_t> byName; // "side:key"
		};

		std::mutex s_mutex;
		std::string s_bundled, s_cache;
		bool s_pathsSet = false, s_loaded = false;
		std::vector<std::unique_ptr<Table>> s_tables;
		const Table* s_current = nullptr;
		int s_generation = 0;

		// lower case letters and digits only
		std::string Key(const std::string& name)
		{
			std::string key;
			for (const char c : name)
				if (std::isalnum((unsigned char)c))
					key += (char)std::tolower((unsigned char)c);
			return key;
		}

		std::string IdKey(const std::string& side, uint64_t titleId)
		{
			char id[17];
			std::snprintf(id, sizeof(id), "%016llx", (unsigned long long)titleId);
			return side + ":" + id;
		}

		std::string Text(const rapidjson::Value& game, const char* name)
		{
			const auto it = game.FindMember(name);
			return it != game.MemberEnd() && it->value.IsString() ? std::string(it->value.GetString(), it->value.GetStringLength()) : std::string();
		}

		int Number(const rapidjson::Value& game, const char* name)
		{
			const auto it = game.FindMember(name);
			return it != game.MemberEnd() && it->value.IsInt() ? it->value.GetInt() : 0;
		}

		// the feed's words for a status, as the launcher shows it; null: one it does not know
		const char* Shown(const std::string& status)
		{
			// tr: how a game runs, from the compatibility list and players' reports: a badge on its cover and page
			static constexpr const char* kStatuses[] = {ps5lang::TrMarkC("status", "Playable"), ps5lang::TrMarkC("status", "Issues"),
				ps5lang::TrMarkC("status", "Not playable")};
			return status == "playable" ? kStatuses[0] : status == "issues" ? kStatuses[1] : status == "broken" ? kStatuses[2] : nullptr;
		}

		std::unique_ptr<Table> Parse(const std::string& text)
		{
			rapidjson::Document json;
			json.Parse(text.c_str(), text.size());
			if (json.HasParseError() || !json.IsObject() || !json.HasMember("schema") || !json["schema"].IsInt() || json["schema"].GetInt() != 1 ||
				!json.HasMember("games") || !json["games"].IsArray())
				return nullptr;
			auto table = std::make_unique<Table>();
			for (const rapidjson::Value& game : json["games"].GetArray())
			{
				if (!game.IsObject())
					continue;
				const std::string side = Text(game, "side"), name = Text(game, "name"), id = Text(game, "titleId");
				const char* status = Shown(Text(game, "status"));
				if (side.empty() || !status || (name.empty() && id.empty()))
					continue;
				Report report{name, Text(game, "region"), status, Text(game, "notes"), Number(game, "consoles"), Number(game, "sessions"),
					Number(game, "ended")};
				const size_t at = table->reports.size();
				table->reports.push_back(std::move(report));
				if (id.size() == 16)
					table->byId.emplace(IdKey(side, std::strtoull(id.c_str(), nullptr, 16)), at);
				if (const std::string key = Key(name); !key.empty())
					table->byName.emplace(side + ":" + key, at);
			}
			return table;
		}

		std::string Read(const std::string& path)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file)
				return {};
			std::ostringstream text;
			text << file.rdbuf();
			return text.str();
		}

		const std::string& Bundled()
		{
			if (!s_pathsSet)
				s_bundled = ps5paths::Assets() + "/compatibility.json";
			return s_bundled;
		}

		const std::string& Cache()
		{
			if (!s_pathsSet)
				s_cache = ps5paths::Cache() + "/compatibility.json";
			return s_cache;
		}

		// under s_mutex
		void Use(std::unique_ptr<Table> table)
		{
			s_current = table.get();
			s_tables.push_back(std::move(table));
			s_generation++;
		}

		// under s_mutex: the kept copy, else the bundled one, once
		void Load()
		{
			if (s_loaded)
				return;
			s_loaded = true;
			if (s_current)
				return; // the fetch's came first
			for (const std::string* path : {&Cache(), &Bundled()})
				if (!path->empty())
					if (auto table = Parse(Read(*path)))
					{
						Use(std::move(table));
						return;
					}
		}
	}

	const Report* Find(const std::string& side, uint64_t titleId, const std::string& name)
	{
		std::lock_guard lock(s_mutex);
		Load();
		if (!s_current)
			return nullptr;
		if (titleId)
			if (const auto it = s_current->byId.find(IdKey(side, titleId)); it != s_current->byId.end())
				return &s_current->reports[it->second];
		const std::string key = Key(name);
		if (key.empty())
			return nullptr;
		const auto it = s_current->byName.find(side + ":" + key);
		return it == s_current->byName.end() ? nullptr : &s_current->reports[it->second];
	}

	const char* Kind(const std::string& status)
	{
		if (status == "Playable")
			return "good";
		if (status == "Issues")
			return "warn";
		if (status == "Not playable")
			return "bad";
		return "";
	}

	int Generation()
	{
		std::lock_guard lock(s_mutex);
		return s_generation;
	}

	bool Take(const std::string& text)
	{
		auto table = Parse(text);
		if (!table)
			return false;
		std::lock_guard lock(s_mutex);
		s_loaded = true;
		Use(std::move(table));
		return true;
	}

	std::string CachePath()
	{
		std::lock_guard lock(s_mutex);
		return Cache();
	}

	std::string CachedTag()
	{
		std::string path = CachePath();
		if (path.empty() || !Parse(Read(path)))
			return {}; // no copy that reads: the whole feed, whatever the server's tag
		std::string tag = Read(path + ".etag");
		while (!tag.empty() && (tag.back() == '\n' || tag.back() == '\r' || tag.back() == ' '))
			tag.pop_back();
		return tag;
	}

	bool Keep(const std::string& text, const std::string& etag)
	{
		if (!Take(text))
			return false;
		const std::string path = CachePath();
		if (path.empty())
			return true;
		std::error_code error;
		std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
		{
			std::ofstream file(path + ".part", std::ios::binary | std::ios::trunc);
			file << text;
			if (!file)
				return true; // read, not kept: the next launch fetches it whole again
		}
		std::filesystem::rename(path + ".part", path, error);
		if (error)
			return true;
		std::ofstream tag(path + ".etag", std::ios::trunc);
		tag << etag;
		return true;
	}

	void SetPaths(const std::string& bundled, const std::string& cache)
	{
		std::lock_guard lock(s_mutex);
		s_pathsSet = true;
		s_bundled = bundled;
		s_cache = cache;
		s_loaded = false;
		s_current = nullptr;
	}
}
