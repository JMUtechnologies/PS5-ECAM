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
// the layer's own, kept in a list; a FILE is the C library's, made with funopen (fopencookie on a PC,
// for the test in tools/tests/test-netfs.sh) over a reader.

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
	int __real_access(const char* path, int mode);
	DIR* __real_opendir(const char* path);
	struct dirent* __real_readdir(DIR* dir);
	int __real_closedir(DIR* dir);
	char* __real_realpath(const char* path, char* resolved);
	FILE* __real_fopen(const char* path, const char* mode);
}

namespace
{
	constexpr int kFirstFd = 1 << 29;

	// An open /net file: its reader and, for read and lseek, where it is
	struct File
	{
		std::mutex m;
		ps5netfs::Reader reader;
		std::uint64_t position = 0;
		std::string path;
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

	int __wrap_access(const char* path, int mode)
	{
		if (!Net(path))
			return __real_access(path, mode);
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
			return __real_opendir(path);
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
			return __real_readdir(handle);
		{
			std::lock_guard lock(s_mutex);
			if (!s_dirs.count(handle))
				return __real_readdir(handle);
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
		return __real_closedir(handle);
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
		File* file = new File; // the FILE's own, which its close deletes
		if (!OpenNet(*file, path))
		{
			delete file;
			return nullptr;
		}
#ifdef __linux__
		cookie_io_functions_t calls{};
		calls.read = [](void* cookie, char* buffer, size_t size) -> ssize_t { return CookieRead(static_cast<File*>(cookie), buffer, size); };
		calls.seek = [](void* cookie, off64_t* offset, int whence) -> int {
			const std::int64_t at = CookieSeek(static_cast<File*>(cookie), *offset, whence);
			if (at < 0)
				return -1;
			*offset = at;
			return 0;
		};
		calls.close = [](void* cookie) -> int {
			delete static_cast<File*>(cookie);
			return 0;
		};
		FILE* stream = fopencookie(file, "r", calls);
#else
		FILE* stream = funopen(
			file, [](void* cookie, char* buffer, int size) -> int { return (int)CookieRead(static_cast<File*>(cookie), buffer, (size_t)size); },
			nullptr,
			[](void* cookie, fpos_t offset, int whence) -> fpos_t { return (fpos_t)CookieSeek(static_cast<File*>(cookie), (std::int64_t)offset, whence); },
			[](void* cookie) -> int {
				delete static_cast<File*>(cookie);
				return 0;
			});
#endif
		if (!stream)
			delete file;
		return stream;
	}
}
