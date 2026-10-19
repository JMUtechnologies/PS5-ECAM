// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: games in archives (.zip, .7z, .rar and tar files, plain or compressed) on every side.
//
// The DS side reads a game out of its archive into memory (melonds/archive.h); the 3DS's and the Wii
// U's are too large for that, so an archived 3DS ROM, or a Wii U title folder (code, content, meta),
// is unpacked once into the data folder's cache/archives and started from there, as Cemu and Azahar
// start any file. The libraries list an archive's game by what is in it (its title ID, name and icon,
// read from its first bytes as a plain file's are), so an archived game is the same game as its
// plain file: its settings, saves, box art and tile follow it.
//
//  - .zip is read with libzip: names in UTF-8 whatever the locale, and each entry reached at once
//    through the central directory. Its reads go through the C library's open, pread and fstat, so
//    an archive on a network share (/net, app/netfs.h) opens as any other file does.
//  - .7z, .rar and tar with libarchive, as melonDS's own frontend reads them. In the app's C locale
//    libarchive drops a name it cannot convert (a 7z's "Pokémon"): such an entry is listed with no
//    name, and the sides tell a ROM from its first bytes. libarchive 3.7.4 reads no solid RAR4.
//  - Unpacking is written with the C library (never libarchive's disk writer, which the console
//    lacks), into <key>.partial, then renamed: a copy stopped part-way is never taken for a whole one.
//    A copy is kept for the next start and found again by the archive's path, size and time; the
//    oldest are removed to make room, and Settings > Diagnostics clears them all.
//
// The header stays C++17 (melonDS's own build includes it, through melonds/archive.h).

#pragma once

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace ps5archive
{
	// Whether a file's name is one of the archives the sides open (melonDS's frontend's
	// ArchiveExtensions, those libarchive reads here). By name only: the launcher's counts and the
	// tiles' sides need no archive opened
	inline bool IsArchive(const std::string& path)
	{
		static constexpr const char* kExtensions[] = {".zip", ".7z", ".rar", ".tar", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2",
			".tbz2", ".tar.zst", ".tzst", ".tar.lzma", ".tlz"};
		std::string lower = path;
		for (char& c : lower)
			c = (char)std::tolower((unsigned char)c);
		for (const char* extension : kExtensions)
		{
			const size_t length = std::strlen(extension);
			if (lower.size() > length && lower.compare(lower.size() - length, length, extension) == 0)
				return true;
		}
		return false;
	}

	// The archive's own name without its extensions ("Game.tar.gz": "Game"), as a game's name when
	// what is in it has none
	std::string Stem(const std::string& path);

	// The archive's kind, as the libraries' Format column shows it: ZIP, 7Z, RAR or TAR
	std::string Kind(const std::string& path);

	// A regular file in an archive
	struct Entry
	{
		int index = -1;	  // its place among the archive's entries (the backend's order); -1: none
		std::string name; // its path in the archive, '/' between folders; empty when libarchive drops it
		uint64_t size = 0; // uncompressed
	};

	// The entry being visited, read from its start onward
	class Stream
	{
	public:
		virtual ~Stream() = default;
		// Up to bytes of the entry's next bytes into out; 0 at its end, -1 on an error
		virtual int64_t Read(void* out, size_t bytes) = 0;
		// The entry's bytes from offset (at or after what was read so far) into out, all of them: false
		// when the entry is shorter or unreadable, or offset is behind. The 3DS's and the Wii U's headers
		// are read this way, at offsets that only increase
		bool ReadAt(uint64_t offset, void* out, size_t bytes);
		uint64_t Position() const { return m_position; }

	protected:
		uint64_t m_position = 0; // set by Read's implementations
	};

	// Every regular file in the archive in its order, Mac copies ("__MACOSX/", "._") left out: visit
	// is called for each with a Stream over it, and stops the walk by returning false. False (with a
	// reason in error, in English, for the boot log) when the archive cannot be opened or its walk
	// fails before its end, unless visit stopped it
	bool Visit(const std::string& path, const std::function<bool(const Entry&, Stream&)>& visit, std::string* error = nullptr);

	// How an unpacking goes, read by the launcher while it runs on a thread of its own; cancel stops it
	// at its next block
	struct Progress
	{
		std::atomic<uint64_t> done{0}, total{0}; // bytes
		std::atomic<bool> cancel{false};
	};

	// What an unpacked copy is of: an archived ROM (folder false: the entry, found again by index and
	// checked by name and size), or a Wii U title's folder (folder true: every entry whose name starts
	// with member, the title's folder in the archive with its '/', or "" for one at the archive's top)
	struct Source
	{
		std::string archive; // the archive's path
		std::string member;
		int index = -1;
		uint64_t size = 0;
		bool folder = false;
	};

	// Where the source's whole unpacked copy is, when there is one: a ROM's file (named as in the
	// archive, or game.3ds when libarchive dropped its name: Azahar knows a ROM by its first bytes) or
	// the title's folder. Empty when it has to be unpacked first. Marks the copy used, for
	// the oldest-first clean-up
	std::string Unpacked(const Source& source);

	// Unpacks the source into the cache (the data folder's cache/archives), removing the oldest other
	// copies first while the drive lacks room for this one; then where it is, as Unpacked says. Run
	// on a thread of the caller's. False with a reason in error, in the menus' language (or empty when
	// progress->cancel stopped it), and nothing of it left behind
	bool Unpack(const Source& source, Progress* progress, std::string& path, std::string& error);

	// The folder the unpacked copies are in (the data folder's cache/archives), so a list of games can
	// leave out a copy that Cemu's title cache picked up when it started one
	std::string UnpackedRoot();

	// The unpacked copies' size, and removing them all (Settings > Diagnostics)
	uint64_t UnpackedBytes();
	void ClearUnpacked();
}
