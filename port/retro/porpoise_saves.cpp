// SPDX-License-Identifier: GPL-3.0-or-later
// Layouts as in Porpoise-X's src/ui_memcard.cpp (Copyright (C) 2026 Ruben, Project Porpoise, GPL-3.0-or-later),
// which has them from Dolphin's GCMemcard and YAGCD. POSIX calls, as the console's paths need.
#include "porpoise_saves.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PS5CEMU_VERSION
#define PS5CEMU_VERSION "dev"
#endif
#ifndef PS5CEMU_USB_ROOT
#define PS5CEMU_USB_ROOT "/mnt"
#endif

namespace ps5porpoise
{
	using ps5titlesaves::Container;
	using ps5titlesaves::TitleSave;

	namespace
	{
		constexpr size_t kBlock = 0x2000, kEntry = 64;
		constexpr int64_t kEpoch2000 = 946684800;
		constexpr const char* kUsbFolder = "/Porpoise Saves";
		constexpr const char* kWiiPathFile = "porpoise-wii-path.txt";

		uint16_t Be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
		uint32_t Be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

		bool IsDir(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}
		bool Exists(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0;
		}
		std::vector<std::string> Names(const std::string& dir)
		{
			std::vector<std::string> names;
			if (DIR* d = opendir(dir.c_str()))
			{
				while (const dirent* e = readdir(d))
					if (e->d_name[0] != '.')
						names.push_back(e->d_name);
				closedir(d);
			}
			std::sort(names.begin(), names.end());
			return names;
		}
		bool EndsWith(const std::string& s, const char* end)
		{
			const size_t n = std::strlen(end);
			return s.size() >= n && s.compare(s.size() - n, n, end) == 0;
		}
		bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
		{
			std::FILE* f = std::fopen(path.c_str(), "rb");
			if (!f)
				return false;
			uint8_t buffer[65536];
			size_t got;
			out.clear();
			while ((got = std::fread(buffer, 1, sizeof buffer, f)) > 0)
				out.insert(out.end(), buffer, buffer + got);
			const bool ok = !std::ferror(f);
			std::fclose(f);
			return ok;
		}
		bool CopyFile(const std::string& from, const std::string& to)
		{
			std::vector<uint8_t> data;
			if (!ReadFile(from, data))
				return false;
			const std::string part = to + ".part";
			std::FILE* f = std::fopen(part.c_str(), "wb");
			if (!f)
				return false;
			const bool wrote = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
			const bool closed = std::fclose(f) == 0;
			if (!wrote || !closed || std::rename(part.c_str(), to.c_str()) != 0)
			{
				std::remove(part.c_str());
				return false;
			}
			return true;
		}
		bool CopyTree(const std::string& from, const std::string& to, int depth = 0)
		{
			if (depth > 16 || (mkdir(to.c_str(), 0777) != 0 && errno != EEXIST))
				return false;
			for (const std::string& name : Names(from))
			{
				const std::string a = from + "/" + name, b = to + "/" + name;
				if (!(IsDir(a) ? CopyTree(a, b, depth + 1) : CopyFile(a, b)))
					return false;
			}
			return true;
		}
		void MakeParents(const std::string& path)
		{
			for (size_t at = path.find('/', 1); at != std::string::npos; at = path.find('/', at + 1))
				mkdir(path.substr(0, at).c_str(), 0777);
		}
		void Tally(const std::string& dir, TitleSave& save, int depth = 0)
		{
			if (depth > 16)
				return;
			for (const std::string& name : Names(dir))
			{
				const std::string path = dir + "/" + name;
				struct stat st;
				if (stat(path.c_str(), &st) != 0)
					continue;
				if (S_ISDIR(st.st_mode))
					Tally(path, save, depth + 1);
				else
				{
					save.bytes += uint64_t(st.st_size);
					save.changed = std::max<int64_t>(save.changed, st.st_mtime);
				}
			}
		}
		std::string Text(const uint8_t* p, size_t n)
		{
			std::string s;
			for (size_t i = 0; i < n && p[i]; i++)
				s += p[i] >= 0x20 && p[i] < 0x7f ? char(p[i]) : ' ';
			const size_t first = s.find_first_not_of(' ');
			if (first == std::string::npos)
				return "";
			return s.substr(first, s.find_last_not_of(' ') - first + 1);
		}
		void Rgb5a3(uint16_t v, uint8_t* out)
		{
			if (v & 0x8000) // RGB555, opaque
			{
				out[0] = uint8_t(((v >> 10) & 0x1f) * 255 / 31);
				out[1] = uint8_t(((v >> 5) & 0x1f) * 255 / 31);
				out[2] = uint8_t((v & 0x1f) * 255 / 31);
				out[3] = 255;
			}
			else // A3 RGB444
			{
				out[0] = uint8_t(((v >> 8) & 0xf) * 17);
				out[1] = uint8_t(((v >> 4) & 0xf) * 17);
				out[2] = uint8_t((v & 0xf) * 17);
				out[3] = uint8_t(((v >> 12) & 0x7) * 255 / 7);
			}
		}
		// RGB5A3 in 4x4 tiles
		void DecodeRgb5a3(const uint8_t* src, size_t avail, int w, int h, ps5titlesaves::Picture& out)
		{
			if (avail < size_t(w) * h * 2)
				return;
			out.width = w, out.height = h;
			out.rgba.assign(size_t(w) * h * 4, 0);
			size_t i = 0;
			for (int ty = 0; ty < h; ty += 4)
				for (int tx = 0; tx < w; tx += 4)
					for (int y = 0; y < 4; y++)
						for (int x = 0; x < 4; x++, i += 2)
							Rgb5a3(Be16(src + i), &out.rgba[(size_t(ty + y) * w + tx + x) * 4]);
		}
		// 8-bit indices in 8x4 tiles, with a 256-entry RGB5A3 palette
		void DecodeCi8(const uint8_t* src, size_t avail, const uint8_t* palette, int w, int h, ps5titlesaves::Picture& out)
		{
			if (avail < size_t(w) * h)
				return;
			out.width = w, out.height = h;
			out.rgba.assign(size_t(w) * h * 4, 0);
			size_t i = 0;
			for (int ty = 0; ty < h; ty += 4)
				for (int tx = 0; tx < w; tx += 8)
					for (int y = 0; y < 4; y++)
						for (int x = 0; x < 8; x++, i++)
							Rgb5a3(Be16(palette + size_t(src[i]) * 2), &out.rgba[(size_t(ty + y) * w + tx + x) * 4]);
		}
		// The banner and the icon's first frame; the entry says where in the data, and how
		void ParsePictures(const uint8_t* entry, const uint8_t* data, size_t size, TitleSave& out)
		{
			uint64_t off = Be32(entry + 0x2C);
			if (off >= size)
				return;
			const int bannerFormat = entry[0x07] & 3;
			if (bannerFormat == 2)
			{
				DecodeRgb5a3(data + off, size - off, 96, 32, out.banner);
				off += 96 * 32 * 2;
			}
			else if (bannerFormat == 1)
			{
				if (off + 96 * 32 + 512 <= size)
					DecodeCi8(data + off, size - off, data + off + 96 * 32, 96, 32, out.banner);
				off += 96 * 32 + 512;
			}
			// frames: two bits of format and two of speed each, a speed of 0 the end; "shared" CI8 frames use
			// one palette after the last
			const uint16_t formats = Be16(entry + 0x30), speeds = Be16(entry + 0x32);
			uint64_t first = 0, at = off;
			int firstFormat = 0;
			for (int i = 0; i < 8; i++)
			{
				const int format = (formats >> (2 * i)) & 3;
				if (((speeds >> (2 * i)) & 3) == 0)
					break;
				if (format && !firstFormat)
				{
					first = at;
					firstFormat = format;
				}
				at += format == 2 ? 32 * 32 * 2 : format == 1 ? 32 * 32 : format == 3 ? 32 * 32 + 512 : 0;
			}
			if (firstFormat == 2 && first < size)
				DecodeRgb5a3(data + first, size - first, 32, 32, out.icon);
			else if (firstFormat == 1 && first < size && at + 512 <= size)
				DecodeCi8(data + first, size - first, data + at, 32, 32, out.icon);
			else if (firstFormat == 3 && first + 32 * 32 + 512 <= size)
				DecodeCi8(data + first, size - first, data + first + 32 * 32, 32, 32, out.icon);
		}
		// A directory entry, and its save's data for the comment
		bool ParseEntry(const uint8_t* entry, const uint8_t* data, size_t size, TitleSave& out)
		{
			if (Be32(entry) == 0xFFFFFFFF)
				return false;
			out.code = Text(entry, 4) + Text(entry + 4, 2);
			const std::string name = Text(entry + 0x08, 32);
			out.changed = int64_t(Be32(entry + 0x28)) + kEpoch2000;
			out.blocks = Be16(entry + 0x38);
			const uint32_t comment = Be32(entry + 0x3C);
			if (comment != 0xFFFFFFFF && uint64_t(comment) + 64 <= size)
			{
				out.title = Text(data + comment, 32);
				out.detail = Text(data + comment + 32, 32);
			}
			if (out.title.empty())
				out.title = name;
			ParsePictures(entry, data, size, out);
			return true;
		}
		void ReadFolderCard(const std::string& dir, Container& card)
		{
			card.folder = dir;
			card.totalBlocks = 2043;
			int used = 0;
			for (const std::string& name : Names(dir))
			{
				std::vector<uint8_t> file;
				if (!EndsWith(name, ".gci") || !ReadFile(dir + "/" + name, file) || file.size() < kEntry)
					continue;
				TitleSave save;
				if (!ParseEntry(file.data(), file.data() + kEntry, file.size() - kEntry, save))
					continue;
				save.path = dir + "/" + name;
				save.bytes = file.size();
				save.separate = true;
				used += save.blocks;
				card.saves.push_back(std::move(save));
			}
			card.freeBlocks = std::max(0, card.totalBlocks - used);
		}
		// A whole-card image: header, two directories, two allocation tables
		bool ReadImageCard(const std::string& path, Container& card)
		{
			std::vector<uint8_t> raw;
			if (!ReadFile(path, raw) || raw.size() < kBlock * 5)
				return false;
			card.totalBlocks = std::max(0, Be16(&raw[0x22]) * 16 - 5);
			const uint8_t* dir = Be16(&raw[kBlock * 2 + 0x1FFA]) > Be16(&raw[kBlock + 0x1FFA]) ? &raw[kBlock * 2] : &raw[kBlock];
			const uint8_t* bat = Be16(&raw[kBlock * 4 + 4]) > Be16(&raw[kBlock * 3 + 4]) ? &raw[kBlock * 4] : &raw[kBlock * 3];
			card.freeBlocks = Be16(bat + 6);
			const size_t blocks = raw.size() / kBlock;
			for (int i = 0; i < 127; i++)
			{
				const uint8_t* entry = dir + i * kEntry;
				if (Be32(entry) == 0xFFFFFFFF)
					continue;
				std::vector<uint8_t> data;
				uint16_t block = Be16(entry + 0x36);
				const int count = Be16(entry + 0x38);
				for (int n = 0; n < count && block >= 5 && block < blocks; n++)
				{
					data.insert(data.end(), raw.begin() + std::ptrdiff_t(block * kBlock), raw.begin() + std::ptrdiff_t((block + 1) * kBlock));
					const size_t at = 0x0A + size_t(block - 5) * 2;
					if (at + 2 > kBlock)
						break;
					block = Be16(bat + at);
					if (block == 0xFFFF)
						break;
				}
				TitleSave save;
				if (ParseEntry(entry, data.data(), data.size(), save))
				{
					save.path = path;
					save.bytes = uint64_t(save.blocks) * kBlock;
					card.saves.push_back(std::move(save));
				}
			}
			return true;
		}
		void Scan(const std::string& dir, int depth, std::vector<std::string>& folders, std::vector<std::string>& images)
		{
			for (const std::string& name : Names(dir))
			{
				const std::string path = dir + "/" + name;
				if (IsDir(path))
				{
					if (name == "Card A" || name == "Card B")
						folders.push_back(path);
					else if (depth > 0)
						Scan(path, depth - 1, folders, images);
				}
				else if (name.rfind("MemoryCard", 0) == 0 && EndsWith(name, ".raw"))
					images.push_back(path);
			}
		}
		std::string Utf16Be(const uint8_t* p, size_t n)
		{
			std::string out;
			for (size_t i = 0; i < n; i++)
			{
				uint32_t c = uint32_t(p[i * 2]) << 8 | p[i * 2 + 1];
				if (c == 0)
					break;
				if (c >= 0xD800 && c < 0xDC00 && i + 1 < n)
				{
					const uint32_t low = uint32_t(p[i * 2 + 2]) << 8 | p[i * 2 + 3];
					c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
					i++;
				}
				if (c < 0x80)
					out += char(c);
				else if (c < 0x800)
				{
					out += char(0xC0 | (c >> 6));
					out += char(0x80 | (c & 0x3F));
				}
				else if (c < 0x10000)
				{
					out += char(0xE0 | (c >> 12));
					out += char(0x80 | ((c >> 6) & 0x3F));
					out += char(0x80 | (c & 0x3F));
				}
				else
				{
					out += char(0xF0 | (c >> 18));
					out += char(0x80 | ((c >> 12) & 0x3F));
					out += char(0x80 | ((c >> 6) & 0x3F));
					out += char(0x80 | (c & 0x3F));
				}
			}
			while (!out.empty() && (out.back() == ' ' || out.back() == '\n'))
				out.pop_back();
			return out;
		}
		bool ByTitle(const TitleSave& a, const TitleSave& b) { return a.title < b.title; }
	}

