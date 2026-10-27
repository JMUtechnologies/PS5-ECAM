// SPDX-License-Identifier: GPL-3.0-or-later
#include "extras.h"
#include "standalone/mods.h"
#include "../app/own_status.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <set>
#include <unistd.h>

namespace ps5eden::extras
{
	namespace
	{
		namespace fs = std::filesystem;
		using Json = rapidjson::Value;
		using Document = rapidjson::Document;
		using Allocator = Document::AllocatorType;
		std::vector<Profile> Profiles(const Context& context);
		std::string Text(const Json& object, const char* key)
		{
			return object.IsObject() && object.HasMember(key) && object[key].IsString() ? object[key].GetString() : "";
		}
		bool Hex(const std::string& value, size_t length)
		{
			return value.size() == length && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c); });
		}
		std::string Title(uint64_t value)
		{
			char title[17];
			std::snprintf(title, sizeof(title), "%016llX", (unsigned long long)value);
			return title;
		}
		bool Load(const Context& context, Document& document, std::string& error)
		{
			std::error_code ec;
			// stat's where lstat is refused, as on the console's /data (app/own_status.h)
			const auto status = ps5files::OwnStatus(context.settings, ec);
			if (ec == std::errc::no_such_file_or_directory || (!ec && status.type() == fs::file_type::not_found))
			{
				document.SetObject();
				return true;
			}
			if (ec || status.type() != fs::file_type::regular || fs::file_size(context.settings, ec) > (4u << 20) || ec)
			{
				error = "Eden settings are not a readable regular file of at most 4 MiB.";
				return false;
			}
			std::ifstream in(context.settings, std::ios::binary);
			const std::string text{std::istreambuf_iterator<char>(in), {}};
			if (!in || document.Parse(text.data(), text.size()).HasParseError() || !document.IsObject())
			{
				error = "Eden settings could not be read; the original was left unchanged.";
				return false;
			}
			return true;
		}
		Json* Object(Json& parent, const char* key, Allocator& allocator, bool create)
		{
			if (!parent.IsObject()) return nullptr;
			if (!parent.HasMember(key))
			{
				if (!create) return nullptr;
				parent.AddMember(Json(key, allocator), Json(rapidjson::kObjectType), allocator);
			}
			return parent[key].IsObject() ? &parent[key] : nullptr;
		}
		std::pair<std::string, std::string> ProfileChoice(Document& document, const Context& context)
		{
			const auto known = Profiles(context);
			Json* profiles = Object(document, "profiles", document.GetAllocator(), false);
			std::string current = profiles ? Text(*profiles, "current") : "";
			std::string first = profiles ? Text(*profiles, "first") : "";
			const auto exists = [&](const std::string& id) { return std::any_of(known.begin(), known.end(), [&](const auto& profile) { return profile.id == id; }); };
			if (!known.empty())
			{
				Json* users = profiles ? Object(*profiles, "users", document.GetAllocator(), false) : nullptr;
				const std::string remembered = users && context.ps5User >= 0 ? Text(*users, std::to_string(context.ps5User).c_str()) : "";
				if (exists(remembered)) current = remembered;
				else if (!exists(current)) current = known.front().id;
				if (first.empty()) first = known.front().id;
			}
			return {current, first};
		}
		Json* ProfileSettings(Document& document, const Context& context, bool create)
		{
			auto& allocator = document.GetAllocator();
			if (document.HasMember("profiles") && !document["profiles"].IsObject()) return nullptr;
			const auto [current, first] = ProfileChoice(document, context);
			if (!Hex(current, 32) || current == first) return &document;
			Json* all = Object(document, "profile_settings", allocator, create);
			return all ? Object(*all, current.c_str(), allocator, create) : nullptr;
		}
		Json* Game(Document& document, const Context& context, bool create)
		{
			const uint64_t title = context.title;
			Json* profile = ProfileSettings(document, context, create);
			if (!profile) return nullptr;
			if (!title) return profile;
			Json* games = Object(*profile, "games", document.GetAllocator(), create);
			return games ? Object(*games, Title(title).c_str(), document.GetAllocator(), create) : nullptr;
		}
		std::vector<std::string> Strings(const Json* value, const char* key)
		{
			std::vector<std::string> result;
			if (value && value->HasMember(key) && (*value)[key].IsArray())
				for (const auto& entry : (*value)[key].GetArray())
					if (entry.IsString()) result.emplace_back(entry.GetString(), entry.GetStringLength());
			return result;
		}
		bool On(const Json* value, const char* key)
		{
			return !value || !value->HasMember(key) || !(*value)[key].IsBool() || (*value)[key].GetBool();
		}
		void Put(Json& object, const char* key, Json value, Allocator& allocator)
		{
			if (object.HasMember(key)) object[key] = std::move(value);
			else object.AddMember(Json(key, allocator), std::move(value), allocator);
		}
		void PutStrings(Json& object, const char* key, const std::vector<std::string>& values, Allocator& allocator)
		{
			Json array(rapidjson::kArrayType);
			for (const auto& value : values) array.PushBack(Json(value.c_str(), (rapidjson::SizeType)value.size(), allocator), allocator);
			Put(object, key, std::move(array), allocator);
		}
		void Choose(std::vector<std::string>& values, const std::string& name, bool include)
		{
			values.erase(std::remove(values.begin(), values.end(), name), values.end());
			if (include) values.push_back(name);
		}
		std::vector<Eden::Mods::Mod> Mods(const Context& context)
		{
			return context.title ? Eden::Mods::List({context.gameFiles + "/mods", context.gameFiles + "/cheats", context.gameFolder}, context.title) : std::vector<Eden::Mods::Mod>{};
		}
		void AddPresets(const fs::path& root, std::vector<std::string>& presets)
		{
			// Eden's catalog uses name=/chain= inside .fxp, not the filename. Bound discovery;
			// actual effect parsing/compilation remains Eden's responsibility at game launch.
			std::vector<std::pair<fs::path, int>> folders = {{root, 0}};
			std::vector<fs::path> files;
			for (size_t i = 0; i < folders.size() && i < 256; i++)
				for (const auto& entry : Eden::Mods::ListFolder(folders[i].first))
				{
					std::error_code ec;
					if (entry.is_symlink(ec)) continue;
					if (entry.is_directory(ec) && folders[i].second < 8 && folders.size() < 256)
						folders.push_back({entry.path(), folders[i].second + 1});
					else if (entry.is_regular_file(ec) && entry.path().extension() == ".fxp" && files.size() < 1024 && entry.file_size(ec) <= (64u << 10) && !ec)
						files.push_back(entry.path());
				}
			std::sort(files.begin(), files.end());
			for (const auto& file : files)
			{
				std::ifstream in(file);
				std::string line, name, chain;
				while (std::getline(in, line))
				{
					const auto equals = line.find('=');
					if (equals == std::string::npos) continue;
					const auto key = Eden::Patches::Trim(std::string_view(line).substr(0, equals));
					const auto value = Eden::Patches::Trim(std::string_view(line).substr(equals + 1));
					if (key == "name") name = value;
					else if (key == "chain") chain = value;
				}
				if (!name.empty() && name.size() <= 64 && !chain.empty() && name.find_first_of("/\\") == std::string::npos &&
					std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }) &&
					std::find(presets.begin(), presets.end(), name) == presets.end()) presets.push_back(name);
			}
		}
		bool Save(const Context& context, Document& document, std::string& error)
		{
			std::error_code ec;
			fs::create_directories(fs::path(context.settings).parent_path(), ec);
			if (ec) { error = "Eden settings folder could not be created."; return false; }
			// Exclusive staging avoids following a pre-existing .new symlink or overwriting another writer.
			const std::string staged = context.settings + ".ecam-new";
			const int fd = open(staged.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
			if (fd < 0) { error = "Eden settings could not be staged; remove a stale .ecam-new only after checking it."; return false; }
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			document.Accept(writer);
			size_t at = 0;
			while (at < buffer.GetSize())
			{
				const ssize_t count = write(fd, buffer.GetString() + at, buffer.GetSize() - at);
				if (count < 0 && errno == EINTR) continue;
				if (count <= 0) break;
				at += (size_t)count;
			}
			bool ok = at == buffer.GetSize();
			if (close(fd) != 0) ok = false;
			if (ok && fs::exists(context.settings, ec))
			{
				const std::string before = context.settings + ".ecam-before";
				if (!fs::exists(before, ec)) fs::copy_file(context.settings, before, fs::copy_options::none, ec);
				ok = !ec;
			}
			if (ok) { fs::rename(staged, context.settings, ec); ok = !ec; }
			if (!ok) { fs::remove(staged, ec); error = "Eden settings could not be saved; the original was left unchanged."; }
			return ok;
		}
		bool Edit(const Context& context, const std::function<bool(Document&, Json&, std::string&)>& edit, std::string& error)
		{
			if (context.title && (context.title >> 56) != 1) { error = "A real Switch title ID is required."; return false; }
			Document document;
			if (!Load(context, document, error)) return false;
			Json* game = Game(document, context, true);
			if (!game) { error = "Eden settings have an unexpected object shape; no changes were made."; return false; }
			return edit(document, *game, error) && Save(context, document, error);
		}
		std::vector<Profile> Profiles(const Context& context)
		{
			// Exact pinned standalone layout: header 0x10, eight 0xC8 entries, ID twice, timestamp, name.
			std::array<unsigned char, 0x10 + 8 * 0xC8> data{};
			std::ifstream file(context.user + "/nand/system/save/8000000000000010/su/avators/profiles.dat", std::ios::binary);
			if (!file.read((char*)data.data(), data.size())) return {};
			std::vector<Profile> result;
			for (size_t i = 0; i < 8; i++)
			{
				const auto* entry = data.data() + 0x10 + i * 0xC8;
				uint64_t low = 0, high = 0;
				std::memcpy(&low, entry, 8); std::memcpy(&high, entry + 8, 8);
				if (!low && !high) continue;
				char id[33];
				std::snprintf(id, sizeof(id), "%016llX%016llX", (unsigned long long)high, (unsigned long long)low);
				const char* name = (const char*)entry + 40;
				result.push_back({id, std::string(name, strnlen(name, 32))});
			}
			return result;
		}
		constexpr size_t kProfileHeader = 0x10, kProfileEntry = 0xC8, kProfileCount = 8;
		using ProfileBytes = std::array<unsigned char, kProfileHeader + kProfileEntry * kProfileCount>;
		std::string ProfileFile(const Context& context)
		{
			return context.user + "/nand/system/save/8000000000000010/su/avators/profiles.dat";
		}
		bool ReadProfileBytes(const Context& context, ProfileBytes& bytes, bool missingOK, std::string& error)
		{
			std::error_code ec;
			const auto file = ProfileFile(context);
			const auto status = ps5files::OwnStatus(file, ec);
			if (ec == std::errc::no_such_file_or_directory || (!ec && status.type() == fs::file_type::not_found))
			{
				if (missingOK) return true;
				error = "The player list is missing."; return false;
			}
			if (ec || status.type() != fs::file_type::regular || fs::file_size(file, ec) != bytes.size() || ec)
			{ error = "The player list has an unsupported format; it was left unchanged."; return false; }
			std::ifstream in(file, std::ios::binary);
			if (!in.read((char*)bytes.data(), bytes.size())) { error = "The player list could not be read."; return false; }
			std::set<std::array<unsigned char, 16>> known;
			for (size_t i = 0; i < kProfileCount; i++)
			{
				const auto* entry = bytes.data() + kProfileHeader + i * kProfileEntry;
				std::array<unsigned char, 16> id{};
				std::copy(entry, entry + 16, id.begin());
				if (std::all_of(id.begin(), id.end(), [](auto value) { return value == 0; })) continue;
				if (std::memcmp(entry, entry + 16, 16) != 0 || !known.insert(id).second)
				{ error = "The player list has inconsistent IDs; it was left unchanged."; return false; }
			}
			return true;
		}
		std::string ProfileKey(const unsigned char* entry)
		{
			uint64_t low = 0, high = 0;
			std::memcpy(&low, entry, 8); std::memcpy(&high, entry + 8, 8);
			if (!low && !high) return {};
			char key[33];
			std::snprintf(key, sizeof(key), "%016llX%016llX", (unsigned long long)high, (unsigned long long)low);
			return key;
		}
		bool ProfileName(const std::string& name, std::string& fitted, std::string& error)
		{
			// Validate UTF-8 and cut only at a whole character, fitting Eden's 31-byte name field.
			bool visible = false;
			for (size_t i = 0; i < name.size();)
			{
				const auto first = (unsigned char)name[i];
				size_t length = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 : first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
				if (!length || i + length > name.size() || first < 32 || first == 127)
				{ error = "Enter a player name without control characters."; return false; }
				for (size_t j = 1; j < length; j++)
					if (((unsigned char)name[i + j] & 0xC0) != 0x80) { error = "The player name is not valid UTF-8."; return false; }
				if ((first == 0xE0 && (unsigned char)name[i + 1] < 0xA0) || (first == 0xED && (unsigned char)name[i + 1] >= 0xA0) ||
					(first == 0xF0 && (unsigned char)name[i + 1] < 0x90) || (first == 0xF4 && (unsigned char)name[i + 1] >= 0x90))
				{ error = "The player name is not valid UTF-8."; return false; }
				if (i + length <= 31) { fitted.append(name, i, length); visible = visible || first != ' '; }
				i += length;
			}
			if (!visible) { error = "Enter a nonempty player name."; return false; }
			return true;
		}
		bool WriteProfileBytes(const Context& context, const ProfileBytes& bytes, std::string& error)
		{
			const std::string file = ProfileFile(context), staged = file + ".ecam-new";
			std::error_code ec;
			fs::create_directories(fs::path(file).parent_path(), ec);
			if (ec) { error = "The player folder could not be created."; return false; }
			const int fd = open(staged.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
			if (fd < 0) { error = "The player list could not be staged; inspect any existing .ecam-new file."; return false; }
			size_t at = 0;
			while (at < bytes.size())
			{
				const ssize_t written = write(fd, bytes.data() + at, bytes.size() - at);
				if (written < 0 && errno == EINTR) continue;
				if (written <= 0) break;
				at += (size_t)written;
			}
			bool ok = at == bytes.size();
			if (close(fd) != 0) ok = false;
			if (ok && fs::exists(file, ec))
			{
				const std::string before = file + ".ecam-before";
				if (!fs::exists(before, ec)) fs::copy_file(file, before, fs::copy_options::none, ec);
				ok = !ec;
			}
			if (ok) { fs::rename(staged, file, ec); ok = !ec; }
			if (!ok) { fs::remove(staged, ec); error = "The player list was not replaced."; }
			else error.clear();
			return ok;
		}
	}

	View Read(const Context& context)
	{
		View view;
		Document document;
		if (!Load(context, document, view.error)) return view;
		Json* game = Game(document, context, false);
		const auto disabled = Strings(game, "mods_off"), chosen = Strings(game, "cheats_on"), off = Strings(game, "cheats_off");
		view.modsOn = On(game, "mods"); view.cheatsOn = On(game, "cheats");
		const auto mods = Mods(context);
		for (const auto& mod : mods)
			if (!Eden::Mods::CheatsOnly(mod)) view.mods.push_back({mod.name, mod.name, !Eden::Mods::Has(disabled, mod.name)});
		for (const auto& cheat : Eden::Mods::CheatItems(mods, disabled, chosen, off))
			view.cheats.push_back({cheat.mod, cheat.name, cheat.on});
		view.presets = {"", "Anime", "Cinematic", "Clean", "Dreamy", "Retro", "Vivid"};
		if (!context.gameFolder.empty())
			for (const char* folder : {"reshade", "ReShade", "Reshade", "RESHADE"})
			{
				std::error_code ec;
				const fs::path path = fs::path(context.gameFolder) / folder;
				if (fs::is_directory(path, ec)) { AddPresets(path, view.presets); break; }
			}
		AddPresets(fs::path(context.user) / "post_presets", view.presets);
		Json* profile = ProfileSettings(document, context, false);
		if (profile && profile->HasMember("video") && (*profile)["video"].IsObject()) view.preset = Text((*profile)["video"], "post_processing");
		if (context.title && game && game->HasMember("post_processing") && (*game)["post_processing"].IsString())
		{
			view.ownPreset = true;
			view.preset = Text(*game, "post_processing");
		}
		// Preserve a custom selection instead of silently changing it to Off.
		if (std::find(view.presets.begin(), view.presets.end(), view.preset) == view.presets.end()) view.presets.push_back(view.preset);
		view.profiles = Profiles(context);
		const auto current = ProfileChoice(document, context).first;
		for (size_t i = 0; i < view.profiles.size(); i++)
			if (view.profiles[i].id == current) view.profile = (int)i;
		return view;
	}

	bool SetMaster(const Context& context, bool cheats, bool on, std::string& error)
	{
		if (!context.title) { error = "A real Switch title ID is required."; return false; }
		return Edit(context, [&](Document& d, Json& game, std::string&) { Put(game, cheats ? "cheats" : "mods", Json(on), d.GetAllocator()); return true; }, error);
	}
	bool SetMod(const Context& context, const std::string& name, bool on, std::string& error)
	{
		const auto mods = Mods(context);
		if (std::none_of(mods.begin(), mods.end(), [&](const auto& mod) { return mod.name == name && !Eden::Mods::CheatsOnly(mod); }))
		{ error = "That mod is no longer present."; return false; }
		return Edit(context, [&](Document& d, Json& game, std::string&) {
			auto disabled = Strings(&game, "mods_off"); Choose(disabled, name, !on);
			PutStrings(game, "mods_off", disabled, d.GetAllocator()); return true;
		}, error);
	}
	bool SetCheat(const Context& context, const std::string& source, const std::string& name, bool on, std::string& error)
	{
		return Edit(context, [&](Document& d, Json& game, std::string& why) {
			const auto mods = Mods(context);
			auto disabled = Strings(&game, "mods_off"), chosen = Strings(&game, "cheats_on"), off = Strings(&game, "cheats_off");
			const auto items = Eden::Mods::CheatItems(mods, disabled, chosen, off);
			const auto item = std::find_if(items.begin(), items.end(), [&](const auto& entry) { return entry.mod == source && entry.name == name; });
			if (item == items.end()) { why = "That cheat is no longer present."; return false; }
			using Switch = Eden::Mods::CheatItem::Switch;
			if (item->by == Switch::mod) Choose(disabled, source, !on);
			else if (item->by == Switch::own) Choose(off, Eden::Cheats::Key(source, name), !on);
			else for (const auto& mod : mods) if (mod.name == source) chosen = Eden::Mods::ChooseCheat(mod, name, on, chosen);
			PutStrings(game, "mods_off", disabled, d.GetAllocator());
			PutStrings(game, "cheats_on", chosen, d.GetAllocator());
			PutStrings(game, "cheats_off", off, d.GetAllocator()); return true;
		}, error);
	}
	bool SetPreset(const Context& context, const std::string& name, bool inherit, std::string& error)
	{
		if (name.size() > 64 || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127 || c == '/' || c == '\\'; }))
		{ error = "Invalid preset name."; return false; }
		return Edit(context, [&](Document& d, Json& game, std::string& why) {
			Json* video = context.title ? &game : Object(game, "video", d.GetAllocator(), true);
			if (!video) { why = "Eden video settings are not an object."; return false; }
			if (inherit) video->RemoveMember("post_processing");
			else Put(*video, "post_processing", Json(name.c_str(), d.GetAllocator()), d.GetAllocator());
			return true;
		}, error);
	}
	bool SelectProfile(const Context& context, const std::string& id, int ps5User, std::string& error)
	{
		const auto profiles = Profiles(context);
		if (std::none_of(profiles.begin(), profiles.end(), [&](const auto& profile) { return profile.id == id; }))
		{ error = "That profile is no longer present."; return false; }
		Document document;
		if (!Load(context, document, error)) return false;
		Json* settings = Object(document, "profiles", document.GetAllocator(), true);
		if (!settings) { error = "Eden profiles settings are not an object."; return false; }
		Put(*settings, "current", Json(id.c_str(), document.GetAllocator()), document.GetAllocator());
		if (Text(*settings, "first").empty()) Put(*settings, "first", Json(profiles.front().id.c_str(), document.GetAllocator()), document.GetAllocator());
		if (ps5User >= 0)
		{
			Json* users = Object(*settings, "users", document.GetAllocator(), true);
			if (!users) { error = "Eden profile user mapping is not an object."; return false; }
			Put(*users, std::to_string(ps5User).c_str(), Json(id.c_str(), document.GetAllocator()), document.GetAllocator());
		}
		return Save(context, document, error);
	}
	bool CreateProfile(const Context& context, const std::string& name, std::string& error)
	{
		std::string fitted;
		if (!ProfileName(name, fitted, error)) return false;
		ProfileBytes bytes{};
		if (!ReadProfileBytes(context, bytes, true, error)) return false;
		size_t slot = kProfileCount;
		std::set<std::string> known;
		for (size_t i = 0; i < kProfileCount; i++)
		{
			const auto key = ProfileKey(bytes.data() + kProfileHeader + i * kProfileEntry);
			if (key.empty() && slot == kProfileCount) slot = i;
			else if (!key.empty()) known.insert(key);
		}
		if (slot == kProfileCount) { error = "Eden supports at most eight players."; return false; }
		auto* entry = bytes.data() + kProfileHeader + slot * kProfileEntry;
		std::fill(entry, entry + kProfileEntry, 0);
		try
		{
			std::random_device random;
			bool unique = false;
			for (int attempt = 0; attempt < 32 && !unique; attempt++)
			{
				for (size_t at = 0; at < 16; at += 4) { const uint32_t value = random(); std::memcpy(entry + at, &value, 4); }
				const auto key = ProfileKey(entry);
				unique = !key.empty() && !known.count(key);
			}
			if (!unique) { error = "A unique player ID could not be generated."; return false; }
		}
		catch (const std::exception& failure) { error = std::string("Player ID generation failed: ") + failure.what(); return false; }
		std::copy(entry, entry + 16, entry + 16);
		std::copy(fitted.begin(), fitted.end(), entry + 40);
		return WriteProfileBytes(context, bytes, error);
	}
	bool RenameProfile(const Context& context, const std::string& id, const std::string& name, std::string& error)
	{
		if (id.empty()) { error = "Choose an existing player first."; return false; }
		std::string fitted;
		if (!ProfileName(name, fitted, error)) return false;
		ProfileBytes bytes{};
		if (!ReadProfileBytes(context, bytes, false, error)) return false;
		for (size_t i = 0; i < kProfileCount; i++)
		{
			auto* entry = bytes.data() + kProfileHeader + i * kProfileEntry;
			if (ProfileKey(entry) != id) continue;
			std::fill(entry + 40, entry + 72, 0); std::copy(fitted.begin(), fitted.end(), entry + 40);
			return WriteProfileBytes(context, bytes, error);
		}
		error = "That player is no longer present."; return false;
	}
	bool RemoveProfile(const Context& context, const std::string& id, std::string& error)
	{
		ProfileBytes bytes{}, next{};
		if (!ReadProfileBytes(context, bytes, false, error)) return false;
		std::copy(bytes.begin(), bytes.begin() + kProfileHeader, next.begin());
		size_t count = 0; bool found = false;
		for (size_t i = 0; i < kProfileCount; i++)
		{
			const auto* entry = bytes.data() + kProfileHeader + i * kProfileEntry;
			const auto key = ProfileKey(entry);
			if (key.empty()) continue;
			if (key == id) { found = true; continue; }
			std::copy(entry, entry + kProfileEntry, next.begin() + kProfileHeader + count++ * kProfileEntry);
		}
		if (!found) { error = "That player is no longer present."; return false; }
		if (!count) { error = "At least one player must remain."; return false; }
		return WriteProfileBytes(context, next, error);
	}
}
