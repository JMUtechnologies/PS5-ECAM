// SPDX-License-Identifier: GPL-3.0-or-later
#include "reports.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

namespace ps5reports
{
	namespace
	{
		// -- the scrubber --------------------------------------------------------------------------

		bool IsAlnum(char c) { return std::isalnum((unsigned char)c) != 0; }
		bool IsDigit(char c) { return std::isdigit((unsigned char)c) != 0; }
		bool IsHex(char c) { return std::isxdigit((unsigned char)c) != 0; }
		bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
		bool IsWord(char c) { return IsAlnum(c) || c == '_' || (unsigned char)c >= 0x80; }
		char Lower(char c) { return (char)std::tolower((unsigned char)c); }

		// whether text has `word` at `at`, whatever its case
		bool HasAt(std::string_view text, size_t at, std::string_view word)
		{
			if (at + word.size() > text.size())
				return false;
			for (size_t i = 0; i < word.size(); i++)
				if (Lower(text[at + i]) != Lower(word[i]))
					return false;
			return true;
		}

		// the values the app knows, as whole words, whatever their case
		std::string RemoveSecrets(std::string_view text, const std::vector<std::string>& secrets)
		{
			std::string out(text);
			for (const std::string& secret : secrets)
			{
				if (secret.size() < 3)
					continue; // a one-letter name would take every such letter with it
				std::string next;
				size_t i = 0;
				while (i < out.size())
				{
					const bool before = i == 0 || !IsWord(out[i - 1]) || !IsWord(secret.front());
					const size_t end = i + secret.size();
					if (before && HasAt(out, i, secret) && (end >= out.size() || !IsWord(out[end]) || !IsWord(secret.back())))
					{
						next += "<redacted>";
						i = end;
					}
					else
						next += out[i++];
				}
				out = std::move(next);
			}
			return out;
		}

		// aa:bb:cc:dd:ee:ff or aa-bb-cc-dd-ee-ff
		size_t Mac(std::string_view t, size_t i)
		{
			if (i + 17 > t.size())
				return 0;
			const char separator = t[i + 2];
			if (separator != ':' && separator != '-')
				return 0;
			for (int group = 0; group < 6; group++)
			{
				const size_t at = i + group * 3;
				if (!IsHex(t[at]) || !IsHex(t[at + 1]) || (group < 5 && t[at + 2] != separator))
					return 0;
			}
			return i + 17 < t.size() && (IsAlnum(t[i + 17]) || t[i + 17] == separator) ? 0 : 17;
		}

		// an IPv6 address: hex groups and colons with "::" in it, or six colons and more
		size_t Ipv6(std::string_view t, size_t i)
		{
			size_t end = i;
			int colons = 0;
			bool twoColons = false, hex = false;
			while (end < t.size() && (IsHex(t[end]) || t[end] == ':'))
			{
				if (t[end] == ':')
				{
					colons++;
					twoColons |= end + 1 < t.size() && t[end + 1] == ':';
				}
				else
					hex = true;
				end++;
			}
			if (!hex || colons < 2 || (!twoColons && colons < 6))
				return 0;
			if (end < t.size() && IsWord(t[end]))
				return 0; // ps5log::Line and the like
			// a zone or the dotted end of a mapped address go with it
			while (end < t.size() && (IsAlnum(t[end]) || t[end] == '.' || t[end] == '%'))
				end++;
			return end - i;
		}

		// 192.168.1.20: four numbers, each three digits at most
		size_t Ipv4(std::string_view t, size_t i)
		{
			size_t at = i;
			for (int part = 0; part < 4; part++)
			{
				size_t digits = 0;
				while (at < t.size() && IsDigit(t[at]) && digits < 4)
					at++, digits++;
				if (digits == 0 || digits > 3)
					return 0;
				if (part < 3)
				{
					if (at >= t.size() || t[at] != '.')
						return 0;
					at++;
				}
			}
			if (at < t.size() && (IsAlnum(t[at]) || (t[at] == '.' && at + 1 < t.size() && IsDigit(t[at + 1]))))
				return 0;
			return at - i;
		}