	std::vector<Container> ReadCards(const std::string& savesDir)
	{
		std::vector<std::string> folders, images;
		Scan(savesDir + "/User/GC", 4, folders, images);
		const char* const regions[] = {"USA", "EUR", "JAP"};
		// a card's region: its folder's, or in its image's name
		const auto regionOf = [](const std::string& path, bool folder) {
			if (folder)
			{
				const size_t end = path.rfind('/');
				return end == std::string::npos ? std::string() : path.substr(path.rfind('/', end - 1) + 1, end - path.rfind('/', end - 1) - 1);
			}
			const std::string name = path.substr(path.rfind('/') + 1);
			return name.size() >= 15 ? name.substr(12, 3) : std::string();
		};
		std::vector<Container> cards;
		for (const char* region : regions)
			for (const char* slot : {"A", "B"})
			{
				Container card;
				card.id = slot;
				card.region = region;
				bool found = false;
				for (const std::string& folder : folders)
					if (!found && folder.back() == slot[0] && regionOf(folder, true) == region)
					{
						ReadFolderCard(folder, card);
						found = true;
					}
				for (const std::string& image : images)
				{
					const std::string name = image.substr(image.rfind('/') + 1);
					if (!found && name.size() > 10 && name[10] == slot[0] && regionOf(image, false) == region)
						found = ReadImageCard(image, card);
				}
				if (!found)
					continue;
				std::sort(card.saves.begin(), card.saves.end(), ByTitle);
				cards.push_back(std::move(card));
			}
		// no card at all: an empty A, in USA. Then each card without its other slot in its region gets that one,
		// empty. Dolphin makes them when a game first saves there.
		const auto empty = [&](const char* slot, const std::string& region) {
			Container card;
			card.id = slot;
			card.region = region;
			card.folder = savesDir + "/User/GC/" + region + "/Card " + slot;
			card.totalBlocks = card.freeBlocks = 2043;
			return card;
		};
		if (cards.empty())
			cards.push_back(empty("A", "USA"));
		const std::vector<Container> found = cards;
		for (const Container& card : found)
		{
			const char* other = card.id == "A" ? "B" : "A";
			if (std::none_of(found.begin(), found.end(), [&](const Container& c) { return c.id == other && c.region == card.region; }))
				cards.push_back(empty(other, card.region));
		}
		std::stable_sort(cards.begin(), cards.end(), [&](const Container& a, const Container& b) {
			const auto rank = [&](const Container& c) { return std::find_if(std::begin(regions), std::end(regions), [&](const char* r) { return c.region == r; }) - std::begin(regions); };
			return rank(a) != rank(b) ? rank(a) < rank(b) : a.id < b.id;
		});
		return cards;
	}

