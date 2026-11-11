// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"
#include "../app/lang.h"
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
#include <thread>
// Wii banner extraction
#include "porpoise_banner.h"
namespace ps5retro
{
	namespace fs = std::filesystem;
	using ps5lang::Tr;
	using ps5lang::TrF;
	namespace
	{
		bool Disc(int side) { return Is(side, "gamecube") || Is(side, "wii"); }
		// the cartridge's name in the N64, NES and GB runtimes' own covers: <its name>-<a hash of the path>
		const char* SerialPrefix(int side)
		{
			return Is(side, "n64") || Is(side, "ique") ? "N64" : Is(side, "nes") ? "NES" : Is(side, "gba") ? "GBA" : Is(side, "gb") || Is(side, "gbc") ? "GBC" : nullptr;
		}
	}
	namespace
	{
		std::string Extension(const fs::path& path)
		{
			auto ext = path.extension().string();
			for (char& c : ext) c = char(std::tolower((unsigned char)c));
			return ext;
		}

		namespace
		{
			// Read the disc header (0x100 bytes) from various container formats at their known offsets.
			// Returns true if header was read successfully.
			// the Wii's and the GameCube's magic words end at 0x20: all of the header that is read
			constexpr std::streamsize kDiscMagics = 0x20;
			bool ReadDiscHeader(const fs::path& path, std::string_view ext, unsigned char header[0x100])
			{
				std::ifstream in(path, std::ios::binary);
				if (!in) return false;

				if (ext == ".iso" || ext == ".gcm")
				{
					// Raw ISO/GCM: header at offset 0
					in.seekg(0);
					return in.read(reinterpret_cast<char*>(header), kDiscMagics) && in.gcount() == kDiscMagics;
				}
				else if (ext == ".rvz" || ext == ".wia")
				{
					// RVZ/WIA: disc header stored at file offset 0x58 (0x48 header + 0x10 into h2)
					in.seekg(0x58);
					return in.read(reinterpret_cast<char*>(header), kDiscMagics) && in.gcount() == kDiscMagics;
				}
				else if (ext == ".ciso")
				{
					// CISO: disc header at virtual offset 0x8000, which is file offset 0x8000 (after CISO header)
					in.seekg(0x8000);
					return in.read(reinterpret_cast<char*>(header), kDiscMagics) && in.gcount() == kDiscMagics;
				}
				else if (ext == ".gcz")
				{
					// GCZ: check magic at offset 0, then disc header would be in block 0 (compressed)
					// For simplicity, just verify it's a GCZ file. The disc header requires decompression.
					// We'll read the first 4 bytes to verify magic.
					unsigned char magic[4];
					in.seekg(0);
					if (!in.read(reinterpret_cast<char*>(magic), 4) || in.gcount() != 4) return false;
					// GCZ magic: 0xB10BC001 (little-endian) = 0x01, 0xC0, 0x0B, 0xB1
					if (magic[0] != 0x01 || magic[1] != 0xC0 || magic[2] != 0x0B || magic[3] != 0xB1) return false;
					// Can't easily read disc header without decompression. Fall through to extension-based.
					return false;
				}
				else if (ext == ".wbfs")
				{
					// WBFS: check magic at offset 0
					unsigned char magic[4];
					in.seekg(0);
					if (!in.read(reinterpret_cast<char*>(magic), 4) || in.gcount() != 4) return false;
					if (magic[0] != 'W' || magic[1] != 'B' || magic[2] != 'F' || magic[3] != 'S') return false;
					// Disc header is at virtual offset 0, but requires parsing WBFS header to find first sector.
					// For now, fall through to extension-based.
					return false;
				}
				else if (ext == ".wad")
				{
					// WAD: title ID determines if it's a Wii channel (system menu, WiiWare, VC)
					// Title ID is at offset 0x18C in TMD, which is at variable offset.
					// Too complex to parse here without full WAD parser. Fall through.
					return false;
				}
				return false;
			}

			bool CheckDiscHeader(const unsigned char header[0x100], std::string_view side)
			{
				const bool wii = header[0x18] == 0x5d && header[0x19] == 0x1c && header[0x1a] == 0x9e && header[0x1b] == 0xa3;
				const bool gc = header[0x1c] == 0xc2 && header[0x1d] == 0x33 && header[0x1e] == 0x9f && header[0x1f] == 0x3d;
				return side == "wii" ? wii : gc && !wii;
			}
		}