		// name@host.tld
		size_t Email(std::string_view t, size_t i)
		{
			size_t at = i;
			while (at < t.size() && (IsAlnum(t[at]) || t[at] == '.' || t[at] == '_' || t[at] == '+' || t[at] == '-'))
				at++;
			if (at == i || at >= t.size() || t[at] != '@')
				return 0;
			size_t end = at + 1;
			bool dot = false;
			while (end < t.size() && (IsAlnum(t[end]) || t[end] == '.' || t[end] == '-'))
				dot |= t[end++] == '.';
			return dot && end > at + 3 ? end - i : 0;
		}

		// a token, a key, a hash: 32 hex digits and more (a title ID is 16)
		size_t LongHex(std::string_view t, size_t i)
		{
			size_t end = i;
			while (end < t.size() && IsHex(t[end]))
				end++;
			return end - i >= 32 && (end >= t.size() || !IsWord(t[end])) ? end - i : 0;
		}

		// "user: ...", "token=...", "nickname "..."": the word and its sign stay, what follows goes, to the
		// line's end or the next , ; or ). The longer words first.
		constexpr const char* kKeys[] = {"display name", "displayname", "user name", "user_name", "username", "online id", "online_id", "onlineid",
			"account id", "account_id", "accountid", "nickname", "password", "hostname", "api key", "api_key", "apikey", "account", "e-mail", "passwd",
			"secret", "serial", "server", "email", "login", "owner", "share", "token", "wi-fi", "host", "mail", "nick", "room", "ssid", "user", "wifi",
			"mac", "psn", "pwd", "ip"};
		size_t KeyValue(std::string_view t, size_t i, std::string& out)
		{
			for (const char* key : kKeys)
			{
				const size_t length = std::strlen(key);
				if (!HasAt(t, i, key))
					continue;
				size_t at = i + length;
				if (at < t.size() && Lower(t[at]) == 's')
					at++; // users, tokens
				const size_t sign = at;
				while (at < t.size() && t[at] == ' ')
					at++;
				if (at >= t.size())
					continue;
				const bool quoted = at > sign && (t[at] == '"' || t[at] == '\'');
				if (t[at] != '=' && t[at] != ':' && !quoted)
					continue;
				if (!quoted)
					at++;
				size_t end = at;
				while (end < t.size() && t[end] != '\n' && t[end] != ',' && t[end] != ';' && t[end] != ')')
					end++;
				// nothing after the sign (a line's label, "[main] host:") is nothing to remove
				bool any = false;
				for (size_t k = at; k < end; k++)
					any |= !IsSpace(t[k]);
				if (!any)
					continue;
				out.append(t.substr(i, at - i));
				out += quoted ? "<redacted>" : " <redacted>";
				return end - i;
			}
			return 0;
		}

		// a stretch with no path in it: the addresses, the long hex and the named values
		std::string ScrubTokens(std::string_view t)
		{
			std::string out;
			out.reserve(t.size());
			for (size_t i = 0; i < t.size();)
			{
				size_t used = 0;
				if (i == 0 || !IsWord(t[i - 1]))
				{
					if ((used = Email(t, i)))
						out += "<email>";
					else if ((used = Mac(t, i)))
						out += "<mac>";
					else if ((used = Ipv6(t, i)))
						out += "<ip>";
					else if ((i == 0 || t[i - 1] != '.') && (used = Ipv4(t, i)))
						out += "<ip>";
					else if ((used = LongHex(t, i)))
						out += "<hex>";
				}
				// a named value's word may follow a sign of its own: ra_user=, net.host:
				if (!used && (i == 0 || !IsAlnum(t[i - 1])))
					used = KeyValue(t, i, out);
				if (used)
					i += used;
				else
					out += t[i++];
			}
			return out;
		}

		// "name.ext" somewhere in it: a file's name rather than a folder's
		bool HasExtension(std::string_view tail)
		{
			for (size_t i = 0; i + 1 < tail.size(); i++)
			{
				if (tail[i] != '.' || i == 0 || IsSpace(tail[i - 1]))
					continue;
				size_t end = i + 1;
				while (end < tail.size() && IsAlnum(tail[end]))
					end++;
				if (end - i - 1 >= 1 && end - i - 1 <= 5)
					return true;
			}
			return false;
		}