	Container ReadWiiSaves(const std::string& savesDir)
	{
		Container wii;
		wii.id = "wii";
		for (const char* kind : {"00010000", "00010004", "00010001"})
		{
			const std::string root = savesDir + "/User/Wii/title/" + kind;
			for (const std::string& id : Names(root))
			{
				const std::string data = root + "/" + id + "/data";
				if (id.size() != 8 || id.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos || !IsDir(data))
					continue;
				TitleSave save;
				save.path = data;
				save.separate = true;
				for (size_t i = 0; i < 8; i += 2)
				{
					const int v = int(std::strtol(id.substr(i, 2).c_str(), nullptr, 16));
					save.code += v >= 0x20 && v < 0x7F ? char(v) : '?';
				}
				Tally(data, save);
				if (save.bytes == 0 && Names(data).empty())
					continue;
				std::vector<uint8_t> banner;
				if (ReadFile(data + "/banner.bin", banner) && banner.size() >= 0xA0 && std::memcmp(banner.data(), "WIBN", 4) == 0)
				{
					save.title = Utf16Be(banner.data() + 0x20, 32);
					save.detail = Utf16Be(banner.data() + 0x60, 32);
					constexpr size_t kHeader = 0xA0, kBanner = 192 * 64 * 2;
					if (banner.size() >= kHeader + kBanner)
						DecodeRgb5a3(banner.data() + kHeader, banner.size() - kHeader, 192, 64, save.banner);
					if (banner.size() >= kHeader + kBanner + 48 * 48 * 2)
						DecodeRgb5a3(banner.data() + kHeader + kBanner, banner.size() - kHeader - kBanner, 48, 48, save.icon);
				}
				if (save.title.empty())
					save.title = save.code;
				wii.saves.push_back(std::move(save));
			}
		}
		std::sort(wii.saves.begin(), wii.saves.end(), ByTitle);
		return wii;
	}

