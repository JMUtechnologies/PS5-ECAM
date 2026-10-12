// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: /net paths for the C library's file calls (docs/UI-REDESIGN.md 6.14). The app is linked
// with -Wl,--wrap for the calls below (tools/link.sh), so Cemu's title scan and its .wua, .wud and
// folder reads, Azahar's files, melonDS's ROM and the launcher's folder browser reach a network share
// by its /net/<name>/... path with no change to them: a call on such a path, or on a descriptor, a
// directory stream or a FILE opened from one, is answered by app/netfs.cpp; every other goes to the C
// library's own call (__real_...), after one comparison. Read-only: opening one to write fails with
// EROFS.
//
// The descriptors are numbers from kFirstFd up, far above any the kernel gives; a directory stream is
// the layer's own, kept in a list. A FILE cannot be the C library's over a reader: no module gives a
// title funopen or fopencookie (PS5_PayloadSDK's ps5platform/libc.h). So a /net FILE is a real one on
// an empty pipe, which only names it, and the stdio calls below answer for it from the reader; any
// other stdio call on it reads the empty pipe (the end of the file) rather than anything worse.
// fclose and fflush are wrapped by the platform layer already (its open_memstream): tools/link.sh
// renames those of its wraps to ps5_memstream_fclose and ps5_memstream_fflush, called here for every
// other FILE.

#include "../app/netfs.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

extern "C"
{
	int __real_open(const char* path, int flags, ...);
	ssize_t __real_read(int fd, void* buffer, size_t size);
	ssize_t __real_pread(int fd, void* buffer, size_t size, off_t offset);
	off_t __real_lseek(int fd, off_t offset, int whence);
	int __real_close(int fd);
	int __real_fstat(int fd, struct stat* st);
	int __real_stat(const char* path, struct stat* st);
	int __real_lstat(const char* path, struct stat* st);
	// access and the directory calls: the platform layer binds these names to its own (PS5_Vulkan's
	// radv-link.sh, --defsym access=ps5_access...), and a --defsym wins over a --wrap. So in the app's
	// link tools/link.sh binds them to ps5cemu_access and the like below, which call the platform's; in
	// a link check (no platform layer) and on a PC they are wrapped, and the C library's are __real_.
	// Weak: each link has one or the other.
	int __real_access(const char* path, int mode) __attribute__((weak));
	DIR* __real_opendir(const char* path) __attribute__((weak));
	struct dirent* __real_readdir(DIR* dir) __attribute__((weak));
	int __real_closedir(DIR* dir) __attribute__((weak));
	int ps5_access(const char* path, int mode) __attribute__((weak));
	DIR* ps5_opendir(const char* path) __attribute__((weak));
	struct dirent* ps5_readdir(DIR* dir) __attribute__((weak));
	int ps5_closedir(DIR* dir) __attribute__((weak));
	char* __real_realpath(const char* path, char* resolved);
	FILE* __real_fopen(const char* path, const char* mode);
	int __real_fclose(FILE* stream);
	int __real_fflush(FILE* stream);
	size_t __real_fread(void* buffer, size_t size, size_t count, FILE* stream);
	size_t __real_fwrite(const void* buffer, size_t size, size_t count, FILE* stream);
	int __real_fseek(FILE* stream, long offset, int whence);
	int __real_fseeko(FILE* stream, off_t offset, int whence);
	long __real_ftell(FILE* stream);
	off_t __real_ftello(FILE* stream);
	void __real_rewind(FILE* stream);
	int __real_feof(FILE* stream);
	int __real_ferror(FILE* stream);
	void __real_clearerr(FILE* stream);
	int __real_fgetc(FILE* stream);
	int __real_getc(FILE* stream);
	int __real_ungetc(int c, FILE* stream);
	char* __real_fgets(char* buffer, int size, FILE* stream);
	int __real_fileno(FILE* stream);
	// the platform layer's own fclose and fflush wraps, renamed by tools/link.sh (absent in a link
	// check, and on a PC)
	int ps5_memstream_fclose(FILE* stream) __attribute__((weak));
	int ps5_memstream_fflush(FILE* stream) __attribute__((weak));
}

namespace
{
	constexpr int kFirstFd = 1 << 29;