		bool DiscMatches(std::string_view side, const fs::path& path)
		{
			if (side != "gamecube" && side != "wii") return true;
			const auto ext = Extension(path);
			unsigned char header[0x100]{};
			if (ReadDiscHeader(path, ext, header))
			{
				return CheckDiscHeader(header, side);
			}
			// For formats where we can't easily read the disc header (GCZ, WBFS, WAD),
			// fall back to extension-based filtering as a best effort.
			// RVZ/WIA/CISO are the main compressed formats used; they're handled above.
			// A raw disc too short to hold its header is no disc of either side, as before.
			if (ext == ".iso" || ext == ".gcm" || ext == ".rvz" || ext == ".wia" || ext == ".ciso")
			{
				// If we couldn't read the header, assume it doesn't match (safer than matching both)
				return false;
			}
			// For other formats, allow (conservative: don't hide games we can't identify)
			return true;
		}
	}

	std::string_view Formats(std::string_view side)
	{
		if (side == "gb") return ".gb";
		if (side == "gbc") return ".gbc";
		if (side == "gba") return ".gba .zip .7z";
		if (side == "nes") return ".nes .unf .unif .fds .zip .7z";
		if (side == "snes") return ".sfc .smc .swc .fig .bs .st";
		if (side == "vb") return ".vb .vboy";
		if (side == "pokemini") return ".min";
		if (side == "fds") return ".fds";
		if (side == "sgb") return ".gb .gbc";
		if (side == "gw") return ".mgw";
		if (side == "satellaview") return ".bs";
		if (side == "vs") return ".nes .zip .7z";
		if (side == "ique") return ".z64 .n64 .v64 .zip";
		if (side == "n64") return ".z64 .n64 .v64 .zip .rom";
		if (side == "gamecube") return ".iso .gcm .rvz .ciso .gcz .wia";
		if (side == "wii") return ".iso .gcm .rvz .ciso .gcz .wia .wbfs .wad .dol .elf";
		return "";
	}