	std::string FirstUsbDrive()
	{
		for (int i = 0; i < 8; i++)
		{
			const std::string drive = std::string(PS5CEMU_USB_ROOT) + "/usb" + std::to_string(i);
			if (IsDir(drive))
				return drive;
		}
		return "";
	}

	namespace
	{
		constexpr const char* kBackups = "/PS5CEMU-HAR-saves";

		std::string BackupRoot(const std::string& drive, const std::string& stamp) { return drive + kBackups + "/" + stamp; }

		int CountFiles(const std::string& dir, int depth = 0)
		{
			int files = 0;
			for (const std::string& name : Names(dir))
				files += IsDir(dir + "/" + name) ? (depth < 16 ? CountFiles(dir + "/" + name, depth + 1) : 0) : 1;
			return files;
		}

		// What FindBackups reads; a folder without it isn't a backup
		bool FinishBackup(const std::string& drive, const std::string& stamp)
		{
			TitleSave total;
			Tally(BackupRoot(drive, stamp) + "/retro", total);
			std::FILE* f = std::fopen((BackupRoot(drive, stamp) + "/backup.json").c_str(), "w");
			if (!f)
				return false;
			std::fprintf(f, "{\"version\": \"%s\", \"date\": \"%s\", \"files\": %d, \"bytes\": %llu}\n", PS5CEMU_VERSION, stamp.c_str(), CountFiles(BackupRoot(drive, stamp) + "/retro"), (unsigned long long)total.bytes);
			return std::fclose(f) == 0;
		}

