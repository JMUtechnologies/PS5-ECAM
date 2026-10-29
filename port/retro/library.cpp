// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"
#include "../app/paths.h"
#include "../app/own_status.h"
#if defined(__PROSPERO__)
#include "../ps5/log.h"
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <fstream>
#include <cstdio>
#include <rapidjson/document.h>
#include <memory>
#include <map>
#include <set>
#include <thread>
namespace ps5retro
{
	namespace fs = std::filesystem;
	namespace
	{
		std::string Extension(const fs::path& path)
		{
			auto ext = path.extension().string();
			for (char& c : ext) c = char(std::tolower((unsigned char)c));
			return ext;
		}

		bool DiscMatches(std::string_view side, const fs::path& path)
		{
			if (side != "gamecube" && side != "wii") return true;
			const auto ext = Extension(path);
			// Compressed containers need the native disc reader. Inspect only raw ISO/GCM
			// headers here, without unpacking, renaming or copying the user's discs.
			if (ext != ".iso" && ext != ".gcm") return true;
			unsigned char header[0x20]{};
			std::ifstream in(path, std::ios::binary);
			if (!in.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
			const bool wii = header[0x18] == 0x5d && header[0x19] == 0x1c && header[0x1a] == 0x9e && header[0x1b] == 0xa3;
			const bool gc = header[0x1c] == 0xc2 && header[0x1d] == 0x33 && header[0x1e] == 0x9f && header[0x1f] == 0x3d;
			return side == "wii" ? wii : gc && !wii;
		}

		bool ProtectedFolder(std::string name)
		{
			for (char& c : name) c = char(std::tolower((unsigned char)c));
			if (name.empty() || name.front() == '.') return true;
			for (const char* excluded : {"saves", "save", "states", "keys", "firmware", "updates", "dlc", "nand", "sdmc",
				"user", "homebrew", "sce_sys", "sce_module", "system", "system_ex", "system_data", "cache", "config", "settings", "bios"})
				if (name == excluded) return true;
			return false;
		}

		struct SearchFolder { std::string path; int depth; bool shared; };
		std::string PathKey(const fs::path& path)
		{
			// Native title realpath implementations can return the same successful
			// result for different files. Paths here come from bounded, no-link walks.
			std::error_code ec;
			const auto absolute = fs::absolute(path, ec);
			return (ec ? path : absolute).lexically_normal().string();
		}
		std::string UniqueKey(const fs::path& path)
		{
			struct stat info{};
			if (::stat(path.c_str(), &info) == 0 && info.st_ino != 0)
				return "inode:" + std::to_string((uint64_t)info.st_dev) + ":" + std::to_string((uint64_t)info.st_ino);
			return PathKey(path);
		}
		std::vector<SearchFolder> SearchFolders(int side, const std::string& configured, const std::vector<std::string>& storage)
		{
			std::vector<SearchFolder> result{{configured, 4, false}};
			std::set<std::string> seen;
			seen.insert(UniqueKey(configured));
			auto add = [&](const fs::path& base, const fs::path& relative, int depth, bool shared) {
				if (result.size() >= 256) return;
				const auto candidate = base / relative;
				std::error_code error;
				fs::path component = base;
				for (const auto& part : relative)
				{
					component /= part;
					if (fs::is_symlink(ps5files::OwnStatus(component, error))) return;
					error.clear();
				}
				if (!fs::is_directory(ps5files::OwnStatus(candidate, error)) || error) return;
				const fs::path root(PathKey(base)), path(PathKey(candidate));
				const auto within = path.lexically_relative(root);
				if (within.empty() || *within.begin() == "..") return;
				if (seen.insert(UniqueKey(candidate)).second) result.push_back({candidate.lexically_normal().string(), depth, shared});
			};
			const auto& system = Of(side);
			std::vector<std::string> aliases{system.key, system.name};
			if (side == 4) aliases.insert(aliases.end(), {"GameCube", "GC", "gc"});
			if (side == 5) aliases.push_back("Wii");
			if (side == 6) aliases.insert(aliases.end(), {"N64", "Nintendo64"});
			if (side == 7) aliases.insert(aliases.end(), {"NES", "Famicom"});
			if (side == 8) aliases.insert(aliases.end(), {"GB", "GameBoy"});
			if (side == 9) aliases.insert(aliases.end(), {"GBC", "GameBoyColor"});
			if (side == 10) aliases.insert(aliases.end(), {"GBA", "GameBoyAdvance"});
			for (const auto& drive : storage)
			{
				const fs::path base(drive);
				if (!base.is_absolute()) continue;
				// Native library locations, including Porpoise's existing mixed GC/Wii folder.
				const auto native = fs::path(system.data).filename();
				for (const char* container : {"", "PS5CEMU-HAR-data"})
					for (const char* games : {"games", "roms"}) add(base, fs::path(container) / native / games, 4, false);
				for (const char* games : {"games", "roms"})
				{
					add(base, fs::path("homebrew") / native / games, 4, false);
					const char* title = side == 6 ? "PPSA99206" : side == 7 ? "PPSA99204" : side == 8 || side == 9 ? "PPSA99210" : side == 10 ? "PPSA99209" : "";
					if (*title) add(base, fs::path("homebrew") / title / games, 4, false);
				}
				add(base, native, 0, false); // standalone shelves also allow loose games at their data root
				if (side == 6) { add(base, "mupen64plus/roms", 4, false); add(base, "mupen64plus/games", 4, false); }
				if (side == 7) { add(base, "fceumm/roms", 4, false); add(base, "fceumm/games", 4, false); }
				// Porpoise saves custom game folders as repeated folder= values. Mupen's
				// browser remembers its last folder. Read bounded copies, never rewrite them.
				const auto ini = base / (side == 4 || side == 5 ? "porpoise/settings.ini" : side == 6 ? "mupen64plus/mupen64plus-ps5.ini" : "");
				std::error_code configError;
				if ((side == 4 || side == 5 || side == 6) && fs::is_regular_file(ps5files::OwnStatus(ini, configError)) &&
					!configError && fs::file_size(ini, configError) <= 65536 && !configError)
				{
					std::ifstream in(ini);
					auto trim = [](std::string text) {
						const auto start = text.find_first_not_of(" \t\r");
						return start == std::string::npos ? std::string() : text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
					};
					for (std::string line; std::getline(in, line);)
					{
						const auto eq = line.find('='); if (eq == std::string::npos) continue;
						if (trim(line.substr(0, eq)) != (side == 6 ? "last_dir" : "folder")) continue;
						const auto value = trim(line.substr(eq + 1));
						if (value.empty() || value.size() > 4096 || !fs::path(value).is_absolute() || value.find('\0') != std::string::npos) continue;
						const auto normalized = fs::path(value).lexically_normal();
						bool blocked = false;
						for (const auto& part : normalized) if (part != normalized.root_path() && ProtectedFolder(part.string())) blocked = true;
						if (blocked) continue;
						for (const auto& allowed : storage)
						{
							const auto rel = normalized.lexically_relative(fs::path(allowed).lexically_normal());
							if (!rel.empty() && rel != "." && *rel.begin() != "..") add(allowed, rel, 4, false);
						}
					}
				}
				for (const auto& alias : aliases)
					for (const char* parent : {"", "roms", "ROMs", "Roms", "ROMS", "games", "Games"})
						add(base, fs::path(parent) / alias, 4, false);
			}
			// Loose games at drive/roms/games roots are safe to identify; do not recursively
			// crawl the drive or guess the system of ZIP/7z files in a shared folder.
			for (const auto& drive : storage)
			{
				if (!fs::path(drive).is_absolute()) continue;
				for (const char* parent : {"", "roms", "ROMs", "Roms", "ROMS", "games", "Games"}) add(drive, parent, 0, true);
			}
			return result;
		}
	}

