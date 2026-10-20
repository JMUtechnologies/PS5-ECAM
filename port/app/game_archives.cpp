// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: games in archives (game_archives.h): an archive's files walked with libzip (a .zip) or
// libarchive (the rest), what the libraries' scans read of each kept by the archive's size and time,
// and the 3DS's and the Wii U's games unpacked into the data folder's cache/archives.

#include "game_archives.h"
#include "lang.h"
#include "paths.h"
#include "../ps5/log.h"

#include <archive.h>
#include <archive_entry.h>
#include <zip.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ps5archive
{
	namespace
	{
		constexpr size_t kBlock = 1 << 20;		  // what a read and a write of an unpacking take at a time
		constexpr uint64_t kSpare = 256ull << 20; // left free on the drive beside an unpacked copy
		// a side's Probes file: "PS5A", a version, then each archive's path, size, time and fields
		constexpr uint32_t kProbesMagic = 0x41355350, kProbesVersion = 1;

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = (char)std::tolower((unsigned char)c);
			return text;
		}

		// What a Mac's zip keeps beside each file, named as the file is: "__MACOSX/" and "._" entries
		bool IsMacCopy(const std::string& name)
		{
			const size_t slash = name.rfind('/');
			return name.rfind("__MACOSX/", 0) == 0 || name.compare(slash == std::string::npos ? 0 : slash + 1, 2, "._") == 0;
		}

		// -- a .zip, with libzip ---------------------------------------------------------------------------

		// The zip read through the C library's open, pread and fstat: libzip's own file source makes a FILE
		// of its descriptor with fdopen, which a network share's descriptor is not (ps5/netfs_files.cpp)
		struct FileSource
		{
			std::string path;
			int fd = -1;
			uint64_t size = 0, offset = 0;
			zip_error_t error;
		};

		zip_int64_t FileSourceCall(void* state, void* data, zip_uint64_t length, zip_source_cmd_t command)
		{
			FileSource& source = *static_cast<FileSource*>(state);
			switch (command)
			{
			case ZIP_SOURCE_OPEN:
			{
				struct stat info{};
				source.fd = open(source.path.c_str(), O_RDONLY);
				if (source.fd < 0 || fstat(source.fd, &info) != 0)
				{
					zip_error_set(&source.error, ZIP_ER_OPEN, errno);
					if (source.fd >= 0)
						close(source.fd);
					source.fd = -1;
					return -1;
				}
				source.size = (uint64_t)info.st_size;
				source.offset = 0;
				return 0;
			}
			case ZIP_SOURCE_READ:
			{
				uint8_t* out = static_cast<uint8_t*>(data);
				const uint64_t wanted = source.offset < source.size ? std::min<uint64_t>(length, source.size - source.offset) : 0;
				uint64_t done = 0;
				while (done < wanted)
				{
					const ssize_t got = pread(source.fd, out + done, (size_t)(wanted - done), (off_t)(source.offset + done));
					if (got < 0 && errno == EINTR)
						continue;
					if (got < 0)
					{
						zip_error_set(&source.error, ZIP_ER_READ, errno);
						return -1;
					}
					if (got == 0)
						break;
					done += (uint64_t)got;
				}
				source.offset += done;
				return (zip_int64_t)done;
			}
			case ZIP_SOURCE_CLOSE:
				if (source.fd >= 0)
					close(source.fd);
				source.fd = -1;
				return 0;
			case ZIP_SOURCE_STAT:
			{
				// asked before the archive is opened too (zip_open_from_source: whether it is there)
				struct stat info{};
				if (source.fd >= 0 ? fstat(source.fd, &info) != 0 : stat(source.path.c_str(), &info) != 0)
				{
					zip_error_set(&source.error, ZIP_ER_READ, errno);
					return -1;
				}
				zip_stat_t* out = static_cast<zip_stat_t*>(data);
				zip_stat_init(out);
				out->size = (zip_uint64_t)info.st_size;
				out->mtime = info.st_mtime;
				out->valid |= ZIP_STAT_SIZE | ZIP_STAT_MTIME;
				return sizeof(zip_stat_t);
			}
			case ZIP_SOURCE_SEEK:
			{
				const zip_int64_t offset = zip_source_seek_compute_offset(source.offset, source.size, data, length, &source.error);
				if (offset < 0)
					return -1;
				source.offset = (uint64_t)offset;
				return 0;
			}
			case ZIP_SOURCE_TELL:
				return (zip_int64_t)source.offset;
			case ZIP_SOURCE_ERROR:
				return zip_error_to_data(&source.error, data, length);
			case ZIP_SOURCE_FREE:
				zip_error_fini(&source.error);
				delete &source;
				return 0;
			case ZIP_SOURCE_SUPPORTS:
				return zip_source_make_command_bitmap(ZIP_SOURCE_OPEN, ZIP_SOURCE_READ, ZIP_SOURCE_CLOSE, ZIP_SOURCE_STAT, ZIP_SOURCE_ERROR,
					ZIP_SOURCE_FREE, ZIP_SOURCE_SEEK, ZIP_SOURCE_TELL, ZIP_SOURCE_SUPPORTS, -1);
			default:
				zip_error_set(&source.error, ZIP_ER_OPNOTSUPP, 0);
				return -1;
			}
		}

		// The zip at path, read through a FileSource; null with libzip's reason in error
		zip_t* OpenZip(const std::string& path, std::string& error)
		{
			zip_error_t why;
			zip_error_init(&why);
			FileSource* state = new FileSource{path};
			zip_error_init(&state->error);
			zip_t* zip = nullptr;
			zip_source_t* source = zip_source_function_create(FileSourceCall, state, &why);
			if (!source)
			{
				zip_error_fini(&state->error);
				delete state;
			}
			else if (!(zip = zip_open_from_source(source, ZIP_RDONLY, &why)))
				zip_source_free(source); // and the state with it (ZIP_SOURCE_FREE)
			if (!zip)
				error = zip_error_strerror(&why);
			zip_error_fini(&why);
			return zip;
		}

		// A zip's entry, opened as it is first read; closed, its CRC checked, at its end
		class ZipStream : public Stream
		{
		public:
			ZipStream(zip_t* zip, zip_uint64_t index) : m_zip(zip), m_index(index) {}
			~ZipStream() override
			{
				if (m_file)
					zip_fclose(m_file);
			}

			int64_t Read(void* out, size_t bytes) override
			{
				if (!m_file && !m_failed)
				{
					m_file = zip_fopen_index(m_zip, m_index, 0);
					m_failed = !m_file;
				}
				if (!m_file)
					return -1;
				const zip_int64_t got = zip_fread(m_file, out, bytes);
				if (got > 0)
					m_position += (uint64_t)got;
				return got < 0 ? -1 : got;
			}

			std::string Error() const override
			{
				const char* why = m_file ? zip_file_strerror(m_file) : zip_strerror(m_zip);
				return why ? why : "";
			}

		private:
			zip_t* m_zip;
			zip_uint64_t m_index;
			zip_file_t* m_file = nullptr;
			bool m_failed = false;
		};

		bool VisitZip(const std::string& path, const std::function<bool(const Entry&, Stream&)>& visit, std::string& error)
		{
			zip_t* zip = OpenZip(path, error);
			if (!zip)
				return false;
			const zip_int64_t count = zip_get_num_entries(zip, 0);
			for (zip_int64_t index = 0; index < count && index < INT_MAX; index++)
			{
				zip_stat_t info;
				zip_stat_init(&info);
				if (zip_stat_index(zip, (zip_uint64_t)index, 0, &info) != 0 || !(info.valid & ZIP_STAT_NAME) || !info.name)
					continue;
				// a folder's entry ends with '/', and some Windows tools wrote '\' between folders
				std::string name = info.name;
				std::replace(name.begin(), name.end(), '\\', '/');
				if (name.empty() || name.back() == '/' || IsMacCopy(name))
					continue;
				// an encrypted entry asks for a password the app has not got
				if ((info.valid & ZIP_STAT_ENCRYPTION_METHOD) && info.encryption_method != ZIP_EM_NONE)
					continue;
				ZipStream stream(zip, (zip_uint64_t)index);
				if (!visit({(int)index, name, (info.valid & ZIP_STAT_SIZE) ? (uint64_t)info.size : 0}, stream))
					break;
			}
			zip_discard(zip);
			return true;
		}

		// -- the rest, with libarchive ---------------------------------------------------------------------

		std::string ErrorOf(::archive* handle)
		{
			const char* why = archive_error_string(handle);
			return why ? why : "unreadable";
		}

		// The entry libarchive has read the header of
		class ArchiveStream : public Stream
		{
		public:
			explicit ArchiveStream(::archive* handle) : m_handle(handle) {}

			int64_t Read(void* out, size_t bytes) override
			{
				const la_ssize_t got = archive_read_data(m_handle, out, bytes);
				if (got > 0)
					m_position += (uint64_t)got;
				return got < 0 ? -1 : got;
			}

			std::string Error() const override { return ErrorOf(m_handle); }

		private:
			::archive* m_handle;
		};

		// As melonds/archive.cpp walks one: every format and filter libarchive has, each header counted
		bool VisitOther(const std::string& path, const std::function<bool(const Entry&, Stream&)>& visit, std::string& error)
		{
			const std::unique_ptr<::archive, int (*)(::archive*)> handle(archive_read_new(), archive_read_free); // closes it too
			if (!handle)
			{
				error = "out of memory";
				return false;
			}
			archive_read_support_filter_all(handle.get());
			archive_read_support_format_all(handle.get());
			if (archive_read_open_filename(handle.get(), path.c_str(), 64 * 1024) != ARCHIVE_OK)
			{
				error = ErrorOf(handle.get());
				return false;
			}
			archive_entry* entry = nullptr;
			int result = ARCHIVE_OK;
			for (int index = 0;; index++)
			{
				result = archive_read_next_header(handle.get(), &entry);
				// a warning is an entry too: a name that does not convert to the locale's characters is one
				if (result != ARCHIVE_OK && result != ARCHIVE_WARN)
					break;
				if (archive_entry_filetype(entry) != AE_IFREG)
					continue;
				const char* name = archive_entry_pathname_utf8(entry);
				if (!name)
					name = archive_entry_pathname(entry);
				const std::string text = name ? name : "";
				if (!text.empty() && IsMacCopy(text))
					continue;
				ArchiveStream stream(handle.get());
				if (!visit({index, text, (uint64_t)std::max<la_int64_t>(0, archive_entry_size(entry))}, stream))
					return true;
				// (the rest of the entry is skipped by the next header's read)
			}
			if (result != ARCHIVE_EOF)
			{
				error = ErrorOf(handle.get());
				return false;
			}
			return true;
		}

		// -- Probes' file ------------------------------------------------------------------------------------

		void Put32(std::string& out, uint32_t value)
		{
			out.append((const char*)&value, sizeof(value));
		}

		void Put64(std::string& out, uint64_t value)
		{
			out.append((const char*)&value, sizeof(value));
		}

		void PutBytes(std::string& out, const std::string& bytes)
		{
			Put32(out, (uint32_t)bytes.size());
			out += bytes;
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
	}

	bool Stream::ReadAt(uint64_t offset, void* out, size_t bytes)
	{
		if (offset < m_position)
			return false;
		// what comes before it is read and let go: an entry is read from its start onward
		std::vector<uint8_t> skipped;
		while (m_position < offset)
		{
			skipped.resize((size_t)std::min<uint64_t>(offset - m_position, 256 * 1024));
			if (Read(skipped.data(), skipped.size()) <= 0)
				return false;
		}
		for (size_t done = 0; done < bytes;)
		{
			const int64_t got = Read(static_cast<uint8_t*>(out) + done, bytes - done);
			if (got <= 0)
				return false;
			done += (size_t)got;
		}
		return true;
	}

	bool Visit(const std::string& path, const std::function<bool(const Entry&, Stream&)>& visit, std::string* error)
	{
		std::string why;
		const bool walked = ArchiveExtension(path) == ".zip" ? VisitZip(path, visit, why) : VisitOther(path, visit, why);
		if (!walked && error)
			*error = why;
		return walked;
	}

	Probes::Probes(std::string file) : m_file(std::move(file))
	{
		std::ifstream in(m_file, std::ios::binary);
		const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		Cursor cursor{data};
		if (cursor.Get<uint32_t>() != kProbesMagic || cursor.Get<uint32_t>() != kProbesVersion)
			return;
		const uint32_t count = cursor.Get<uint32_t>();
		for (uint32_t i = 0; i < count && cursor.ok; i++)
		{
			const std::string path = cursor.Bytes();
			Record record;
			record.size = cursor.Get<uint64_t>();
			record.time = cursor.Get<int64_t>();
			const uint32_t fields = cursor.Get<uint32_t>();
			for (uint32_t field = 0; field < fields && cursor.ok; field++)
				record.fields.push_back(cursor.Bytes());
			if (cursor.ok)
				m_known[path] = std::move(record);
		}
	}

	const std::vector<std::string>* Probes::Find(const std::string& path, uint64_t size, int64_t time) const
	{
		const auto known = m_known.find(path);
		return known != m_known.end() && known->second.size == size && known->second.time == time ? &known->second.fields : nullptr;
	}

	void Probes::Keep(const std::string& path, uint64_t size, int64_t time, std::vector<std::string> fields)
	{
		const auto known = m_known.find(path);
		if (known == m_known.end() || known->second.size != size || known->second.time != time || known->second.fields != fields)
			m_changed = true;
		m_seen[path] = {size, time, std::move(fields)};
	}

	void Probes::Save() const
	{
		if (!m_changed && m_seen.size() == m_known.size())
			return;
		std::string out;
		Put32(out, kProbesMagic);
		Put32(out, kProbesVersion);
		Put32(out, (uint32_t)m_seen.size());
		for (const auto& [path, record] : m_seen)
		{
			PutBytes(out, path);
			Put64(out, record.size);
			Put64(out, (uint64_t)record.time);
			Put32(out, (uint32_t)record.fields.size());
			for (const std::string& field : record.fields)
				PutBytes(out, field);
		}
		const std::string temporary = m_file + ".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			file.write(out.data(), (std::streamsize)out.size());
			if (!file)
				return;
		}
		std::error_code ec;
		fs::rename(temporary, m_file, ec);
	}

	bool Probes::Stamp(const std::string& path, uint64_t& size, int64_t& time)
	{
		struct stat info{};
		if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
			return false;
		size = (uint64_t)info.st_size;
		time = (int64_t)info.st_mtim.tv_sec * 1000000000 + info.st_mtim.tv_nsec;
		return true;
	}

	// -- unpacked copies ---------------------------------------------------------------------------------

	std::string UnpackedRoot()
	{
		return ps5paths::Cache() + "/archives";
	}

	namespace
	{
		// FNV-1a
		uint64_t Hash(const std::string& text)
		{
			uint64_t hash = 0xcbf29ce484222325ull;
			for (const char c : text)
				hash = (hash ^ (uint8_t)c) * 0x100000001b3ull;
			return hash;
		}

		// The folder a source's copy is kept in, named by what it is a copy of: the archive's path, size
		// and time, and its entry or its folder in it. Empty when the archive is not there
		std::string CopyFolder(const Source& source)
		{
			uint64_t size = 0;
			int64_t time = 0;
			if (!Probes::Stamp(source.archive, size, time))
				return {};
			const std::string what = source.archive + '\n' + std::to_string(size) + '\n' + std::to_string(time) + '\n' +
				(source.folder ? "folder\n" + source.member :
								 "rom\n" + std::to_string(source.index) + '\n' + source.member + '\n' + std::to_string(source.size));
			char name[17];
			std::snprintf(name, sizeof(name), "%016llx", (unsigned long long)Hash(what));
			return UnpackedRoot() + "/" + name;
		}

		// An archived ROM's name in its copy: the entry's own, without its folders; game.3ds for one whose
		// name libarchive dropped
		std::string RomName(const Source& source)
		{
			const size_t slash = source.member.find_last_of('/');
			const std::string name = slash == std::string::npos ? source.member : source.member.substr(slash + 1);
			return name.empty() || name == "." || name == ".." ? std::string("game.3ds") : name;
		}

		// What the emulator starts in a copy's folder: the ROM, or the folder itself (the title's code,
		// content and meta)
		std::string Target(const Source& source, const std::string& folder)
		{
			return source.folder ? folder : folder + "/" + RomName(source);
		}

		// A copy used now, for the oldest-first clean-up: its .used file written, with the copy's size in it
		// (UnpackedBytes reads it, rather than every file of a Wii U title's)
		void MarkUsed(const std::string& folder, uint64_t size)
		{
			const std::string file = folder + "/.used";
			const int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
			if (fd < 0)
				return;
			const std::string text = std::to_string(size);
			(void)!write(fd, text.data(), text.size());
			close(fd);
		}

		// When a copy was last used: its .used file's time, else its folder's
		int64_t LastUsed(const std::string& folder)
		{
			struct stat info{};
			if (stat((folder + "/.used").c_str(), &info) != 0 && stat(folder.c_str(), &info) != 0)
				return 0;
			return (int64_t)info.st_mtime;
		}

		uint64_t FreeBytes(const std::string& folder)
		{
			struct statvfs info{};
			if (statvfs(folder.c_str(), &info) != 0 || info.f_frsize == 0)
				return UINT64_MAX; // not known: the writes will say
			return (uint64_t)info.f_bavail * (uint64_t)info.f_frsize;
		}

		// A size as the menus write one: 2.4 GB, 640.5 MB
		std::string SizeText(uint64_t bytes)
		{
			if (bytes >= 1000000000ull)
				return ps5lang::TrFC("size", "{0} GB", ps5lang::Decimal(bytes / 1e9, 1));
			return ps5lang::TrFC("size", "{0} MB", ps5lang::Decimal(bytes / 1e6, 1));
		}

		// The copies in the cache, but the one given
		std::vector<std::string> Copies(const std::string& except)
		{
			std::vector<std::string> copies;
			std::error_code ec;
			for (const fs::directory_entry& entry : fs::directory_iterator(UnpackedRoot(), ec))
				if (entry.is_directory(ec) && entry.path().extension() != ".partial" && entry.path().string() != except)
					copies.push_back(entry.path().string());
			return copies;
		}

		// An entry's path in a title's copy, from its path under the title's folder: no part of it empty,
		// "." or ".." (a name in the archive cannot leave the copy's folder), and code, content and meta in
		// small letters, as Cemu looks for them. Empty when it is not one
		std::string Relative(const std::string& name)
		{
			std::string out;
			for (size_t at = 0, part = 0; at <= name.size(); part++)
			{
				const size_t slash = std::min(name.find('/', at), name.size());
				std::string piece = name.substr(at, slash - at);
				if (piece.empty() || piece == "." || piece == "..")
					return {};
				if (part == 0 && (Lower(piece) == "code" || Lower(piece) == "content" || Lower(piece) == "meta"))
					piece = Lower(piece);
				out += (out.empty() ? "" : "/") + piece;
				at = slash + 1;
			}
			return out;
		}

		enum class Failure
		{
			None,
			Read,	   // the archive (why: libzip's or libarchive's words)
			Write,	   // the drive (why: the C library's)
			Changed,   // the archive is not as the library read it
			Cancelled, // Progress::cancel
		};

		// The entry being visited into file: size bytes, as the archive said
		Failure CopyEntry(Stream& stream, const std::string& file, uint64_t size, Progress* progress, std::vector<uint8_t>& block, std::string& why)
		{
			const int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
			if (fd < 0)
			{
				why = std::strerror(errno);
				return Failure::Write;
			}
			Failure failure = Failure::None;
			uint64_t copied = 0;
			while (failure == Failure::None)
			{
				if (progress && progress->cancel)
				{
					failure = Failure::Cancelled;
					break;
				}
				const int64_t got = stream.Read(block.data(), block.size());
				if (got < 0)
				{
					why = stream.Error();
					failure = Failure::Read;
					break;
				}
				if (got == 0)
					break;
				if ((uint64_t)got > size - copied)
				{
					failure = Failure::Changed; // more than the archive's record of it says
					break;
				}
				for (int64_t written = 0; written < got;)
				{
					const ssize_t put = write(fd, block.data() + written, (size_t)(got - written));
					if (put < 0 && errno == EINTR)
						continue;
					if (put <= 0)
					{
						why = std::strerror(put < 0 ? errno : ENOSPC);
						failure = Failure::Write;
						break;
					}
					written += put;
				}
				if (failure != Failure::None)
					break;
				copied += (uint64_t)got;
				if (progress)
					progress->done += (uint64_t)got;
			}
			if (close(fd) != 0 && failure == Failure::None)
			{
				why = std::strerror(errno);
				failure = Failure::Write;
			}
			if (failure == Failure::None && copied != size)
				failure = Failure::Changed;
			return failure;
		}
	}

	std::string Unpacked(const Source& source)
	{
		const std::string folder = CopyFolder(source);
		if (folder.empty())
			return {};
		const std::string target = Target(source, folder);
		struct stat info{};
		if (stat(target.c_str(), &info) != 0 || (source.folder ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode)))
			return {};
		MarkUsed(folder, source.size);
		return target;
	}

	bool Unpack(const Source& source, Progress* progress, std::string& path, std::string& error)
	{
		path.clear();
		error.clear();
		const std::string folder = CopyFolder(source);
		if (folder.empty())
		{
			// tr: why an archived game could not start; {0} is why, in English (the archive's library's words)
			error = ps5lang::TrF("The archive could not be read: {0}", std::strerror(ENOENT));
			return false;
		}
		std::error_code ec;
		fs::create_directories(UnpackedRoot(), ec);
		// what an unpacking stopped part-way left (the app closed meanwhile), and a copy of this one that
		// is not whole: never taken for one
		std::vector<fs::path> stale;
		for (const fs::directory_entry& entry : fs::directory_iterator(UnpackedRoot(), ec))
			if (entry.path().extension() == ".partial")
				stale.push_back(entry.path());
		stale.push_back(folder);
		for (const fs::path& leftover : stale)
			fs::remove_all(leftover, ec);

		// room: the copies used longest ago removed first, while the drive lacks it
		const uint64_t needed = source.size + kSpare;
		for (uint64_t free = FreeBytes(UnpackedRoot()); free < needed; free = FreeBytes(UnpackedRoot()))
		{
			std::string oldest;
			int64_t oldestUsed = INT64_MAX;
			for (const std::string& copy : Copies(folder))
				if (const int64_t used = LastUsed(copy); used < oldestUsed)
				{
					oldest = copy;
					oldestUsed = used;
				}
			if (oldest.empty())
			{
				ps5log::Line("[archives] {} MiB free in {}, {} MiB needed", free >> 20, UnpackedRoot(), needed >> 20);
				// tr: why an archived game could not start; {0} and {1} are sizes, as in 2.4 GB
				error = ps5lang::TrF("Unpacking the game needs {0} of free space, and {1} is free.", SizeText(needed), SizeText(free));
				return false;
			}
			ps5log::Line("[archives] {} removed, the copy used longest ago, to make room", oldest);
			fs::remove_all(oldest, ec);
		}

		const std::string partial = folder + ".partial";
		if (mkdir(partial.c_str(), 0777) != 0)
		{
			ps5log::Line("[archives] {} could not be made: {}", partial, std::strerror(errno));
			// tr: why an archived game could not start; {0} is the folder the games unpacked from archives are kept in
			error = ps5lang::TrF("The game could not be written to {0}.", UnpackedRoot());
			return false;
		}
		if (progress)
		{
			progress->total = source.size;
			progress->done = 0;
		}
		ps5log::Line("[archives] unpacking {} of {} ({} MiB) into {}", source.folder ? "the folder \"" + source.member + "\"" : "\"" + source.member + "\"",
			source.archive, source.size >> 20, folder);
		const auto started = std::chrono::steady_clock::now();
		Failure failure = Failure::None;
		std::string why, walkError;
		bool found = false;
		uint64_t copied = 0;
		std::vector<uint8_t> block(kBlock);
		std::set<std::string> made; // the folders made in the copy
		const bool walked = Visit(source.archive, [&](const Entry& entry, Stream& stream) {
			if (!source.folder)
			{
				// the ROM: by its place, and the name and size it had when the library read it
				if (entry.index != source.index)
					return true;
				found = true;
				copied = entry.size;
				failure = entry.size != source.size || entry.name != source.member ?
					Failure::Changed :
					CopyEntry(stream, partial + "/" + RomName(source), entry.size, progress, block, why);
				return false;
			}
			// the title's folder: each of its files, where it is in it (a file libarchive gave no name is
			// none of them)
			if (entry.name.empty() || entry.name.size() <= source.member.size() || entry.name.compare(0, source.member.size(), source.member) != 0)
				return true;
			const std::string relative = Relative(entry.name.substr(source.member.size()));
			if (relative.empty())
			{
				why = "an unsafe path in the archive: " + entry.name;
				failure = Failure::Read;
				return false;
			}
			found = true;
			if (const size_t slash = relative.rfind('/'); slash != std::string::npos)
			{
				const std::string parent = partial + "/" + relative.substr(0, slash);
				if (made.insert(parent).second && !fs::create_directories(parent, ec) && ec)
				{
					why = ec.message();
					failure = Failure::Write;
					return false;
				}
			}
			failure = CopyEntry(stream, partial + "/" + relative, entry.size, progress, block, why);
			copied += entry.size;
			return failure == Failure::None;
		}, &walkError);
		if (failure == Failure::None && !walked)
		{
			why = walkError;
			failure = Failure::Read;
		}
		if (failure == Failure::None && (!found || copied != source.size))
			failure = Failure::Changed;
		if (failure == Failure::None && rename(partial.c_str(), folder.c_str()) != 0)
		{
			why = std::strerror(errno);
			failure = Failure::Write;
		}
		const long long seconds = (long long)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
		if (failure != Failure::None)
		{
			fs::remove_all(partial, ec);
			switch (failure)
			{
			case Failure::Read: error = ps5lang::TrF("The archive could not be read: {0}", why); break;
			case Failure::Write: error = ps5lang::TrF("The game could not be written to {0}.", UnpackedRoot()); break;
			case Failure::Changed:
				// tr: why an archived game could not start
				error = ps5lang::Tr("The archive has changed since the library read it: look for games again (Settings > Game files).");
				break;
			default: break; // stopped: nothing to say
			}
			ps5log::Line("[archives] {} after {} s: {}", failure == Failure::Cancelled ? "stopped" : "failed", seconds,
				failure == Failure::Cancelled ? "asked to" : why.empty() ? error : why);
			return false;
		}
		MarkUsed(folder, source.size);
		path = Target(source, folder);
		ps5log::Line("[archives] unpacked in {} s: {}", seconds, path);
		return true;
	}

	uint64_t UnpackedBytes()
	{
		uint64_t bytes = 0;
		for (const std::string& copy : Copies({}))
		{
			// the size its .used file says, else what its files add up to
			const std::string used = [&] {
				std::ifstream file(copy + "/.used");
				return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			}();
			char* end = nullptr;
			const unsigned long long size = std::strtoull(used.c_str(), &end, 10);
			if (!used.empty() && end && *end == '\0')
			{
				bytes += size;
				continue;
			}
			std::error_code ec;
			for (auto it = fs::recursive_directory_iterator(copy, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
				if (std::error_code sized; it->is_regular_file(sized))
					if (const uintmax_t file = it->file_size(sized); !sized)
						bytes += file;
		}
		return bytes;
	}

	int ClearUnpacked()
	{
		std::error_code ec;
		std::vector<fs::path> entries;
		for (const fs::directory_entry& entry : fs::directory_iterator(UnpackedRoot(), ec))
			entries.push_back(entry.path());
		int copies = 0;
		for (const fs::path& entry : entries)
		{
			const bool copy = entry.extension() != ".partial" && fs::is_directory(entry, ec);
			if (fs::remove_all(entry, ec) != (uintmax_t)-1 && !ec && copy)
				copies++;
		}
		ps5log::Line("[archives] {} unpacked copies removed from {}", copies, UnpackedRoot());
		return copies;
	}
}