	// An open /net file: its reader and, for read and lseek, where it is; as a FILE, also its flags,
	// a character put back, and the pipe that names it
	struct File
	{
		std::mutex m;
		ps5netfs::Reader reader;
		std::uint64_t position = 0;
		std::string path;
		bool eof = false, error = false;
		int pushed = -1;		 // ungetc's
		int fd = -1;			 // fileno's: a descriptor of this layer's for the same file
		int pipeWrite = -1;		 // the pipe's other end
	};

	struct Dir
	{
		std::vector<ps5netfs::Entry> entries;
		std::size_t next = 0;
		struct dirent entry{};
	};

	std::mutex s_mutex;
	std::map<int, std::shared_ptr<File>> s_files;
	int s_nextFd = kFirstFd;
	std::set<DIR*> s_dirs;
	std::atomic<int> s_dirCount{0}; // so readdir looks in s_dirs only while one is open
	std::map<FILE*, std::shared_ptr<File>> s_streams;
	std::atomic<int> s_streamCount{0}; // so a stdio call looks in s_streams only while one is open

	std::shared_ptr<File> Stream(FILE* stream)
	{
		if (s_streamCount.load(std::memory_order_relaxed) == 0)
			return nullptr;
		std::lock_guard lock(s_mutex);
		const auto it = s_streams.find(stream);
		return it != s_streams.end() ? it->second : nullptr;
	}

	// n bytes into buffer from where the file is (a put-back character first); how many
	size_t StreamRead(File& file, char* buffer, size_t n)
	{
		size_t got = 0;
		if (n > 0 && file.pushed >= 0)
		{
			buffer[got++] = (char)file.pushed;
			file.pushed = -1;
			file.position++;
		}
		while (got < n)
		{
			const std::int64_t read = file.reader.read_some(file.position, buffer + got, n - got);
			if (read < 0)
			{
				file.error = true;
				break;
			}
			if (read == 0)
			{
				file.eof = true;
				break;
			}
			got += (size_t)read;
			file.position += (std::uint64_t)read;
		}
		return got;
	}

	int StreamSeek(File& file, std::int64_t offset, int whence)
	{
		const std::int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (std::int64_t)file.position : (std::int64_t)file.reader.size();
		if ((whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) || base + offset < 0)
		{
			errno = EINVAL;
			return -1;
		}
		file.position = (std::uint64_t)(base + offset);
		file.eof = false;
		file.pushed = -1;
		return 0;
	}

	bool Net(const char* path)
	{
		return path && std::strncmp(path, "/net", 4) == 0 && (path[4] == 0 || path[4] == '/');
	}

	// The path with its ".", ".." and doubled slashes taken out, as the share is asked for it
	std::string Normal(const char* path)
	{
		std::vector<std::string> parts;
		for (const char* at = path; *at;)
		{
			const char* end = std::strchr(at, '/');
			const std::string part(at, end ? end : at + std::strlen(at));
			if (part == "..")
			{
				if (parts.size() > 1) // never above /net
					parts.pop_back();
			}
			else if (!part.empty() && part != ".")
				parts.push_back(part);
			at = end ? end + 1 : at + part.size();
		}
		std::string out;
		for (const std::string& part : parts)
			out += "/" + part;
		return out.empty() ? "/" : out;
	}

	std::shared_ptr<File> Find(int fd)
	{
		std::lock_guard lock(s_mutex);
		const auto it = s_files.find(fd);
		return it != s_files.end() ? it->second : nullptr;
	}

	void Fill(struct stat* st, bool isDir, std::uint64_t size, std::int64_t mtime)
	{
		std::memset(st, 0, sizeof(*st));
		st->st_mode = isDir ? (S_IFDIR | 0555) : (S_IFREG | 0444);
		st->st_nlink = 1;
		st->st_size = (off_t)size;
		st->st_blksize = 64 * 1024;
		st->st_blocks = (blkcnt_t)((size + 511) / 512);
		st->st_mtime = (time_t)mtime;
		st->st_atime = (time_t)mtime;
		st->st_ctime = (time_t)mtime;
	}

	int StatNet(const char* path, struct stat* st)
	{
		bool isDir = false;
		std::uint64_t size = 0;
		std::int64_t mtime = 0;
		if (!ps5netfs::stat(Normal(path), &isDir, &size, &mtime))
		{
			errno = ENOENT;
			return -1;
		}
		Fill(st, isDir, size, mtime);
		return 0;
	}

