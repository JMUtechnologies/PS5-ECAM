// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: DS games in archives (archive.h). After melonDS's ArchiveUtil.cpp (GPL-3.0-or-later, the
// melonDS team): every format and filter libarchive has, the regular files listed, one read out. Its
// single archive_read_data is a loop here, as libarchive may answer a read in parts, and an entry is
// found again by its place in the archive, as not every entry has a name here (FindRom).

#include "archive.h"
#include "library.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cctype>
#include <memory>

namespace ps5melonds::archive
{
	namespace
	{
		constexpr size_t kHeaderSize = 0x200; // a DS ROM's header

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = (char)std::tolower((unsigned char)c);
			return text;
		}

		// An archive opened for reading, closed and freed with it
		struct Reader
		{
			::archive* handle = archive_read_new();

			explicit Reader(const std::string& path)
			{
				if (!handle)
					return;
				archive_read_support_filter_all(handle);
				archive_read_support_format_all(handle);
				if (archive_read_open_filename(handle, path.c_str(), 64 * 1024) != ARCHIVE_OK)
				{
					archive_read_free(handle);
					handle = nullptr;
				}
			}
			~Reader()
			{
				if (handle)
					archive_read_free(handle); // closes it too
			}
			Reader(const Reader&) = delete;
			Reader& operator=(const Reader&) = delete;
		};

		// The archive's next entry. A warning is one: a name that does not convert to the locale's
		// characters (a zip's Japanese names, in the PS5's C locale) is, where melonDS's loop would stop
		bool Next(Reader& reader, archive_entry*& entry, int& result)
		{
			result = archive_read_next_header(reader.handle, &entry);
			return result == ARCHIVE_OK || result == ARCHIVE_WARN;
		}

		// An entry's name, in UTF-8 where libarchive has it so; null when libarchive dropped it
		const char* NameOf(archive_entry* entry)
		{
			const char* name = archive_entry_pathname_utf8(entry);
			return name ? name : archive_entry_pathname(entry);
		}

		// What a Mac's zip keeps beside each file, named as the file is: "__MACOSX/" and "._" entries
		bool IsMacCopy(const std::string& name)
		{
			const size_t slash = name.rfind('/');
			return name.rfind("__MACOSX/", 0) == 0 || name.compare(slash == std::string::npos ? 0 : slash + 1, 2, "._") == 0;
		}

		uint32_t U32(const uint8_t* p)
		{
			return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
		}

		// Whether the entry being read starts as a DS ROM does: the checksum of the Nintendo logo every
		// cartridge's header carries, and its ARM9 code within the entry, as the library checks a ROM
		bool StartsAsRom(Reader& reader, uint64_t size)
		{
			uint8_t header[kHeaderSize];
			size_t got = 0;
			while (got < sizeof(header))
			{
				const la_ssize_t n = archive_read_data(reader.handle, header + got, sizeof(header) - got);
				if (n <= 0)
					return false;
				got += (size_t)n;
			}
			const uint32_t arm9Offset = U32(header + 0x20);
			return (header[0x15C] | header[0x15D] << 8) == 0xCF56 && header[0x12] <= 3 && arm9Offset >= kHeaderSize && arm9Offset < size &&
				U32(header + 0x2C) != 0;
		}
	}

	bool FindRom(const std::string& path, Rom& rom)
	{
		rom = {};
		Reader reader(path);
		if (!reader.handle)
			return false;
		std::vector<Rom> named;
		Rom unnamed;
		archive_entry* entry = nullptr;
		int result;
		for (int index = 0; Next(reader, entry, result); index++)
		{
			if (archive_entry_filetype(entry) != AE_IFREG)
				continue;
			const uint64_t size = (uint64_t)std::max<la_int64_t>(0, archive_entry_size(entry));
			const char* name = NameOf(entry);
			if (name && IsDsFile(name) && !IsMacCopy(name) && size >= kHeaderSize)
				named.push_back({index, name, size});
			else if (!name && unnamed.index < 0 && StartsAsRom(reader, size))
				unnamed = {index, "", size};
			// (the rest of the entry is skipped by the next header's read)
		}
		if (result != ARCHIVE_EOF)
			return false;
		// the first by name, as melonDS lists them (case aside)
		std::stable_sort(named.begin(), named.end(), [](const Rom& a, const Rom& b) { return Lower(a.member) < Lower(b.member); });
		rom = !named.empty() ? named.front() : unnamed;
		return true;
	}

	bool Read(const std::string& path, const Rom& rom, std::vector<uint8_t>& data, uint64_t limit)
	{
		data.clear();
		if (rom.index < 0 || rom.size == 0)
			return false;
		Reader reader(path);
		if (!reader.handle)
			return false;
		archive_entry* entry = nullptr;
		int result;
		for (int index = 0; Next(reader, entry, result); index++)
		{
			if (index < rom.index)
				continue;
			// FindRom's entry, unless the archive has changed since
			const char* name = NameOf(entry);
			if (archive_entry_filetype(entry) != AE_IFREG || (uint64_t)std::max<la_int64_t>(0, archive_entry_size(entry)) != rom.size ||
				rom.member != (name ? name : ""))
				return false;
			const uint64_t wanted = std::min(rom.size, limit);
			data.resize(wanted);
			uint64_t read = 0;
			while (read < wanted)
			{
				const la_ssize_t got = archive_read_data(reader.handle, data.data() + read, (size_t)std::min<uint64_t>(wanted - read, 1 << 20));
				if (got <= 0)
					break;
				read += (uint64_t)got;
			}
			if (read != wanted)
			{
				data.clear();
				return false;
			}
			return true;
		}
		return false;
	}
}