		// file or folder under <saves>/User/GC or /User/Wii, to its place in the backup
		bool CopyToBackup(const std::string& savesDir, const std::string& path, const std::string& drive, const std::string& stamp, std::string& where)
		{
			const std::string gc = savesDir + "/User/GC/", wii = savesDir + "/User/Wii/";
			std::string set;
			if (path.compare(0, gc.size(), gc) == 0)
				set = "gc/" + path.substr(gc.size());
			else if (path.compare(0, wii.size(), wii) == 0)
				set = "wii/" + path.substr(wii.size());
			else
				return false;
			where = BackupRoot(drive, stamp) + "/retro/porpoise/" + set;
			MakeParents(where);
			return IsDir(path) ? CopyTree(path, where) : CopyFile(path, where);
		}
	}

	bool ExportSave(const std::string& savesDir, const TitleSave& save, const std::string& drive, const std::string& stamp, std::string& where, std::string& error)
	{
		if (!save.separate || drive.empty())
		{
			error = drive.empty() ? "no USB drive" : "a whole-card image";
			return false;
		}
		if (!CopyToBackup(savesDir, save.path, drive, stamp, where) || !FinishBackup(drive, stamp))
		{
			error = std::strerror(errno);
			return false;
		}
		return true;
	}

	bool ExportCard(const std::string& savesDir, const Container& card, const std::string& drive, const std::string& stamp, std::string& where, std::string& error)
	{
		if (drive.empty() || card.saves.empty())
		{
			error = drive.empty() ? "no USB drive" : "an empty card";
			return false;
		}
		const std::string path = card.folder.empty() ? card.saves.front().path : card.folder;
		if (!CopyToBackup(savesDir, path, drive, stamp, where) || !FinishBackup(drive, stamp))
		{
			error = std::strerror(errno);
			return false;
		}
		return true;
	}

