// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: DS games in archives (archive.h). After melonDS's ArchiveUtil.cpp (GPL-3.0-or-later, the
// melonDS team): every format and filter libarchive has, the regular files listed, one read out. Its
// single archive_read_data is a loop here, as libarchive may answer a read in parts.

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
	}

	bool FindRom(const std::string& path, Rom& rom)
	{
		rom = {};
		Reader reader(path);
		if (!reader.handle)
			return false;
		std::vector<Rom> roms;
		archive_entry* entry = nullptr;
		int result;
		while (Next(reader, entry, result))
		{
			if (archive_entry_filetype(entry) != AE_IFREG)
				continue;
			const char* name = archive_entry_pathname_utf8(entry);
			if (!name)
				name = archive_entry_pathname(entry);
			if (name && IsDsFile(name))
				roms.push_back({name, (uint64_t)std::max<la_int64_t>(0, archive_entry_size(entry))});
			archive_read_data_skip(reader.handle);
		}
		if (result != ARCHIVE_EOF)
			return false;
		// the first by name, as melonDS lists them (case aside)
		std::stable_sort(roms.begin(), roms.end(), [](const Rom& a, const Rom& b) { return Lower(a.member) < Lower(b.member); });
		if (!roms.empty())
			rom = roms.front();
		return true;
	}

	bool Read(const std::string& path, const std::string& member, std::vector<uint8_t>& data, uint64_t limit)
	{
		data.clear();
		Reader reader(path);
		if (!reader.handle)
			return false;
		archive_entry* entry = nullptr;
		int result;
		while (Next(reader, entry, result))
		{
			const char* name = archive_entry_pathname_utf8(entry);
			if (!name)
				name = archive_entry_pathname(entry);
			if (!name || member != name)
			{
				archive_read_data_skip(reader.handle);
				continue;
			}
			const la_int64_t size = archive_entry_size(entry);
			if (size <= 0)
				return false;
			const uint64_t wanted = std::min<uint64_t>((uint64_t)size, limit);
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