	bool HasExtension(std::string_view side, std::string name)
	{
		for (auto& c : name) c = char(std::tolower((unsigned char)c));
		const auto dot = name.find_last_of('.');
		if (dot == std::string::npos) return false;
		const std::string_view ext = std::string_view(name).substr(dot);
		for (std::string_view formats = Formats(side); !formats.empty();)
		{
			const size_t space = formats.find(' ');
			if (formats.substr(0, space) == ext) return true;
			formats = space == std::string_view::npos ? std::string_view() : formats.substr(space + 1);
		}
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

		// For Wii games, try to extract the Wii banner (opening.bnr) from the disc
		// and use it as the launcher image. The banner is the widescreen image used
		// in the Wii Menu disc channel. We cache it as a PNG next to the game file.
		if (Is(side, "wii"))
		{
			const auto cache_dir = game.path.parent_path() / ".cache" / "banners";
			std::error_code ec;
			fs::create_directories(cache_dir, ec);
			const auto banner_file = cache_dir / (game.path.stem().string() + ".png");
			if (fs::is_regular_file(ps5files::OwnStatus(banner_file, ec)) && !ec)
				return banner_file.string();
			std::string error;
			if (ps5porpoise::ExtractWiiBanner(game.path.string(), banner_file.string(), error))
			{
#if defined(__PROSPERO__)
				ps5log::Line("[retro] extracted Wii banner for {} -> {}", game.name, banner_file.string());
#endif
				return banner_file.string();
			}
#if defined(__PROSPERO__)
			ps5log::Line("[retro] Wii banner extraction failed for {}: {}", game.name, error);
#endif
			// Fall through to existing cover art search
		}

		const auto stem = game.path.stem().string();
		std::vector<fs::path> folders{game.path.parent_path(), game.path.parent_path() / "covers"};
		for (int backend = 0; backend < 2; backend++)
		{
			const fs::path data = BackendFor(side, backend).data;
			folders.push_back(data / "covers"); folders.push_back(data / "cache");
			if (Is(side, "nes")) folders.push_back(data / "covers" / ((game.path.extension() == ".fds" || game.path.extension() == ".FDS") ? "FDS" : "NES"));
		}
		std::vector<std::string> names{stem, game.name};
		if (const char* prefix = SerialPrefix(side))
		{
			uint32_t hash = 2166136261u; for (unsigned char c : game.path.string()) { hash ^= c; hash *= 16777619u; }
			char serial[20]; std::snprintf(serial, sizeof(serial), "%s-%08X", prefix, hash); names.push_back(serial);
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
		if (!IsRetro(side) || !HasExtension(Of(side).key, path)) { error = Tr("This system can't start this game."); return false; }
		std::error_code ec;
		const auto size = fs::file_size(path, ec);
		if (ec || size == 0) { error = Tr("The game file is empty or cannot be read."); return false; }
		if (Disc(side))
		{
			if (!DiscMatches(Of(side).key, path)) { error = TrF("This disc is not a {0} game, or it cannot be read.", Of(side).name); return false; }
			error.clear(); return true;
		}
		const char* damaged = Tr("This game's file is damaged or incomplete.");
		const char* tooLarge = Tr("This game is too large for this system's emulator.");
		std::string ext = fs::path(path).extension().string();
		for (char& c : ext) c = char(std::tolower((unsigned char)c));
		// the N64's and the NES's emulators have their limits below (the iQue Player's is the N64's); the
		// others' are a handheld's
		const bool n64 = Is(side, "n64") || Is(side, "ique");
		const bool n64OrNes = n64 || Is(side, "nes");
		if (!n64OrNes)
		{
			const uint64_t maximum = (ext == ".zip" || ext == ".7z") ? 64ull * 1024 * 1024 : 32ull * 1024 * 1024;
			if (size > maximum) { error = tooLarge; return false; }
			if (ext == ".gb" || ext == ".gbc" || ext == ".gba")
			{
				const uint64_t minimum = Is(side, "gba") ? 0xc0 : 0x150;
				if (size < minimum) { error = damaged; return false; }
				std::ifstream in(path, std::ios::binary); if (!in) { error = Tr("The game file is empty or cannot be read."); return false; }
				error.clear(); return true; // The native mGBA loader validates complete cartridge contents.
			}
		}
		if (n64OrNes && backend == 0 && (ext == ".7z" || ext == ".rom")) { error = Tr("This emulator can't read this kind of file. Choose the other emulator in this system's Settings."); return false; }
		const uint64_t limit = n64 ? (backend == 1 ? 252ull : 64ull) * 1024 * 1024 : 64ull * 1024 * 1024;
		if (size > limit && ext != ".zip" && ext != ".7z") { error = tooLarge; return false; }
		unsigned char h[16]{}; std::ifstream in(path, std::ios::binary); in.read(reinterpret_cast<char*>(h), sizeof(h));
		if (ext == ".zip")
		{
			if (size < 22 || size > 512ull * 1024 * 1024 || h[0] != 'P' || h[1] != 'K' || h[2] != 3 || h[3] != 4) { error = TrF("This {0} archive is damaged or too large.", "ZIP"); return false; }
			// The native loader selects members, limits decompression and validates the contents.
		}
		else if (ext == ".7z")
		{
			if (size < 32 || size > 512ull * 1024 * 1024 || h[0] != '7' || h[1] != 'z' || h[2] != 0xbc || h[3] != 0xaf || h[4] != 0x27 || h[5] != 0x1c) { error = TrF("This {0} archive is damaged or too large.", "7-Zip"); return false; }
		}
		else if (n64)
		{
			const bool magic = (h[0] == 0x80 && h[1] == 0x37 && h[2] == 0x12 && h[3] == 0x40) ||
				(h[0] == 0x37 && h[1] == 0x80 && h[2] == 0x40 && h[3] == 0x12) || (h[0] == 0x40 && h[1] == 0x12 && h[2] == 0x37 && h[3] == 0x80);
			if (size < 64 || !magic) { error = damaged; return false; }
		}
		else if (ext == ".nes" && (size < 16 || h[0] != 'N' || h[1] != 'E' || h[2] != 'S' || h[3] != 0x1a))
		{ error = damaged; return false; }
		else if ((ext == ".unf" || ext == ".unif") && (size < 32 || std::string(reinterpret_cast<char*>(h), 4) != "UNIF"))
		{ error = damaged; return false; }
		else if (ext == ".fds")
		{
			// a side on its core finds it in the core's own folder (port/libretro/game.cpp: <data>/system)
			const fs::path data = BackendFor(side, backend).data;
			const fs::path bios = !*BackendFor(side, backend).runtime ? data / "system/disksys.rom" : backend == 1 ? data / "bios/disksys.rom" : data / "disksys.rom";
			if (!fs::is_regular_file(bios, ec) || fs::file_size(bios, ec) != 8192 || ec) { error = TrF("Famicom Disk System games need its BIOS, disksys.rom (8192 bytes), in {0}.", bios.parent_path().string()); return false; }
		}
		else if (ext == ".bs")
		{
			// Mesen-S's Satellaview, in the core's own folder
			const fs::path bios = fs::path(BackendFor(side, backend).data) / "system/BS-X.bin";
			if (!fs::is_regular_file(bios, ec) || ec)
			{
				// tr: {0} is the folder BS-X.bin goes in
				error = TrF("Satellaview games need its BIOS, BS-X.bin, in {0}.", bios.parent_path().string());
				return false;
			}
		}
		error.clear(); return true;
	}
	std::vector<ps5emu::Game> Scan(const std::string& side, const std::string& folder, std::string& error)
	{
		std::vector<ps5emu::Game> result;
		if (!Find(side)) { error = "Unknown emulator system."; return result; }
		std::error_code ec;
		if (!fs::is_directory(folder, ec)) { error = TrF("{0} cannot be read", folder); return result; }
		fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
		size_t inspected = 0;
		for (; !ec && it != end; it.increment(ec))
		{
			if (++inspected > 100000) { error = TrF("Only the first {0} files in {1} were searched. Choose a folder with fewer files.", 100000, folder); break; }
			if (it.depth() >= 4) it.disable_recursion_pending();
			std::error_code statusError;
			const auto status = ps5files::OwnStatus(it->path(), statusError);
			if (statusError || !fs::is_regular_file(status) || !HasExtension(side, it->path().filename().string()) || !DiscMatches(side, it->path())) continue;
			ps5emu::Game game;
			game.path = it->path(); game.name = game.path.stem().string();
			game.format = game.path.extension().string().substr(1); for (char& c : game.format) c = char(std::toupper((unsigned char)c));
			// Stable identity in a dedicated namespace. Include system to keep GC/Wii lists independent.
			uint64_t hash = 14695981039346656037ull;
			for (unsigned char c : side + ":" + fs::absolute(game.path).lexically_normal().string()) { hash ^= c; hash *= 1099511628211ull; }
			game.titleId = 0x6000000000000000ull | (hash & 0x00ffffffffffffffull);
			result.push_back(std::move(game));
		}
		if (ec) error = TrF("Not all of {0} could be read: {1}", folder, ec.message());
		else if (inspected <= 100000) error.clear();
#if defined(__PROSPERO__)
		ps5log::Line("[retro] {} scan {}: {} entries, {} games; {}", side, folder, inspected, result.size(), error);
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

	int IdentifySystem(const std::string& path)
	{
		const auto ext = Extension(path);
		if (ext == ".zip" || ext == ".7z" || ext == ".elf") return -1;
		for (int side = kFirst; side < kSideCount; side++)
			if (HasExtension(Of(side).key, path) && DiscMatches(Of(side).key, path))
			{
				// Compressed GC/Wii containers cannot be assigned by extension alone.
				if (Disc(side) && ext != ".iso" && ext != ".gcm" && ext != ".wbfs" && ext != ".wad" && ext != ".dol") return -1;
				return side;
			}
		return -1;
	}

	namespace
	{
		struct State
		{
			std::thread worker; std::atomic_bool scanning{false}; std::mutex mutex;
			std::vector<ps5emu::Game> games; std::string error, nextFolder;
			bool pending = false; // nextFolder asked for while a scan ran
			~State() { if (worker.joinable()) worker.join(); }
		};
		State states[kSystems.size()];
		State& StateFor(int side) { return states[size_t(side - kFirst)]; }
	}
	void StartScan(int side, const std::string& folder)
	{
		if (!IsRetro(side)) return;
		auto& state = StateFor(side);
		{
			std::lock_guard lock(state.mutex);
			if (state.scanning) { state.nextFolder = folder; state.pending = true; return; }
		}
		if (state.worker.joinable()) state.worker.join();
		state.scanning = true;
		try { state.worker = std::thread([&state, side, folder] {
			std::string current = folder;
			for (;;)
			{
				std::string error; std::vector<ps5emu::Game> games;
				try { games = Scan(Of(side).key, current, error); } catch (const std::exception& e) { error = e.what(); }
				std::lock_guard lock(state.mutex);
				if (state.pending)
				{
					current = std::move(state.nextFolder); state.pending = false;
					continue; // another folder was chosen while this one was scanned: only the newest one's games are kept
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