		// One line. A path or a URL has folders in it, and folders have people's names: from where the
		// first one starts to the line's last slash, all of it becomes "<path>", and what follows that
		// slash stays only when it is a file's name. Folder names may have spaces, brackets and quotes in
		// them, so nothing but the line's end ends a path: two paths in a line leave the second's file.
		std::string ScrubLine(std::string_view line)
		{
			// a slash with something right after it
			auto separator = [&](size_t i) { return line[i] == '/' && i + 1 < line.size() && !IsSpace(line[i + 1]); };
			// Is there a path in the line at all? One that starts at a slash (/data/..., sdmc:/...), a
			// Windows one, or a word with two slashes or a slash and a file's name in it (games/3ds/x.3ds).
			// "radv/ps5 submissions" and "30/60 fps" are none of these, nor is a date.
			bool path = false;
			size_t last = 0;
			for (size_t i = 0; i < line.size(); i++)
			{
				if (line[i] == '/' || line[i] == '\\')
					last = i;
				if (line[i] == '\\' && i + 1 < line.size() && !IsSpace(line[i + 1]))
					path = true;
				else if (separator(i) && (i == 0 || !IsWord(line[i - 1])))
					path = true;
			}
			for (size_t i = 0; i < line.size() && !path;)
			{
				size_t end = i;
				int slashes = 0;
				bool letters = false;
				while (end < line.size() && !IsSpace(line[end]))
				{
					slashes += line[end] == '/';
					letters |= std::isalpha((unsigned char)line[end]) != 0;
					end++;
				}
				path = letters && (slashes >= 2 || (slashes == 1 && HasExtension(line.substr(i, end - i))));
				i = end + 1;
			}
			size_t first = std::string_view::npos;
			for (size_t i = 0; path && i < line.size() && first == std::string_view::npos; i++)
				if ((line[i] == '/' || line[i] == '\\') && i + 1 < line.size() && !IsSpace(line[i + 1]))
					first = i;
			if (first == std::string_view::npos)
				return ScrubTokens(line);
			// back to where its word starts: "smb:", "usb0", a drive's letter
			size_t start = first;
			while (start > 0 && !IsSpace(line[start - 1]) && line[start - 1] != '"' && line[start - 1] != '\'' && line[start - 1] != '(' && line[start - 1] != '[' &&
				   line[start - 1] != '=')
				start--;
			const std::string_view tail = line.substr(last + 1);
			// "scheme://host" has no file after its last slash, only a machine's name
			const bool hostOnly = last >= 1 && line[last - 1] == '/' && last >= 2 && line[last - 2] == ':';
			std::string out = ScrubTokens(line.substr(0, start));
			out += "<path>";
			if (!hostOnly && HasExtension(tail))
			{
				out += '/';
				out += ScrubTokens(tail);
			}
			return out;
		}

		// -- the files -----------------------------------------------------------------------------

		std::mutex s_mutex;
		std::string s_folder;			   // under s_mutex
		bool s_enabled = false;			   // under s_mutex
		std::vector<std::string> s_secrets; // under s_mutex
		Console s_console;					// under s_mutex

		std::string CurrentPath() { return s_folder + "/current.json"; }
		std::string IdPath() { return s_folder + "/install-id"; }
		std::string Outbox() { return s_folder + "/outbox"; }

