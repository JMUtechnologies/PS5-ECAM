// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"
#include "archive.h"
#include "melonds.h"
#include "../app/lang.h"
#include "../app/paths.h"
#include "../ps5/log.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace ps5melonds
{
	namespace
	{
		constexpr int kIconSize = 32;
		constexpr int kScanDepth = 4; // folders in folders, as the 3DS side looks

		std::atomic<bool> s_scanning{false};
		std::mutex s_mutex;
		std::vector<ps5emu::Game> s_games; // under s_mutex
		constexpr size_t kHeaderSize = 0x200;
		// the banner as far as the Korean title: version 1 has the six languages, 2 adds Chinese, 3 Korean
		constexpr size_t kBannerSize = 0xA40;

		uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
		uint32_t U32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = (char)std::tolower((unsigned char)c);
			return text;
		}

		// A banner title (UTF-16LE, up to a NUL) as UTF-8 lines
		std::vector<std::string> Lines(const uint8_t* data, size_t bytes)
		{
			std::vector<std::string> lines(1);
			for (size_t i = 0; i + 1 < bytes; i += 2)
			{
				uint32_t c = data[i] | data[i + 1] << 8;
				if (c == 0)
					break;
				if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes)
				{
					const uint32_t low = data[i + 2] | data[i + 3] << 8;
					if (low >= 0xDC00 && low < 0xE000)
					{
						c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
						i += 2;
					}
				}
				if (c == '\n')
				{
					lines.emplace_back();
					continue;
				}
				if (c == '\r')
					continue;
				std::string& out = lines.back();
				if (c < 0x80)
					out += (char)c;
				else if (c < 0x800)
					out += {(char)(0xC0 | c >> 6), (char)(0x80 | (c & 0x3F))};
				else if (c < 0x10000)
					out += {(char)(0xE0 | c >> 12), (char)(0x80 | (c >> 6 & 0x3F)), (char)(0x80 | (c & 0x3F))};
				else
					out += {(char)(0xF0 | c >> 18), (char)(0x80 | (c >> 12 & 0x3F)), (char)(0x80 | (c >> 6 & 0x3F)), (char)(0x80 | (c & 0x3F))};
			}
			for (std::string& line : lines)
			{
				while (!line.empty() && line.back() == ' ')
					line.pop_back();
				while (!line.empty() && line.front() == ' ')
					line.erase(line.begin());
			}
			std::erase_if(lines, [](const std::string& line) { return line.empty(); });
			return lines;
		}

		// The banner's title in the menus' language, as a DS set to it shows it, else
		// in English (or the first language that has one): its last line the publisher, the lines
		// before it the name (a subtitle after a colon). A banner has Japanese, English, French,
		// German, Italian and Spanish; from its version 2 Chinese (the iQue's, simplified), from 3 Korean.
		void ReadTitle(const std::vector<uint8_t>& banner, Title& title)
		{
			const int version = banner[0] | banner[1] << 8;
			const int languages = version >= 3 ? 8 : version >= 2 ? 7 : 6;
			int wanted = ps5lang::NintendoLanguage();
			if (wanted == 11)
				wanted = 6; // traditional Chinese: the banner's Chinese is the nearest
			if (wanted >= languages)
				wanted = 1;
			for (const int language : {wanted, 1, 0, 2, 3, 4, 5})
			{
				const std::vector<std::string> lines = Lines(banner.data() + 0x240 + language * 0x100, 0x100);
				if (lines.empty())
					continue;
				size_t nameLines = lines.size() > 1 ? lines.size() - 1 : 1;
				if (lines.size() > 1)
					title.publisher = lines.back();
				std::string name = lines[0];
				for (size_t i = 1; i < nameLines; i++)
				{
					const char last = name.empty() ? ' ' : name.back();
					name += std::isalnum((unsigned char)last) ? ": " + lines[i] : " " + lines[i];
				}
				title.name = name;
				return;
			}
		}

		// 32x32, 4 bits a pixel in 8x8 tiles (the left pixel in the low nibble), with 16 BGR555
		// colours, the first one transparent
		void ReadIcon(const std::vector<uint8_t>& banner, Title& title)
		{
			const uint8_t* pixels = banner.data() + 0x20;
			const uint8_t* palette = banner.data() + 0x220;
			title.icon.resize(kIconSize * kIconSize * 4);
			for (int y = 0; y < kIconSize; y++)
				for (int x = 0; x < kIconSize; x++)
				{
					const int tile = (y / 8) * (kIconSize / 8) + x / 8;
					const uint8_t pair = pixels[tile * 32 + (y % 8) * 4 + (x % 8) / 2];
					const int index = (x & 1) ? pair >> 4 : pair & 0xF;
					const uint16_t colour = U16(palette + index * 2);
					uint8_t* out = &title.icon[(y * kIconSize + x) * 4];
					out[0] = (uint8_t)(((colour >> 10) & 0x1F) * 255 / 31); // blue
					out[1] = (uint8_t)(((colour >> 5) & 0x1F) * 255 / 31);
					out[2] = (uint8_t)((colour & 0x1F) * 255 / 31);
					out[3] = index == 0 ? 0 : 255;
				}
		}

		bool ValidCode(const std::string& code)
		{
			return code.size() == 4 &&
				std::all_of(code.begin(), code.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); });
		}

		std::string CoverFile(uint64_t id)
		{
			return fmt::format("{}/ds/{:016x}.tga", ps5paths::kCovers, id);
		}

		void WriteCover(uint64_t id, const std::vector<uint8_t>& icon)
		{
			// six times the size, each pixel a 6x6 square: the size of a 3DS game's (48 at four times),
			// sharp where the launcher scales it
			constexpr int kScale = 6, kSize = kIconSize * kScale;
			const std::string path = CoverFile(id);
			std::error_code ec;
			if (fs::exists(path, ec))
				return;
			fs::create_directories(fs::path(path).parent_path(), ec);
			std::vector<uint8_t> tga(18 + kSize * kSize * 4);
			tga[2] = 2;
			tga[12] = kSize & 255, tga[13] = kSize >> 8, tga[14] = kSize & 255, tga[15] = kSize >> 8;
			tga[16] = 32, tga[17] = 0x28;
			for (int y = 0; y < kSize; y++)
				for (int x = 0; x < kSize; x++)
					std::memcpy(&tga[18 + (y * kSize + x) * 4], &icon[((y / kScale) * kIconSize + x / kScale) * 4], 4);
			const std::string temporary = path + ".tmp";
			{
				std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
				out.write((const char*)tga.data(), (std::streamsize)tga.size());
				if (!out)
					return;
			}
			fs::rename(temporary, path, ec);
		}
	}

	bool IsDsFile(const std::string& path)
	{
		const std::string extension = Lower(fs::path(path).extension().string());
		return extension == ".nds" || extension == ".srl" || extension == ".dsi";
	}

	namespace
	{
		// What a DS ROM of size bytes is, from its header and its banner, as read(offset, out, length)
		// gives them (false: they are not there)
		template<typename Read>
		Title InspectRom(uint64_t size, Read read)
		{
			Title title;
			uint8_t header[kHeaderSize];
			if (!read(0, header, sizeof(header)))
				return title;
			// the header's own sanity: the ARM9's code in the file, a unit code the DS or DSi knows
			const uint32_t arm9Offset = U32(header + 0x20), arm9Size = U32(header + 0x2C);
			const uint8_t unit = header[0x12];
			if (unit > 3 || arm9Offset < 0x200 || arm9Offset >= size || arm9Size == 0)
				return title;
			title.readable = true;
			title.gameCode.assign((const char*)header + 0x0C, 4);
			if (!ValidCode(title.gameCode))
				title.gameCode.clear();
			title.version = header[0x1E];
			title.dsiOnly = unit == 3;
			title.dsiEnhanced = unit == 2;
			// the header's checksum (of its first 0x15E bytes) and the game code, as they are in the file
			title.titleId = kIdTag | (uint64_t)U16(header + 0x15E) << 32 | U32(header + 0x0C);
			const uint32_t bannerOffset = U32(header + 0x68);
			if (bannerOffset >= 0x200 && bannerOffset + kBannerSize <= size)
			{
				std::vector<uint8_t> banner(kBannerSize);
				if (read(bannerOffset, banner.data(), banner.size()))
				{
					ReadTitle(banner, title);
					ReadIcon(banner, title);
				}
			}
			if (title.name.empty())
			{
				// no banner (homebrew): the header's title, in capitals
				std::string internal((const char*)header, strnlen((const char*)header, 12));
				while (!internal.empty() && internal.back() == ' ')
					internal.pop_back();
				title.name = internal;
			}
			return title;
		}

		// The game for the library, from what its file is
		ps5emu::Game GameOf(const Title& title, const std::string& path, const char* format)
		{
			ps5emu::Game game;
			game.titleId = title.titleId;
			game.name = !title.name.empty() ? title.name : fs::path(path).stem().string();
			game.path = path;
			game.version = title.version;
			game.format = title.dsiOnly ? "DSI" : format;
			game.publisher = title.publisher;
			game.gameId = title.gameCode;
			game.nds = true;
			if (!title.icon.empty())
				WriteCover(game.titleId, title.icon);
			return game;
		}

		// -- games in archives (archive.h) ------------------------------------------------------------

		// The furthest into an archive's ROM the library reads for its banner: past this (a ROM with its
		// banner at the end), the archive's game is named by its file, with no icon
		constexpr uint64_t kArchiveBannerLimit = 64ull << 20;

		// What InspectRom reads of an archive's ROM, read once and kept in kRoot/archives.bin: the
		// header, and the banner where the header puts it
		struct Probe
		{
			uint64_t archiveSize = 0;
			int64_t archiveTime = 0;
			std::string member; // empty: the archive holds no DS ROM
			uint64_t romSize = 0;
			std::vector<uint8_t> header, banner;
		};

		Title InspectProbe(const Probe& probe)
		{
			const uint32_t bannerOffset = probe.header.size() == kHeaderSize ? U32(probe.header.data() + 0x68) : 0;
			return InspectRom(probe.romSize, [&](uint64_t offset, uint8_t* out, size_t length) {
				const std::vector<uint8_t>& part = offset == 0 ? probe.header : probe.banner;
				if ((offset != 0 && offset != bannerOffset) || length > part.size())
					return false;
				std::memcpy(out, part.data(), length);
				return true;
			});
		}

		// The archive's ROM read as far as its banner: false when the archive cannot be read
		bool ProbeArchive(const std::string& path, Probe& probe)
		{
			archive::Rom rom;
			if (!archive::FindRom(path, rom))
				return false;
			probe.member = rom.member;
			probe.romSize = rom.size;
			if (rom.member.empty())
				return true;
			std::vector<uint8_t> data;
			if (!archive::Read(path, rom.member, data, kHeaderSize))
				return false;
			probe.header = data;
			const uint64_t bannerOffset = U32(probe.header.data() + 0x68);
			const uint64_t bannerEnd = bannerOffset + kBannerSize;
			if (bannerOffset >= kHeaderSize && bannerEnd <= rom.size && bannerEnd <= kArchiveBannerLimit &&
				archive::Read(path, rom.member, data, bannerEnd))
				probe.banner.assign(data.begin() + (ptrdiff_t)bannerOffset, data.end());
			return true;
		}

		// kRoot/archives.bin: "PDSA", a version, then each archive's path and its Probe
		constexpr uint32_t kProbesMagic = 0x41534450, kProbesVersion = 1;

		struct Probes
		{
			std::map<std::string, Probe> known; // by the archive's path, as the last scan read them
			std::map<std::string, Probe> seen;	// this scan's
			bool changed = false;
		};

		std::string ProbesPath()
		{
			return std::string(kRoot) + "/archives.bin";
		}

		void Put32(std::string& out, uint32_t value)
		{
			out.append((const char*)&value, sizeof(value));
		}

		void Put64(std::string& out, uint64_t value)
		{
			out.append((const char*)&value, sizeof(value));
		}

		void PutBytes(std::string& out, const void* data, size_t length)
		{
			Put32(out, (uint32_t)length);
			out.append((const char*)data, length);
		}

		struct Cursor
		{
			const std::string& in;
			size_t at = 0;
			bool ok = true;

			template<typename T>
			T Get()
			{
				T value{};
				if (!ok || in.size() - at < sizeof(T))
					return ok = false, value;
				std::memcpy(&value, in.data() + at, sizeof(T));
				at += sizeof(T);
				return value;
			}

			std::string Bytes()
			{
				const uint32_t length = Get<uint32_t>();
				if (!ok || in.size() - at < length)
					return ok = false, std::string();
				std::string bytes = in.substr(at, length);
				at += length;
				return bytes;
			}
		};

		void LoadProbes(Probes& probes)
		{
			std::ifstream file(ProbesPath(), std::ios::binary);
			const std::string in((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			Cursor cursor{in};
			if (cursor.Get<uint32_t>() != kProbesMagic || cursor.Get<uint32_t>() != kProbesVersion)
				return;
			const uint32_t count = cursor.Get<uint32_t>();
			for (uint32_t i = 0; i < count && cursor.ok; i++)
			{
				const std::string path = cursor.Bytes();
				Probe probe;
				probe.archiveSize = cursor.Get<uint64_t>();
				probe.archiveTime = cursor.Get<int64_t>();
				probe.member = cursor.Bytes();
				probe.romSize = cursor.Get<uint64_t>();
				const std::string header = cursor.Bytes(), banner = cursor.Bytes();
				probe.header.assign(header.begin(), header.end());
				probe.banner.assign(banner.begin(), banner.end());
				if (cursor.ok)
					probes.known[path] = std::move(probe);
			}
		}

		void SaveProbes(const Probes& probes)
		{
			if (!probes.changed && probes.seen.size() == probes.known.size())
				return;
			std::string out;
			Put32(out, kProbesMagic);
			Put32(out, kProbesVersion);
			Put32(out, (uint32_t)probes.seen.size());
			for (const auto& [path, probe] : probes.seen)
			{
				PutBytes(out, path.data(), path.size());
				Put64(out, probe.archiveSize);
				Put64(out, (uint64_t)probe.archiveTime);
				PutBytes(out, probe.member.data(), probe.member.size());
				Put64(out, probe.romSize);
				PutBytes(out, probe.header.data(), probe.header.size());
				PutBytes(out, probe.banner.data(), probe.banner.size());
			}
			const std::string temporary = ProbesPath() + ".tmp";
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				file.write(out.data(), (std::streamsize)out.size());
				if (!file)
					return;
			}
			std::error_code ec;
			fs::rename(temporary, ProbesPath(), ec);
		}

		// An archive's game for the library: from what an earlier scan read of it, when the archive has
		// not changed since, else from the archive. False when it holds no DS game
		bool ReadArchiveGame(const fs::directory_entry& entry, Probes& probes, ps5emu::Game& game)
		{
			const std::string path = entry.path().string();
			std::error_code ec;
			const uint64_t size = entry.file_size(ec);
			const int64_t time = (int64_t)entry.last_write_time(ec).time_since_epoch().count();
			if (ec)
				return false;
			Probe probe;
			const auto known = probes.known.find(path);
			if (known != probes.known.end() && known->second.archiveSize == size && known->second.archiveTime == time)
				probe = known->second;
			else
			{
				const auto started = std::chrono::steady_clock::now();
				probe.archiveSize = size;
				probe.archiveTime = time;
				if (!ProbeArchive(path, probe))
				{
					ps5log::Line("[ds] {} could not be read as an archive", path);
					return false;
				}
				probes.changed = true;
				ps5log::Line("[ds] {}: {} ({} ms)", path, probe.member.empty() ? "no DS game in it" : probe.member,
					(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
			}
			probes.seen[path] = probe;
			if (probe.member.empty())
				return false;
			const Title title = InspectProbe(probe);
			if (!title.readable)
				return false;
			// the archive's kind, as the library shows a format: ZIP, 7Z, RAR, TAR
			std::string format = fs::path(path).extension().string();
			for (char& c : format)
				c = (char)std::toupper((unsigned char)c);
			if (!format.empty())
				format.erase(0, 1);
			game = GameOf(title, path, format.empty() ? "NDS" : format.c_str());
			return true;
		}
	}

	Title Inspect(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::error_code ec;
		const uint64_t size = fs::file_size(path, ec);
		if (!file || ec)
			return {};
		return InspectRom(size, [&](uint64_t offset, uint8_t* out, size_t length) {
			file.clear();
			file.seekg((std::streamoff)offset);
			return (bool)file.read((char*)out, (std::streamsize)length);
		});
	}

	Title Inspect(const std::vector<uint8_t>& rom)
	{
		return InspectRom(rom.size(), [&](uint64_t offset, uint8_t* out, size_t length) {
			if (offset > rom.size() || rom.size() - offset < length)
				return false;
			std::memcpy(out, rom.data() + offset, length);
			return true;
		});
	}

	bool ReadGame(const std::string& path, ps5emu::Game& game)
	{
		const Title title = Inspect(path);
		if (!title.readable)
			return false;
		game = GameOf(title, path, "NDS");
		return true;
	}

	std::string CoverPath(uint64_t titleId)
	{
		const std::string path = CoverFile(titleId);
		std::error_code ec;
		return fs::exists(path, ec) ? path : std::string();
	}

	namespace
	{
		void Scan(const fs::path& folder, int depth, std::vector<ps5emu::Game>& games, Probes& probes)
		{
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(folder, ec))
			{
				if (entry.is_directory(ec))
				{
					if (depth < kScanDepth)
						Scan(entry.path(), depth + 1, games, probes);
					continue;
				}
				const std::string path = entry.path().string();
				ps5emu::Game game;
				if (!entry.is_regular_file(ec))
					continue;
				if (IsDsFile(path) ? ReadGame(path, game) : archive::IsArchive(path) && ReadArchiveGame(entry, probes, game))
					games.push_back(std::move(game));
			}
		}
	}

	void StartScan(const std::string& folder)
	{
		if (s_scanning.exchange(true))
			return;
		// the DS side's folders from the first start, so there is somewhere to put games and a BIOS
		std::error_code ec;
		for (const char* sub : {"games", "bios", "saves"})
			fs::create_directories(fs::path(kRoot) / sub, ec);
		std::thread([folder] {
			std::vector<ps5emu::Game> games;
			Probes probes;
			LoadProbes(probes);
			Scan(folder, 0, games, probes);
			SaveProbes(probes);
			std::sort(games.begin(), games.end(), [](const ps5emu::Game& a, const ps5emu::Game& b) { return Lower(a.name) < Lower(b.name); });
			// the same game twice (a copy in another folder): listed once, as the 3DS's title IDs are
			games.erase(std::unique(games.begin(), games.end(), [](const ps5emu::Game& a, const ps5emu::Game& b) { return a.titleId == b.titleId; }),
				games.end());
			ps5log::Line("[ds] {} DS games in {}", games.size(), folder);
			{
				std::lock_guard lock(s_mutex);
				s_games = std::move(games);
			}
			s_scanning = false;
		}).detach();
	}

	bool Scanning()
	{
		return s_scanning;
	}

	std::vector<ps5emu::Game> ListGames()
	{
		std::lock_guard lock(s_mutex);
		return s_games;
	}

	bool OwnBiosFound()
	{
		// the sizes melonDS takes: the ARM9's 4 KiB, the ARM7's 16 KiB
		std::error_code ec;
		const std::string folder = std::string(kRoot) + "/bios/";
		return fs::file_size(folder + "bios9.bin", ec) == 0x1000 && !ec && fs::file_size(folder + "bios7.bin", ec) == 0x4000 && !ec;
	}
}