	bool ImportSaves(const std::string& savesDir, const std::string& cardFolder, const std::string& drive, Imported& imported, std::string& error)
	{
		if (drive.empty() || (!IsDir(drive + kUsbFolder) && !IsDir(drive + kBackups)))
		{
			error = "no saves on a USB drive";
			return false;
		}
		// the app's backups, newest first: what isn't here yet, kept where it was
		std::vector<std::string> stamps = Names(drive + kBackups);
		std::reverse(stamps.begin(), stamps.end());
		const std::function<void(const std::string&, const std::string&, int, bool)> walk = [&](const std::string& from, const std::string& to, int depth, bool wii) {
			for (const std::string& name : Names(from))
			{
				const std::string a = from + "/" + name, b = to + "/" + name;
				if (IsDir(a))
				{
					if (depth < 8)
						walk(a, b, depth + 1, wii);
				}
				else if (wii || EndsWith(name, ".gci"))
				{
					if (Exists(b))
						imported.skipped++;
					else
					{
						MakeParents(b);
						if (CopyFile(a, b))
							(wii ? imported.wii : imported.gameCube)++;
					}
				}
			}
		};
		for (const std::string& stamp : stamps)
		{
			const std::string backup = BackupRoot(drive, stamp) + "/retro/porpoise";
			walk(backup + "/gc", savesDir + "/User/GC", 0, false);
			walk(backup + "/wii", savesDir + "/User/Wii", 0, true);
		}
		const std::string root = drive + kUsbFolder;
		if (!cardFolder.empty())
			for (const std::string& name : Names(root + "/GameCube"))
			{
				if (!EndsWith(name, ".gci"))
					continue;
				const std::string to = cardFolder + "/" + name;
				if (Exists(to))
					imported.skipped++;
				else if (CopyFile(root + "/GameCube/" + name, to))
					imported.gameCube++;
			}
		for (const std::string& name : Names(root + "/Wii"))
		{
			const std::string from = root + "/Wii/" + name;
			char line[256] = {0};
			if (std::FILE* f = std::fopen((from + "/" + kWiiPathFile).c_str(), "r"))
			{
				if (!std::fgets(line, sizeof line, f))
					line[0] = 0;
				std::fclose(f);
			}
			std::string relative = line;
			while (!relative.empty() && (relative.back() == '\n' || relative.back() == '\r'))
				relative.pop_back();
			// only ever into the title folders, whatever a drive's file says
			if (relative.rfind("title/", 0) != 0 || relative.find("..") != std::string::npos)
				continue;
			const std::string to = savesDir + "/User/Wii/" + relative;
			if (IsDir(to))
			{
				imported.skipped++;
				continue;
			}
			MakeParents(to);
			if (CopyTree(from, to))
			{
				std::remove((to + "/" + kWiiPathFile).c_str());
				imported.wii++;
			}
		}
		return true;
	}