	bool HasExtension(std::string_view side, std::string name)
	{
		for (auto& c : name) c = char(std::tolower((unsigned char)c));
		const auto dot = name.find_last_of('.');
		if (dot == std::string::npos) return false;
		const auto ext = name.substr(dot);
		if (side == "gb") return ext == ".gb";
		if (side == "gbc") return ext == ".gbc";
		if (side == "gba") return ext == ".gba" || ext == ".zip" || ext == ".7z";
		if (side == "nes") return ext == ".nes" || ext == ".unf" || ext == ".unif" || ext == ".fds" || ext == ".zip" || ext == ".7z";
		if (side == "n64") return ext == ".z64" || ext == ".n64" || ext == ".v64" || ext == ".zip" || ext == ".rom";
		if (side == "gamecube" || side == "wii")
			return ext == ".iso" || ext == ".gcm" || ext == ".rvz" || ext == ".ciso" || ext == ".gcz" || ext == ".wia" || (side == "wii" && (ext == ".wbfs" || ext == ".wad" || ext == ".dol" || ext == ".elf"));
		return false;
	}

	namespace
	{
		// Share immutable parsed sidecars between scanner and artwork lookups. Avoid rereading one
		// multi-megabyte index for every game; changed size/mtime invalidates it at the next lookup.
		std::shared_ptr<const rapidjson::Document> Metadata(const fs::path& path)
		{
			struct Cached { uintmax_t size; fs::file_time_type changed; std::shared_ptr<rapidjson::Document> doc; };
			static std::mutex mutex; static std::map<std::string, Cached> cache;
			std::error_code ec;
			if (!fs::is_regular_file(ps5files::OwnStatus(path, ec)) || ec) return {};
			const auto size = fs::file_size(path, ec); if (ec || size > 8 * 1024 * 1024) return {};
			const auto changed = fs::last_write_time(path, ec); if (ec) return {};
			std::lock_guard lock(mutex); const auto key = path.string(); const auto found = cache.find(key);
			if (found != cache.end() && found->second.size == size && found->second.changed == changed) return found->second.doc;
			std::ifstream stream(path, std::ios::binary); std::string text(size_t(size), '\0');
			stream.read(text.data(), std::streamsize(text.size())); if (stream.gcount() != std::streamsize(size)) return {};
			auto doc = std::make_shared<rapidjson::Document>(); doc->Parse(text.data(), text.size());
			if (doc->HasParseError() || !doc->IsObject() || !doc->HasMember("games") || !(*doc)["games"].IsObject()) return {};
			if (cache.size() >= 4 && found == cache.end()) cache.erase(cache.begin());
			cache.insert_or_assign(key, Cached{size, changed, doc}); return doc;
		}
	}
	std::string CoverPath(int side, const ps5emu::Game& game)
	{
		if (!IsRetro(side)) return {};
		// Look through ancestor sidecars up to the scanner's depth. Artwork is restricted to that root.
		for (auto root = game.path.parent_path(); !root.empty(); root = root.parent_path())
		{
			const auto relativeRom = game.path.lexically_relative(root);
			if (std::distance(relativeRom.begin(), relativeRom.end()) > 5) break;
			std::error_code ec; const auto metadata = Metadata(root / ".ecam-library.json");
			if (metadata)
			{
				const auto& doc = *metadata;
				const auto key = game.path.lexically_relative(root).generic_string();
				if (doc["games"].HasMember(key.c_str()))
				{
					const auto& entry = doc["games"][key.c_str()];
					if (entry.IsObject() && entry.HasMember("image") && entry["image"].IsString())
					{
						const auto image = fs::weakly_canonical(root / entry["image"].GetString(), ec);
						const auto relative = image.lexically_relative(fs::weakly_canonical(root, ec)).generic_string();
						const auto ext = image.extension().string();
						if (!ec && !relative.empty() && relative != ".." && relative.rfind("../", 0) != 0 && (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") && fs::is_regular_file(image, ec) && !ec && fs::file_size(image, ec) <= 16 * 1024 * 1024 && !ec) return image.string();
					}
				}
			}
			if (root == root.parent_path()) break;
		}
		const auto stem = game.path.stem().string();
		std::vector<fs::path> folders{game.path.parent_path(), game.path.parent_path() / "covers"};
		for (int backend = 0; backend < 2; backend++)
		{
			const fs::path data = BackendFor(side, backend).data;
			folders.push_back(data / "covers"); folders.push_back(data / "cache");
			if (side == 7) folders.push_back(data / "covers" / ((game.path.extension() == ".fds" || game.path.extension() == ".FDS") ? "FDS" : "NES"));
		}
		std::vector<std::string> names{stem, game.name};
		if (side >= 6)
		{
			uint32_t hash = 2166136261u; for (unsigned char c : game.path.string()) { hash ^= c; hash *= 16777619u; }
			char serial[20]; std::snprintf(serial, sizeof(serial), "%s-%08X", side == 6 ? "N64" : side == 7 ? "NES" : side == 10 ? "GBA" : "GBC", hash); names.push_back(serial);
		}
		for (const auto& folder : folders)
			for (const auto& name : names)
			{
				if (name.empty() || name.find_first_of("/\\") != std::string::npos || name == "." || name == "..") continue;
				for (const char* ext : {".png", ".jpg", ".jpeg", ".tga"})
				{
					const auto file = folder / (name + ext); std::error_code ec;
					if (fs::is_regular_file(ps5files::OwnStatus(file, ec)) && !ec && fs::file_size(file, ec) <= 16 * 1024 * 1024 && !ec) return file.string();
				}
			}
		return {};
	}
	bool ValidateRom(int side, const std::string& path, std::string& error, int backend)
	{
		if (!IsRetro(side) || !HasExtension(Of(side).key, path)) { error = "Unsupported game format."; return false; }
		std::error_code ec;
		const auto size = fs::file_size(path, ec);
		if (ec || size == 0) { error = "The game file is empty or cannot be read."; return false; }
		if (side < 6)
		{
			if (!DiscMatches(Of(side).key, path)) { error = "The disc header is unreadable or does not match the selected system."; return false; }
			error.clear(); return true;
		}
		std::string ext = fs::path(path).extension().string();
		for (char& c : ext) c = char(std::tolower((unsigned char)c));
		if (side >= 8)
		{
			const uint64_t maximum = (ext == ".zip" || ext == ".7z") ? 64ull * 1024 * 1024 : 32ull * 1024 * 1024;
			if (size > maximum) { error = "The handheld game exceeds this runtime's file-size limit."; return false; }
			if (ext == ".gb" || ext == ".gbc" || ext == ".gba")
			{
				const uint64_t minimum = side == 10 ? 0xc0 : 0x150;
				if (size < minimum) { error = "The handheld cartridge header is truncated."; return false; }
				std::ifstream in(path, std::ios::binary); if (!in) { error = "The handheld game cannot be read."; return false; }
				error.clear(); return true; // The native mGBA loader validates complete cartridge contents.
			}
		}
		if (side < 8 && backend == 0 && (ext == ".7z" || ext == ".rom")) { error = "This format requires the optional alternative runtime. Select it under Emulator runtime."; return false; }
		const uint64_t limit = side == 6 ? (backend == 1 ? 252ull : 64ull) * 1024 * 1024 : 64ull * 1024 * 1024;
		if (size > limit && ext != ".zip" && ext != ".7z") { error = "The ROM exceeds this backend's supported size limit."; return false; }
		unsigned char h[16]{}; std::ifstream in(path, std::ios::binary); in.read(reinterpret_cast<char*>(h), sizeof(h));
		if (ext == ".zip")
		{
			if (size < 22 || size > 512ull * 1024 * 1024 || h[0] != 'P' || h[1] != 'K' || h[2] != 3 || h[3] != 4) { error = "Invalid or oversized ZIP archive."; return false; }
			// The native loader selects members, limits decompression and validates the contents.
		}
		else if (ext == ".7z")
		{
			if (size < 32 || size > 512ull * 1024 * 1024 || h[0] != '7' || h[1] != 'z' || h[2] != 0xbc || h[3] != 0xaf || h[4] != 0x27 || h[5] != 0x1c) { error = "Invalid or oversized 7-Zip archive."; return false; }
		}
		else if (side == 6)
		{
			const bool magic = (h[0] == 0x80 && h[1] == 0x37 && h[2] == 0x12 && h[3] == 0x40) ||
				(h[0] == 0x37 && h[1] == 0x80 && h[2] == 0x40 && h[3] == 0x12) || (h[0] == 0x40 && h[1] == 0x12 && h[2] == 0x37 && h[3] == 0x80);
			if (size < 64 || !magic) { error = "The N64 cartridge header is invalid or truncated."; return false; }
		}
		else if (ext == ".nes" && (size < 16 || h[0] != 'N' || h[1] != 'E' || h[2] != 'S' || h[3] != 0x1a))
		{ error = "The NES cartridge header is invalid or truncated."; return false; }
		else if ((ext == ".unf" || ext == ".unif") && (size < 32 || std::string(reinterpret_cast<char*>(h), 4) != "UNIF"))
		{ error = "The UNIF cartridge header is invalid or truncated."; return false; }
		else if (ext == ".fds")
		{
			const fs::path data = BackendFor(side, backend).data;
			const fs::path bios = backend == 1 ? data / "bios/disksys.rom" : data / "disksys.rom";
			if (!fs::is_regular_file(bios, ec) || fs::file_size(bios, ec) != 8192 || ec) { error = "Famicom Disk System requires your 8192-byte BIOS at " + bios.string(); return false; }
		}
		error.clear(); return true;
	}
	static std::vector<ps5emu::Game> ScanFolder(const std::string& side, const std::string& folder, std::string& error,
		size_t& inspected, int depth, bool shared, bool discovery)
	{
		std::vector<ps5emu::Game> result;
		if (!Find(side)) { error = "Unknown emulator system."; return result; }
		std::error_code ec;
		if (!fs::is_directory(folder, ec)) { error = "The games folder is not available: " + folder; return result; }
		fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
		const auto before = inspected;
		for (; !ec && it != end; it.increment(ec))
		{
			if (++inspected > 100000) { error = "The scan reached its 100000-entry limit."; break; }
			if (it.depth() >= depth) it.disable_recursion_pending();
			std::error_code statusError;
			const auto status = ps5files::OwnStatus(it->path(), statusError);
			if (discovery && fs::is_directory(status) && ProtectedFolder(it->path().filename().string()))
			{
				it.disable_recursion_pending(); continue;
			}
			if (statusError || !fs::is_regular_file(status) || !HasExtension(side, it->path().filename().string()) || !DiscMatches(side, it->path())) continue;
			if (shared)
			{
				const auto ext = Extension(it->path());
				if (ext == ".zip" || ext == ".7z" || ext == ".gcz" || ext == ".ciso" || ext == ".rvz" || ext == ".wia" || ext == ".elf" || ext == ".dol" || ext == ".wad") continue;
			}
			ps5emu::Game game;
			game.path = it->path(); game.name = game.path.stem().string();
			game.format = game.path.extension().string().substr(1); for (char& c : game.format) c = char(std::toupper((unsigned char)c));
			// Stable identity in a dedicated namespace. Include system to keep GC/Wii lists independent.
			uint64_t hash = 14695981039346656037ull;
			for (unsigned char c : side + ":" + fs::absolute(game.path).lexically_normal().string()) { hash ^= c; hash *= 1099511628211ull; }
			game.titleId = 0x6000000000000000ull | (hash & 0x00ffffffffffffffull);
			result.push_back(std::move(game));
		}
		if (ec) error = "The games folder could not be fully scanned: " + ec.message();
		else if (inspected <= 100000) error.clear();
#if defined(__PROSPERO__)
		ps5log::Line("[retro] {} scan {}: {} entries, {} games; {}", side, folder, inspected - before, result.size(), error);
#else
		(void)before;
#endif
		const auto metadata = Metadata(fs::path(folder) / ".ecam-library.json");
		if (metadata)
		{
			const auto& doc = *metadata;
			if (doc.HasMember("games"))
				for (auto& game : result)
				{
					const auto key = game.path.lexically_relative(folder).generic_string(); const auto entry = doc["games"].FindMember(key.c_str());
					if (entry == doc["games"].MemberEnd() || !entry->value.IsObject()) continue;
					const auto name = entry->value.FindMember("name");
					if (name != entry->value.MemberEnd() && name->value.IsString() && name->value.GetStringLength() > 0 && name->value.GetStringLength() <= 512)
						game.name.assign(name->value.GetString(), name->value.GetStringLength());
				}
		}
		std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
		return result;
	}

	std::vector<ps5emu::Game> Scan(const std::string& side, const std::string& folder, std::string& error)
	{
		size_t inspected = 0;
		return ScanFolder(side, folder, error, inspected, 4, false, false);
	}

	std::vector<std::string> StorageRoots()
	{
		std::vector<std::string> roots{"/data"};
		const auto local = fs::path(ps5paths::Root()).parent_path().string();
		if (!local.empty() && local != "/data") roots.push_back(local);
		for (int i = 0; i < 8; ++i) roots.push_back("/mnt/usb" + std::to_string(i));
		roots.push_back("/mnt/ext0"); roots.push_back("/mnt/ext1");
		std::erase_if(roots, [](const auto& path) { std::error_code ec; return !fs::is_directory(ps5files::OwnStatus(path, ec)); });
		return roots;
	}

	int IdentifySystem(const std::string& path)
	{
		const auto ext = Extension(path);
		if (ext == ".zip" || ext == ".7z" || ext == ".elf") return -1;
		for (int side = 4; side < 11; ++side)
			if (HasExtension(Of(side).key, path) && DiscMatches(Of(side).key, path))
			{
				// Compressed GC/Wii containers cannot be assigned by extension alone.
				if (side < 6 && ext != ".iso" && ext != ".gcm" && ext != ".wbfs" && ext != ".wad" && ext != ".dol") return -1;
				return side;
			}
		return -1;
	}

	std::string StorageSignature(const std::vector<std::string>& storage)
	{
		std::string signature;
		for (const auto& path : storage)
		{
			struct stat info{};
			if (::stat(path.c_str(), &info) != 0) continue;
			signature += path + ":" + std::to_string((uint64_t)info.st_dev) + ":" +
				std::to_string((uint64_t)info.st_ino) + ":" + std::to_string((int64_t)info.st_mtime) + "\n";
		}
		return signature;
	}

	std::vector<ExistingFiles> FindExistingFiles(const std::string& activeData,
		const std::vector<std::string>& nativeNames, const std::vector<std::string>& storage)
	{
		std::vector<fs::path> roots{activeData};
		for (const auto& drive : storage)
			for (const auto& name : nativeNames)
			{
				if (name.empty() || !fs::path(drive).is_absolute() || fs::path(name).is_absolute()) continue;
				const auto safe = fs::path(name).lexically_normal();
				if (*safe.begin() == "..") continue;
				for (const auto& relative : {safe, fs::path("PS5CEMU-HAR-data") / safe})
				{
					fs::path path(drive); bool linked = false;
					for (const auto& part : relative)
					{
						path /= part; std::error_code ec;
						linked |= fs::is_symlink(ps5files::OwnStatus(path, ec));
					}
					if (!linked) roots.push_back(path);
				}
			}
		std::vector<ExistingFiles> result;
		std::set<std::string> seen;
		std::error_code ec;
		const auto active = UniqueKey(activeData);
		size_t visited = 0;
		for (const auto& root : roots)
		{
			if (fs::is_symlink(ps5files::OwnStatus(root, ec))) continue;
			const auto data = UniqueKey(root);
			for (const char* category : {"updates", "dlc", "firmware", "bios", "cheats"})
			{
				const auto folder = root / category;
				if (!fs::is_directory(ps5files::OwnStatus(folder, ec)) || ec) continue;
				const auto key = UniqueKey(folder); if (!seen.insert(key).second) continue;
				ExistingFiles found{category, folder.string(), 0, data == active};
				fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
				for (; !ec && it != end && visited < 10000; it.increment(ec))
				{
					++visited;
					if (it.depth() >= 3) it.disable_recursion_pending();
					std::error_code statusError;
					if (fs::is_regular_file(ps5files::OwnStatus(it->path(), statusError)) && !statusError) ++found.files;
				}
				found.limited = bool(ec) || (visited >= 10000 && it != end);
				if (found.files || found.limited) result.push_back(std::move(found));
			}
			// The original NES backend uses the FDS BIOS at the data root.
			const auto fds = root / "disksys.rom";
			if (fs::is_regular_file(ps5files::OwnStatus(fds, ec)) && !ec)
			{
				const auto key = UniqueKey(fds);
				if (seen.insert(key).second) result.push_back({"bios", fds.string(), 1, data == active});
			}
		}
		return result;
	}

	std::vector<ps5emu::Game> ScanLibraries(int side, const std::string& configured,
		const std::vector<std::string>& storage, std::string& error)
	{
		std::vector<ps5emu::Game> result;
		if (!IsRetro(side)) { error = "Unknown emulator system."; return result; }
		std::set<std::string> seen;
		size_t inspected = 0; bool readable = false;
		std::string firstError;
		const auto folders = SearchFolders(side, configured, storage);
		for (const auto& source : folders)
		{
			std::string why;
			auto games = ScanFolder(Of(side).key, source.path, why, inspected, source.depth, source.shared, true);
			readable |= why.empty();
			if (!why.empty() && firstError.empty()) firstError = why;
			for (auto& game : games)
			{
				if (seen.insert(UniqueKey(game.path)).second) result.push_back(std::move(game));
			}
			if (inspected > 100000) { error = "The scan reached its 100000-entry limit."; break; }
		}
		if (inspected <= 100000) error = readable ? std::string() : firstError;
		if (folders.size() >= 256 && error.empty()) error = "The scan reached its 256-folder discovery limit.";
#if defined(__PROSPERO__)
		ps5log::Line("[retro] {} combined library: {} folders, {} games; {}", Of(side).key, folders.size(), result.size(), error);
#endif
		std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
		return result;
	}
	namespace
	{
		struct State
		{
			std::thread worker; std::atomic_bool scanning{false}; std::mutex mutex;
			std::vector<ps5emu::Game> games; std::string error, nextFolder;
			bool pending = false, nextDiscover = true;
			~State() { if (worker.joinable()) worker.join(); }
		};
		State states[7];
		State& StateFor(int side) { return states[size_t(side - 4)]; }
	}
	void StartScan(int side, const std::string& folder, bool discover)
	{
		if (!IsRetro(side)) return;
		auto& state = StateFor(side);
		{
			std::lock_guard lock(state.mutex);
			if (state.scanning) { state.nextFolder = folder; state.nextDiscover = discover; state.pending = true; return; }
		}
		if (state.worker.joinable()) state.worker.join();
		state.scanning = true;
		try { state.worker = std::thread([&state, side, folder, discover] {
			std::string current = folder; bool automatic = discover;
			for (;;)
			{
				std::string error; std::vector<ps5emu::Game> games;
				try { games = automatic ? ScanLibraries(side, current, StorageRoots(), error) : Scan(Of(side).key, current, error); } catch (const std::exception& e) { error = e.what(); }
				std::lock_guard lock(state.mutex);
				if (state.pending)
				{
					current = std::move(state.nextFolder); automatic = state.nextDiscover; state.pending = false;
					continue; // a folder or drive changed while scanning: publish only the newest request
				}
				state.games = std::move(games); state.error = std::move(error); state.scanning = false; return;
			}
		}); } catch (...) { state.scanning = false; throw; }
	}
	bool Scanning(int side) { return IsRetro(side) && StateFor(side).scanning; }
	std::vector<ps5emu::Game> ListGames(int side) { if (!IsRetro(side)) return {}; auto& s = StateFor(side); std::lock_guard lock(s.mutex); return s.games; }
	std::string ScanError(int side) { if (!IsRetro(side)) return {}; auto& s = StateFor(side); std::lock_guard lock(s.mutex); return s.error; }
	void FinishScans() { for (auto& s : states) if (s.worker.joinable()) s.worker.join(); }
}
