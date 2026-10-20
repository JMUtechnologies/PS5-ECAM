// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: DS games in archives (#50): .zip, .7z, .rar and tar files, plain or compressed, read with
// libarchive as melonDS's own frontend reads them (src/frontend/qt_sdl/ArchiveUtil.cpp, without Qt). The
// game is the archive's first DS ROM by name (FindRom), read into memory at once, as melonDS loads any
// ROM; nothing is written out.

#pragma once

#include "../app/game_archives.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ps5melonds::archive
{
	// Whether a file's name is one of the archives melonDS's frontend opens (its ArchiveExtensions,
	// src/frontend/qt_sdl/Window.cpp, those libarchive reads here), as every side reads them. By name
	// only, so the launcher's counts need no libarchive
	using ps5archive::IsArchive;

	struct Rom
	{
		int index = -1;		// its place among the archive's entries; -1: the archive holds no DS ROM
		std::string member; // its path in the archive; empty when libarchive gives it no name (below)
		uint64_t size = 0;	// uncompressed
	};
	// The archive's DS ROM: the first by name (case aside) of its .nds, .srl and .dsi files, a Mac's
	// copies of them ("__MACOSX/", "._") left out; else the first of its files that libarchive gives
	// no name and that starts as a DS ROM does. libarchive drops a name it cannot write in the
	// locale's characters, and the app's locale is C: a 7z's or a UTF-8 zip's "Pokémon" has none.
	// False when the archive cannot be read
	bool FindRom(const std::string& path, Rom& rom);
	// The ROM's first limit bytes (all of it, by default). False when the archive cannot be read, its
	// entry is not that ROM any more (the archive changed) or it reads short
	bool Read(const std::string& path, const Rom& rom, std::vector<uint8_t>& data, uint64_t limit = UINT64_MAX);
}