	std::vector<UsbSave> FindUsbSaves(const std::string& drive, bool wii)
	{
		std::vector<UsbSave> found;
		std::vector<std::string> stamps = Names(drive + kBackups);
		std::reverse(stamps.begin(), stamps.end());
		const std::function<void(const std::string&, const std::string&, const std::string&, int)> walk = [&](const std::string& dir, const std::string& relative, const std::string& when, int depth) {
			for (const std::string& name : Names(dir))
			{
				const std::string path = dir + "/" + name, inside = relative.empty() ? name : relative + "/" + name;
				if (wii && name == "data" && IsDir(path))
				{
					TitleSave save;
					std::vector<uint8_t> banner;
					if (ReadFile(path + "/banner.bin", banner) && banner.size() >= 0xA0 && std::memcmp(banner.data(), "WIBN", 4) == 0)
						save.title = Utf16Be(banner.data() + 0x20, 32);
					found.push_back({path, (save.title.empty() ? relative : save.title) + " · " + when, inside});
				}
				else if (IsDir(path))
				{
					if (depth < 8)
						walk(path, inside, when, depth + 1);
				}
				else if (!wii && EndsWith(name, ".gci"))
				{
					std::vector<uint8_t> file;
					TitleSave save;
					if (ReadFile(path, file) && file.size() >= kEntry && ParseEntry(file.data(), file.data() + kEntry, file.size() - kEntry, save))
						found.push_back({path, save.title + " · " + when, name});
				}
			}
		};
		for (const std::string& stamp : stamps)
			walk(BackupRoot(drive, stamp) + "/retro/porpoise/" + (wii ? "wii" : "gc"), "", stamp, 0);
		if (!wii)
			walk(drive + kUsbFolder + "/GameCube", "", "Porpoise Saves", 0);
		return found;
	}

	std::vector<UsbCard> FindUsbCards(const std::string& drive)
	{
		std::vector<UsbCard> found;
		std::vector<std::string> stamps = Names(drive + kBackups);
		std::reverse(stamps.begin(), stamps.end());
		for (const std::string& stamp : stamps)
		{
			const std::string gc = BackupRoot(drive, stamp) + "/retro/porpoise/gc";
			for (const std::string& region : Names(gc))
				for (const std::string& card : Names(gc + "/" + region))
					if (IsDir(gc + "/" + region + "/" + card))
						found.push_back({gc + "/" + region + "/" + card, card + " (" + region + ") · " + stamp});
		}
		return found;
	}

	bool ImportOneSave(const std::string& savesDir, const UsbSave& save, const std::string& cardFolder, std::string& error)
	{
		const bool wii = IsDir(save.path);
		const std::string to = wii ? savesDir + "/User/Wii/" + save.relative : cardFolder + "/" + save.relative;
		if (save.relative.find("..") != std::string::npos || cardFolder.empty() && !wii)
		{
			error = "not a place for it";
			return false;
		}
		if (Exists(to))
		{
			error = "already here";
			return false;
		}
		MakeParents(to);
		if (!(wii ? CopyTree(save.path, to) : CopyFile(save.path, to)))
		{
			error = std::strerror(errno);
			return false;
		}
		return true;
	}

	bool ImportCard(const UsbCard& card, const std::string& cardFolder, Imported& imported, std::string& error)
	{
		if (cardFolder.empty())
		{
			error = "a whole-card image";
			return false;
		}
		for (const std::string& name : Names(card.path))
		{
			if (!EndsWith(name, ".gci"))
				continue;
			const std::string to = cardFolder + "/" + name;
			if (Exists(to))
				imported.skipped++;
			else
			{
				MakeParents(to);
				if (CopyFile(card.path + "/" + name, to))
					imported.gameCube++;
			}
		}
		return true;
	}

	std::string MoveSaveAside(const std::string& savesDir, const TitleSave& save, const std::string& stamp, std::string& error)
	{
		const std::string prefix = savesDir + "/";
		if (!save.separate || save.path.compare(0, prefix.size(), prefix) != 0)
		{
			error = "not a save of its own";
			return "";
		}
		const std::string to = savesDir + "/before/" + stamp + "/" + save.path.substr(prefix.size());
		MakeParents(to);
		if (std::rename(save.path.c_str(), to.c_str()) != 0)
		{
			error = std::strerror(errno);
			return "";
		}
		return to;
	}
}