		std::string ReadFile(const std::string& path, size_t most = 256 * 1024)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file)
				return {};
			std::string text;
			char block[4096];
			while (file && text.size() < most)
			{
				file.read(block, sizeof(block));
				text.append(block, (size_t)file.gcount());
			}
			return text;
		}

		// written beside, then moved into place: a report is whole or not there
		bool WriteFile(const std::string& path, const std::string& text)
		{
			std::error_code ec;
			fs::create_directories(fs::path(path).parent_path(), ec);
			const std::string beside = path + ".part";
			{
				std::ofstream file(beside, std::ios::binary | std::ios::trunc);
				file.write(text.data(), (std::streamsize)text.size());
				if (!file)
					return false;
			}
			fs::rename(beside, path, ec);
			if (ec)
				fs::remove(beside, ec);
			return !ec;
		}

		std::string RandomHex(int digits)
		{
			std::random_device device;
			std::mt19937_64 mix(((uint64_t)device() << 32) ^ device() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
			std::string text;
			for (int i = 0; i < digits; i++)
				text += "0123456789abcdef"[(device() ^ mix()) & 15];
			return text;
		}

		bool IsId(const std::string& id)
		{
			return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) { return IsDigit(c) || (c >= 'a' && c <= 'f'); });
		}

		std::string LoadId()
		{
			std::string id = ReadFile(IdPath(), 64);
			while (!id.empty() && IsSpace(id.back()))
				id.pop_back();
			return IsId(id) ? id : std::string();
		}

		std::string ToJson(const rapidjson::Document& json)
		{
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			json.Accept(writer);
			return std::string(buffer.GetString(), buffer.GetSize());
		}

		void Set(rapidjson::Document& json, const char* key, const std::string& value)
		{
			json.RemoveMember(key);
			json.AddMember(rapidjson::Value(key, json.GetAllocator()), rapidjson::Value(value.c_str(), (rapidjson::SizeType)value.size(), json.GetAllocator()),
				json.GetAllocator());
		}

		// cut at a character's start, not inside one
		std::string Cut(std::string text, size_t most)
		{
			if (text.size() <= most)
				return text;
			while (most > 0 && ((unsigned char)text[most] & 0xc0) == 0x80)
				most--;
			text.resize(most);
			return text;
		}

		// the boot log's last lines, scrubbed
		std::string LogTail(const std::string& path, const std::vector<std::string>& secrets)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file)
				return {};
			file.seekg(0, std::ios::end);
			const std::streamoff size = file.tellg();
			const std::streamoff from = std::max<std::streamoff>(0, size - (std::streamoff)kLogBytes * 2);
			file.seekg(from);
			std::string text((size_t)(size - from), '\0');
			file.read(text.data(), (std::streamsize)text.size());
			text.resize((size_t)file.gcount());
			// whole lines only: the first one read may start anywhere
			if (from > 0)
				text.erase(0, std::min(text.size(), text.find('\n') + 1));
			size_t lines = 0, at = text.size();
			while (at > 0 && lines < kLogLines)
			{
				const size_t before = text.rfind('\n', at - 1);
				if (before == std::string::npos)
				{
					at = 0;
					break;
				}
				lines++;
				at = before;
			}
			if (at > 0)
				text.erase(0, at + 1);
			std::string scrubbed = Scrub(text, secrets);
			if (scrubbed.size() > kLogBytes)
			{
				scrubbed.erase(0, scrubbed.size() - kLogBytes);
				scrubbed.erase(0, std::min(scrubbed.size(), scrubbed.find('\n') + 1));
			}
			return scrubbed;
		}

		// the outbox's files, by name: each starts with its session's time, twelve digits
		std::vector<std::string> List()
		{
			std::vector<std::string> names;
			std::error_code ec;
			for (fs::directory_iterator it(Outbox(), ec), end; !ec && it != end; it.increment(ec))
			{
				const std::string name = it->path().filename().string();
				if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0)
					names.push_back(name);
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		void Trim(int64_t now)
		{
			std::vector<std::string> names = List();
			std::error_code ec;
			uint64_t bytes = 0;
			std::vector<uint64_t> sizes;
			for (const std::string& name : names)
			{
				const uint64_t size = fs::file_size(Outbox() + "/" + name, ec);
				sizes.push_back(ec ? 0 : size);
				bytes += sizes.back();
			}
			size_t drop = 0;
			while (drop < names.size() &&
				   (names.size() - drop > (size_t)kOutboxFiles || bytes > kOutboxBytes || now - std::strtoll(names[drop].c_str(), nullptr, 10) > kOutboxSeconds))
			{
				bytes -= sizes[drop];
				fs::remove(Outbox() + "/" + names[drop], ec);
				drop++;
			}
		}

		void Forget()
		{
			std::error_code ec;
			fs::remove(CurrentPath(), ec);
			fs::remove(IdPath(), ec);
			fs::remove_all(Outbox(), ec);
		}
	}

	std::string Scrub(std::string_view text, const std::vector<std::string>& secrets)
	{
		const std::string known = RemoveSecrets(text, secrets);
		std::string out;
		out.reserve(known.size());
		size_t at = 0;
		while (at <= known.size())
		{
			size_t end = known.find('\n', at);
			if (end == std::string::npos)
				end = known.size();
			std::string_view line(known.data() + at, end - at);
			const bool carriage = !line.empty() && line.back() == '\r';
			if (carriage)
				line.remove_suffix(1);
			out += ScrubLine(line);
			if (end < known.size())
				out += '\n';
			at = end + 1;
		}
		return out;
	}

	std::string ModelClass(std::string_view model)
	{
		const size_t at = model.find("CFI-");
		if (at == std::string_view::npos || at + 4 >= model.size())
			return "unknown";
		switch (model[at + 4])
		{
		case '1': return "base";
		case '2': return "slim";
		case '7': return "pro";
		default: return "unknown";
		}
	}

	std::string FirmwareShort(std::string_view firmware)
	{
		size_t i = 0;
		while (i < firmware.size() && !IsDigit(firmware[i]))
			i++;
		size_t end = i;
		while (end < firmware.size() && IsDigit(firmware[end]))
			end++;
		if (end == i || end >= firmware.size() || firmware[end] != '.')
			return "unknown";
		size_t minor = end + 1;
		while (minor < firmware.size() && IsDigit(firmware[minor]) && minor - end - 1 < 2)
			minor++;
		if (minor == end + 1)
			return "unknown";
		while (i + 1 < end && firmware[i] == '0')
			i++;
		return std::string(firmware.substr(i, minor - i));
	}

	void SetFolder(const std::string& folder)
	{
		std::lock_guard lock(s_mutex);
		s_folder = folder;
	}

	void SetSecrets(std::vector<std::string> secrets)
	{
		std::lock_guard lock(s_mutex);
		// the longest first, so a name inside another is not left half there
		std::sort(secrets.begin(), secrets.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
		s_secrets = std::move(secrets);
	}

	void SetEnabled(bool on)
	{
		std::lock_guard lock(s_mutex);
		s_enabled = on;
		if (s_folder.empty())
			return;
		if (!on)
			Forget();
		else if (LoadId().empty())
			WriteFile(IdPath(), RandomHex(32) + "\n");
	}

	bool Enabled()
	{
		std::lock_guard lock(s_mutex);
		return s_enabled;
	}

	std::string InstallId()
	{
		std::lock_guard lock(s_mutex);
		return s_enabled && !s_folder.empty() ? LoadId() : std::string();
	}

	void SetConsole(const Console& console)
	{
		std::lock_guard lock(s_mutex);
		s_console = console;
	}

	void Begin(const Session& session)
	{
		std::lock_guard lock(s_mutex);
		const Console& console = s_console;
		if (!s_enabled || s_folder.empty())
			return;
		const std::string id = LoadId();
		if (id.empty())
			return;
		rapidjson::Document json(rapidjson::kObjectType);
		auto& allocator = json.GetAllocator();
		json.AddMember("schema", kSchema, allocator);
		Set(json, "install", id);
		Set(json, "app", Cut(console.app, 24));
		Set(json, "commit", Cut(console.commit, 16));
		Set(json, "side", Cut(session.side, 24));
		Set(json, "emulator", Cut(session.emulator, 32));
		char title[20];
		std::snprintf(title, sizeof(title), "%016llx", (unsigned long long)session.titleId);
		Set(json, "titleId", title);
		// the catalogue's name: the patterns go, but not the users' names, which are often a game's too
		Set(json, "name", Cut(Scrub(session.name), kNameLength));
		json.AddMember("started", (int64_t)session.started, allocator);
		Set(json, "firmware", FirmwareShort(console.firmware));
		Set(json, "model", console.model == "base" || console.model == "slim" || console.model == "pro" ? console.model : "unknown");
		json.AddMember("jit", console.jit, allocator);
		json.AddMember("hen", console.hen, allocator);
		rapidjson::Value settings(rapidjson::kObjectType);
		for (const auto& [name, value] : session.settings)
			settings.AddMember(rapidjson::Value(name.c_str(), allocator), rapidjson::Value(value), allocator);
		json.AddMember("settings", settings, allocator);
		WriteFile(CurrentPath(), ToJson(json));
	}

	void Ended(int64_t when)
	{
		std::lock_guard lock(s_mutex);
		if (!s_enabled || s_folder.empty())
			return;
		rapidjson::Document json;
		const std::string text = ReadFile(CurrentPath());
		if (text.empty() || json.Parse(text.c_str(), text.size()).HasParseError() || !json.IsObject())
			return;
		if (!json.HasMember("started") || !json["started"].IsInt64())
			return;
		const int64_t seconds = when - json["started"].GetInt64();
		json.RemoveMember("ended");
		json.RemoveMember("seconds");
		json.AddMember("ended", true, json.GetAllocator());
		// a day and more is a clock that changed, as the catalogue has it
		if (seconds >= 0 && seconds < 24 * 3600)
			json.AddMember("seconds", (int64_t)seconds, json.GetAllocator());
		WriteFile(CurrentPath(), ToJson(json));
	}

	bool Close(const Outcome& outcome, int64_t now)
	{
		std::lock_guard lock(s_mutex);
		if (s_folder.empty())
			return false;
		std::error_code ec;
		const std::string text = ReadFile(CurrentPath());
		if (text.empty())
			return false;
		fs::remove(CurrentPath(), ec);
		if (!s_enabled)
			return false;
		rapidjson::Document json;
		const std::string id = LoadId();
		if (json.Parse(text.c_str(), text.size()).HasParseError() || !json.IsObject() || !json.HasMember("install") || !json["install"].IsString() ||
			id.empty() || id != json["install"].GetString() || !json.HasMember("started") || !json["started"].IsInt64())
			return false; // noted under an answer since taken back
		const bool ended = json.HasMember("ended") && json["ended"].IsBool() && json["ended"].GetBool();
		json.RemoveMember("ended");
		// a game left from its menu ended, whatever came after it (the launcher's own crash, a restart's
		// error); otherwise its crash, why it did not start, or nothing: the app or the console was closed
		const std::string& why = !outcome.crash.empty() ? outcome.crash : outcome.error;
		const char* result = ended ? "ended" : !outcome.crash.empty() ? "crashed" : !outcome.error.empty() ? "failed" : "closed";
		Set(json, "result", result);
		if (!ended)
			json.RemoveMember("seconds");
		if (!ended && !why.empty())
			Set(json, "error", Cut(Scrub(why, s_secrets), kErrorLength));
		if (!outcome.logPath.empty())
			Set(json, "log", LogTail(outcome.logPath, s_secrets));
		char name[64];
		std::snprintf(name, sizeof(name), "%012lld-%s.json", (long long)json["started"].GetInt64(), RandomHex(8).c_str());
		const bool written = WriteFile(Outbox() + "/" + name, ToJson(json));
		Trim(now);
		return written;
	}

	std::vector<std::string> Pending(int64_t now)
	{
		std::lock_guard lock(s_mutex);
		std::vector<std::string> paths;
		if (!s_enabled || s_folder.empty())
			return paths;
		Trim(now);
		for (const std::string& name : List())
			paths.push_back(Outbox() + "/" + name);
		return paths;
	}

	std::string Read(const std::string& path)
	{
		std::lock_guard lock(s_mutex);
		return s_enabled ? ReadFile(path) : std::string();
	}

	void Remove(const std::string& path)
	{
		std::lock_guard lock(s_mutex);
		std::error_code ec;
		fs::remove(path, ec);
	}
}
