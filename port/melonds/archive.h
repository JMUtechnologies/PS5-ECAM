// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: DS games in archives (#50): .zip, .7z, .rar and tar files, plain or compressed, read with
// libarchive as melonDS's own frontend reads them (src/frontend/qt_sdl/ArchiveUtil.cpp, without Qt). The
// game is the archive's first DS ROM by name (.nds, .srl or .dsi, case aside), read into memory at once,
// as melonDS loads any ROM; nothing is written out.

#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ps5melonds::archive
{
	// Whether a file's name is one of the archives melonDS's frontend opens (its ArchiveExtensions,
	// src/frontend/qt_sdl/Window.cpp, those libarchive reads here). By name only, so the launcher's
	// counts need no libarchive
	inline bool IsArchive(const std::string& path)
	{
		static constexpr const char* kExtensions[] = {".zip", ".7z", ".rar", ".tar", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2",
			".tbz2", ".tar.zst", ".tzst", ".tar.lzma", ".tlz"};
		std::string lower = path;
		for (char& c : lower)
			c = (char)std::tolower((unsigned char)c);
		for (const char* extension : kExtensions)
		{
			// (no ends_with: melonDS's own build, which has core.cpp, is C++17)
			const size_t length = std::strlen(extension);
			if (lower.size() > length && lower.compare(lower.size() - length, length, extension) == 0)
				return true;
		}
		return false;
	}

	struct Rom
	{
		std::string member; // its path in the archive; empty when the archive holds no DS ROM
		uint64_t size = 0;	// uncompressed
	};
	// The archive's DS ROM. False when the archive cannot be read
	bool FindRom(const std::string& path, Rom& rom);
	// The member's first limit bytes (all of it, by default). False when the archive cannot be read, the
	// member is not in it, or it reads short
	bool Read(const std::string& path, const std::string& member, std::vector<uint8_t>& data, uint64_t limit = UINT64_MAX);
}