	bool Writes(int flags)
	{
		return (flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC | O_APPEND));
	}

	bool OpenNet(File& file, const char* path)
	{
		file.path = Normal(path);
		if (file.reader.open(file.path))
			return true;
		bool isDir = false;
		errno = ps5netfs::stat(file.path, &isDir) ? (isDir ? EISDIR : EIO) : ENOENT;
		return false;
	}

	// a FILE over a reader: funopen's (FreeBSD) and fopencookie's (glibc) calls
	ssize_t CookieRead(File* file, char* buffer, size_t size)
	{
		std::lock_guard lock(file->m);
		const std::int64_t got = file->reader.read_some(file->position, buffer, size);
		if (got < 0)
		{
			errno = EIO;
			return -1;
		}
		file->position += (std::uint64_t)got;
		return (ssize_t)got;
	}

	std::int64_t CookieSeek(File* file, std::int64_t offset, int whence)
	{
		std::lock_guard lock(file->m);
		const std::int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (std::int64_t)file->position : (std::int64_t)file->reader.size();
		if (base + offset < 0)
		{
			errno = EINVAL;
			return -1;
		}
		file->position = (std::uint64_t)(base + offset);
		return (std::int64_t)file->position;
	}
}

extern "C"
{
	int __wrap_open(const char* path, int flags, ...)
	{
		mode_t mode = 0;
		if (flags & O_CREAT)
		{
			va_list args;
			va_start(args, flags);
			mode = (mode_t)va_arg(args, int);
			va_end(args);
		}
		if (!Net(path))
			return __real_open(path, flags, mode);
		if (Writes(flags))
		{
			errno = EROFS;
			return -1;
		}
		const auto file = std::make_shared<File>();
		if (!OpenNet(*file, path))
			return -1;
		std::lock_guard lock(s_mutex);
		const int fd = s_nextFd++;
		s_files[fd] = file;
		return fd;
	}

	ssize_t __wrap_read(int fd, void* buffer, size_t size)
	{
		if (fd < kFirstFd)
			return __real_read(fd, buffer, size);
		const std::shared_ptr<File> file = Find(fd);
		if (!file)
		{
			errno = EBADF;
			return -1;
		}
		return CookieRead(file.get(), (char*)buffer, size);
	}

	ssize_t __wrap_pread(int fd, void* buffer, size_t size, off_t offset)
	{
		if (fd < kFirstFd)
			return __real_pread(fd, buffer, size, offset);
		const std::shared_ptr<File> file = Find(fd);
		if (!file || offset < 0)
		{
			errno = !file ? EBADF : EINVAL;
			return -1;
		}
		std::lock_guard lock(file->m);
		const std::int64_t got = file->reader.read_some((std::uint64_t)offset, buffer, size);
		if (got < 0)
			errno = EIO;
		return (ssize_t)got;
	}

	off_t __wrap_lseek(int fd, off_t offset, int whence)
	{
		if (fd < kFirstFd)
			return __real_lseek(fd, offset, whence);
		const std::shared_ptr<File> file = Find(fd);
		if (!file)
		{
			errno = EBADF;
			return -1;
		}
		return (off_t)CookieSeek(file.get(), offset, whence);
	}

	int __wrap_close(int fd)
	{
		if (fd < kFirstFd)
			return __real_close(fd);
		std::lock_guard lock(s_mutex);
		if (!s_files.erase(fd))
		{
			errno = EBADF;
			return -1;
		}
		return 0;
	}

	int __wrap_fstat(int fd, struct stat* st)
	{
		if (fd < kFirstFd)
			return __real_fstat(fd, st);
		const std::shared_ptr<File> file = Find(fd);
		if (!file)
		{
			errno = EBADF;
			return -1;
		}
		std::int64_t mtime = 0;
		ps5netfs::stat(file->path, nullptr, nullptr, &mtime);
		Fill(st, false, file->reader.size(), mtime);
		return 0;
	}

	int __wrap_stat(const char* path, struct stat* st)
	{
		return Net(path) ? StatNet(path, st) : __real_stat(path, st);
	}

	int __wrap_lstat(const char* path, struct stat* st)
	{
		return Net(path) ? StatNet(path, st) : __real_lstat(path, st); // a share has no links to follow
	}

	// the platform layer's (the app), else the C library's (a link check, a PC)
	static int RealAccess(const char* path, int mode)
	{
		return ps5_access ? ps5_access(path, mode) : __real_access(path, mode);
	}
	static DIR* RealOpendir(const char* path)
	{
		return ps5_opendir ? ps5_opendir(path) : __real_opendir(path);
	}
	static struct dirent* RealReaddir(DIR* dir)
	{
		return ps5_readdir ? ps5_readdir(dir) : __real_readdir(dir);
	}
	static int RealClosedir(DIR* dir)
	{
		return ps5_closedir ? ps5_closedir(dir) : __real_closedir(dir);
	}

	int __wrap_access(const char* path, int mode)
	{
		if (!Net(path))
			return RealAccess(path, mode);
		if (!ps5netfs::stat(Normal(path)))
		{
			errno = ENOENT;
			return -1;
		}
		if (mode & W_OK)
		{
			errno = EROFS;
			return -1;
		}
		return 0;
	}

	DIR* __wrap_opendir(const char* path)
	{
		if (!Net(path))
			return RealOpendir(path);
		auto* dir = new Dir;
		if (!ps5netfs::list(Normal(path), dir->entries))
		{
			delete dir;
			bool isDir = false;
			errno = ps5netfs::stat(Normal(path), &isDir) && !isDir ? ENOTDIR : ENOENT;
			return nullptr;
		}
		DIR* handle = reinterpret_cast<DIR*>(dir);
		std::lock_guard lock(s_mutex);
		s_dirs.insert(handle);
		s_dirCount++;
		return handle;
	}

	struct dirent* __wrap_readdir(DIR* handle)
	{
		if (s_dirCount.load(std::memory_order_relaxed) == 0)
			return RealReaddir(handle);
		{
			std::lock_guard lock(s_mutex);
			if (!s_dirs.count(handle))
				return RealReaddir(handle);
		}
		Dir* dir = reinterpret_cast<Dir*>(handle);
		if (dir->next >= dir->entries.size())
			return nullptr; // the end, errno untouched
		const ps5netfs::Entry& entry = dir->entries[dir->next++];
		struct dirent& out = dir->entry;
		std::memset(&out, 0, sizeof(out));
		out.d_ino = (ino_t)dir->next; // any number but 0, which marks an empty slot
		out.d_type = entry.is_dir ? DT_DIR : DT_REG;
		out.d_reclen = sizeof(out);
		std::snprintf(out.d_name, sizeof(out.d_name), "%s", entry.name.c_str());
#ifdef __FreeBSD__
		out.d_namlen = (decltype(out.d_namlen))std::strlen(out.d_name);
#endif
		return &out;
	}

	int __wrap_closedir(DIR* handle)
	{
		if (s_dirCount.load(std::memory_order_relaxed) > 0)
		{
			std::lock_guard lock(s_mutex);
			if (s_dirs.erase(handle))
			{
				s_dirCount--;
				delete reinterpret_cast<Dir*>(handle);
				return 0;
			}
		}
		return RealClosedir(handle);
	}

	// the names tools/link.sh binds access, opendir, readdir and closedir to in the app (see above)
	int ps5cemu_access(const char* path, int mode)
	{
		return __wrap_access(path, mode);
	}
	DIR* ps5cemu_opendir(const char* path)
	{
		return __wrap_opendir(path);
	}
	struct dirent* ps5cemu_readdir(DIR* dir)
	{
		return __wrap_readdir(dir);
	}
	int ps5cemu_closedir(DIR* dir)
	{
		return __wrap_closedir(dir);
	}

	char* __wrap_realpath(const char* path, char* resolved)
	{
		if (!Net(path))
			return __real_realpath(path, resolved);
		if (!ps5netfs::stat(Normal(path)))
		{
			errno = ENOENT;
			return nullptr;
		}
		const std::string normal = Normal(path);
		if (normal.size() >= PATH_MAX)
		{
			errno = ENAMETOOLONG;
			return nullptr;
		}
		char* out = resolved ? resolved : static_cast<char*>(std::malloc(PATH_MAX));
		if (!out)
		{
			errno = ENOMEM;
			return nullptr;
		}
		std::memcpy(out, normal.c_str(), normal.size() + 1);
		return out;
	}

	FILE* __wrap_fopen(const char* path, const char* mode)
	{
		if (!Net(path))
			return __real_fopen(path, mode);
		if (std::strpbrk(mode, "wa+"))
		{
			errno = EROFS;
			return nullptr;
		}
		const auto file = std::make_shared<File>();
		if (!OpenNet(*file, path))
			return nullptr;
		// the FILE that names it: a real one, on an empty pipe
		int ends[2];
		if (pipe(ends) != 0)
			return nullptr;
		FILE* stream = fdopen(ends[0], "r");
		if (!stream)
		{
			__real_close(ends[0]);
			__real_close(ends[1]);
			return nullptr;
		}
		file->pipeWrite = ends[1];
		std::lock_guard lock(s_mutex);
		file->fd = s_nextFd++;
		s_files[file->fd] = file; // fileno's descriptor, for fstat
		s_streams[stream] = file;
		s_streamCount++;
		return stream;
	}

	int __wrap_fclose(FILE* stream)
	{
		if (const auto file = Stream(stream))
		{
			{
				std::lock_guard lock(s_mutex);
				s_streams.erase(stream);
				s_streamCount--;
				s_files.erase(file->fd);
			}
			__real_fclose(stream);
			__real_close(file->pipeWrite);
			return 0;
		}
		return ps5_memstream_fclose ? ps5_memstream_fclose(stream) : __real_fclose(stream);
	}

	int __wrap_fflush(FILE* stream)
	{
		if (stream && Stream(stream))
			return 0;
		return ps5_memstream_fflush ? ps5_memstream_fflush(stream) : __real_fflush(stream);
	}

	size_t __wrap_fread(void* buffer, size_t size, size_t count, FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_fread(buffer, size, count, stream);
		if (size == 0 || count == 0)
			return 0;
		std::lock_guard lock(file->m);
		return StreamRead(*file, static_cast<char*>(buffer), size * count) / size;
	}

	size_t __wrap_fwrite(const void* buffer, size_t size, size_t count, FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_fwrite(buffer, size, count, stream);
		std::lock_guard lock(file->m);
		file->error = true;
		errno = EBADF; // read only
		return 0;
	}

	int __wrap_fseeko(FILE* stream, off_t offset, int whence)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_fseeko(stream, offset, whence);
		std::lock_guard lock(file->m);
		return StreamSeek(*file, (std::int64_t)offset, whence);
	}

	int __wrap_fseek(FILE* stream, long offset, int whence)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_fseek(stream, offset, whence);
		std::lock_guard lock(file->m);
		return StreamSeek(*file, (std::int64_t)offset, whence);
	}

	off_t __wrap_ftello(FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_ftello(stream);
		std::lock_guard lock(file->m);
		return (off_t)file->position;
	}

	long __wrap_ftell(FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_ftell(stream);
		std::lock_guard lock(file->m);
		return (long)file->position;
	}

	void __wrap_rewind(FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_rewind(stream);
		std::lock_guard lock(file->m);
		StreamSeek(*file, 0, SEEK_SET);
		file->error = false;
	}

	int __wrap_feof(FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_feof(stream);
		std::lock_guard lock(file->m);
		return file->eof ? 1 : 0;
	}

	int __wrap_ferror(FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_ferror(stream);
		std::lock_guard lock(file->m);
		return file->error ? 1 : 0;
	}

	void __wrap_clearerr(FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_clearerr(stream);
		std::lock_guard lock(file->m);
		file->eof = file->error = false;
	}

	int __wrap_fgetc(FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_fgetc(stream);
		std::lock_guard lock(file->m);
		char c = 0;
		return StreamRead(*file, &c, 1) == 1 ? (unsigned char)c : EOF;
	}

	int __wrap_getc(FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_getc(stream);
		std::lock_guard lock(file->m);
		char c = 0;
		return StreamRead(*file, &c, 1) == 1 ? (unsigned char)c : EOF;
	}

	int __wrap_ungetc(int c, FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_ungetc(c, stream);
		std::lock_guard lock(file->m);
		if (c == EOF || file->pushed >= 0 || file->position == 0)
			return EOF;
		file->pushed = (unsigned char)c;
		file->position--;
		file->eof = false;
		return (unsigned char)c;
	}

	char* __wrap_fgets(char* buffer, int size, FILE* stream)
	{
		const auto file = Stream(stream);
		if (!file)
			return __real_fgets(buffer, size, stream);
		if (size <= 0)
			return nullptr;
		std::lock_guard lock(file->m);
		int n = 0;
		while (n < size - 1)
		{
			char c = 0;
			if (StreamRead(*file, &c, 1) != 1)
				break;
			buffer[n++] = c;
			if (c == '\n')
				break;
		}
		if (n == 0)
			return nullptr;
		buffer[n] = 0;
		return buffer;
	}

	int __wrap_fileno(FILE* stream)
	{
		const auto file = Stream(stream);
		return file ? file->fd : __real_fileno(stream);
	}
}
