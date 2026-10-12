// SPDX-License-Identifier: GPL-3.0-or-later
// A PC test of port/ps5/netfs_files.cpp, the /net layer under the C library's file calls (docs/UI-REDESIGN.md
// 6.14): linked with the same --wrap list as the app (tools/test-netfs.sh), over a stand-in for app/netfs.cpp
// that serves /net/test/... from a local folder. It checks what Cemu, Azahar and melonDS do with a game's
// files: open, read, pread, seek, stat, a FILE's reads and seeks, lines, a directory's entries, C++ streams
// and std::filesystem, and that every other path still reaches the C library. Prints "ok" at the end.

#include "../../port/app/netfs.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// -- the stand-in share: /net/test is the folder in NETFS_ROOT --------------------------------------------

static std::string Local(const std::string& path)
{
	const std::string prefix = "/net/test";
	if (path.compare(0, prefix.size(), prefix) != 0)
		return {};
	return std::string(std::getenv("NETFS_ROOT")) + path.substr(prefix.size());
}

namespace ps5netfs
{
	bool stat(const std::string& path, bool* is_dir, std::uint64_t* size, std::int64_t* mtime)
	{
		const std::string local = Local(path);
		struct ::stat st{};
		if (local.empty() || ::stat(local.c_str(), &st) != 0) // a local path: the C library's own, past the layer
			return false;
		if (is_dir)
			*is_dir = S_ISDIR(st.st_mode);
		if (size)
			*size = (std::uint64_t)st.st_size;
		if (mtime)
			*mtime = st.st_mtime;
		return true;
	}

	bool list(const std::string& dir, std::vector<Entry>& out)
	{
		out.clear();
		const std::string local = Local(dir);
		std::error_code ec;
		if (local.empty() || !fs::is_directory(local, ec))
			return false;
		for (const auto& entry : fs::directory_iterator(local, ec))
			out.push_back({entry.path().filename().string(), entry.is_directory(ec)});
		return true;
	}

	struct Reader::Impl
	{
		std::FILE* file = nullptr;
		std::uint64_t size = 0;
	};
	Reader::Reader() : impl_(new Impl) {}
	Reader::~Reader() { close(); }
	bool Reader::open(const std::string& path)
	{
		close();
		bool dir = false;
		if (!ps5netfs::stat(path, &dir, &impl_->size) || dir)
			return false;
		impl_->file = std::fopen(Local(path).c_str(), "rb");
		return impl_->file != nullptr;
	}
	void Reader::close()
	{
		if (impl_->file)
			std::fclose(impl_->file);
		impl_->file = nullptr;
	}
	bool Reader::is_open() const { return impl_->file != nullptr; }
	std::uint64_t Reader::size() const { return impl_->size; }
	std::int64_t Reader::read_some(std::uint64_t offset, void* out, std::uint64_t n)
	{
		if (std::fseek(impl_->file, (long)offset, SEEK_SET) != 0)
			return -1;
		return (std::int64_t)std::fread(out, 1, n, impl_->file);
	}
	bool Reader::read(std::uint64_t offset, void* out, std::uint64_t n)
	{
		return read_some(offset, out, n) == (std::int64_t)n;
	}
}

// -- the checks --------------------------------------------------------------------------------------------

#define CHECK(condition)                                                                    \
	do                                                                                      \
	{                                                                                       \
		if (!(condition))                                                                   \
		{                                                                                   \
			std::fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition);   \
			std::exit(1);                                                                   \
		}                                                                                   \
	} while (0)

int main()
{
	const std::string root = std::getenv("NETFS_ROOT");
	// the share's files, written locally
	{
		std::ofstream bin(root + "/game.bin", std::ios::binary);
		for (int i = 0; i < 300000; i++)
			bin.put((char)(i * 7));
		std::ofstream text(root + "/meta.txt");
		text << "first line\nsecond line\nlast";
		fs::create_directories(root + "/folder/inner");
		std::ofstream(root + "/folder/a.nds") << "A";
		std::ofstream(root + "/folder/b.3ds") << "BB";
	}

	// descriptors
	int fd = open("/net/test/game.bin", O_RDONLY);
	CHECK(fd >= (1 << 29));
	unsigned char buffer[1000];
	CHECK(read(fd, buffer, 10) == 10 && buffer[3] == (unsigned char)21);
	CHECK(lseek(fd, 1000, SEEK_SET) == 1000);
	CHECK(read(fd, buffer, 1) == 1 && buffer[0] == (unsigned char)(1000 * 7));
	CHECK(pread(fd, buffer, 4, 299998) == 2); // past the end: what there is
	struct stat st{};
	CHECK(fstat(fd, &st) == 0 && st.st_size == 300000 && S_ISREG(st.st_mode));
	CHECK(lseek(fd, 0, SEEK_END) == 300000);
	CHECK(close(fd) == 0);
	CHECK(close(fd) == -1 && errno == EBADF);
	CHECK(open("/net/test/game.bin", O_RDWR) == -1 && errno == EROFS);
	CHECK(open("/net/test/none.bin", O_RDONLY) == -1 && errno == ENOENT);

	// stat, access
	CHECK(stat("/net/test/folder", &st) == 0 && S_ISDIR(st.st_mode));
	CHECK(stat("/net/test/folder/../game.bin", &st) == 0 && st.st_size == 300000);
	CHECK(stat("/net/test/nothing", &st) == -1);
	CHECK(access("/net/test/meta.txt", R_OK) == 0);
	CHECK(access("/net/test/meta.txt", W_OK) == -1 && errno == EROFS);

	// a FILE: reads, seeks, tell, lines, characters, the end
	std::FILE* file = std::fopen("/net/test/game.bin", "rb");
	CHECK(file);
	CHECK(std::fread(buffer, 1, 1000, file) == 1000 && buffer[999] == (unsigned char)(999 * 7));
	CHECK(std::ftell(file) == 1000);
	CHECK(std::fseek(file, -10, SEEK_END) == 0 && std::ftell(file) == 299990);
	CHECK(std::fread(buffer, 4, 5, file) == 2 && std::feof(file)); // 10 bytes left: two items of 4
	std::clearerr(file);
	CHECK(!std::feof(file));
	std::rewind(file);
	CHECK(std::fgetc(file) == 0 && std::fgetc(file) == 7);
	CHECK(std::ungetc(7, file) == 7 && std::fgetc(file) == 7);
	CHECK(fseeko(file, 200000, SEEK_SET) == 0 && ftello(file) == 200000);
	CHECK(fstat(fileno(file), &st) == 0 && st.st_size == 300000);
	CHECK(std::fwrite(buffer, 1, 1, file) == 0 && std::ferror(file));
	CHECK(std::fflush(file) == 0);
	CHECK(std::fclose(file) == 0);
	file = std::fopen("/net/test/meta.txt", "r");
	char line[64];
	CHECK(std::fgets(line, sizeof(line), file) && std::string(line) == "first line\n");
	CHECK(std::fgets(line, sizeof(line), file) && std::string(line) == "second line\n");
	CHECK(std::fgets(line, sizeof(line), file) && std::string(line) == "last");
	CHECK(!std::fgets(line, sizeof(line), file));
	CHECK(std::fclose(file) == 0);
	CHECK(!std::fopen("/net/test/meta.txt", "w") && errno == EROFS);

	// a directory
	DIR* dir = opendir("/net/test/folder");
	CHECK(dir);
	std::set<std::string> names;
	while (dirent* entry = readdir(dir))
		names.insert(std::string(entry->d_name) + (entry->d_type == DT_DIR ? "/" : ""));
	CHECK(closedir(dir) == 0);
	CHECK(names == (std::set<std::string>{"a.nds", "b.3ds", "inner/"}));

	// C++: streams and std::filesystem, as the emulators read their files. The app's libc++ opens a stream
	// with fopen and reads it with fread and fseeko, all wrapped; a PC's libstdc++ uses fopen64 and the
	// like, which the app never calls, so its streams are left out here.
#ifdef _LIBCPP_VERSION
	{
		std::ifstream in("/net/test/game.bin", std::ios::binary);
		CHECK(in.good());
		in.seekg(299000);
		std::vector<char> data(1000);
		in.read(data.data(), 1000);
		CHECK(in.gcount() == 1000 && (unsigned char)data[0] == (unsigned char)(299000 * 7));
		std::ifstream lines("/net/test/meta.txt");
		std::string text((std::istreambuf_iterator<char>(lines)), std::istreambuf_iterator<char>());
		CHECK(text == "first line\nsecond line\nlast");
	}
#else
	std::puts("(C++ streams not checked: libstdc++ opens files with fopen64, the app's libc++ with fopen)");
#endif
	std::error_code ec;
	CHECK(fs::is_directory("/net/test/folder", ec) && fs::file_size("/net/test/game.bin", ec) == 300000);
	// a folder's tree, as a library scan walks it: libc++ opens each folder by its path; a PC's libstdc++
	// goes down with openat on the folder's descriptor, which the app never does, so here by path
	int count = 0;
	std::vector<std::string> folders{"/net/test/folder"};
	while (!folders.empty())
	{
		const std::string folder = folders.back();
		folders.pop_back();
		DIR* d = opendir(folder.c_str());
		CHECK(d);
		while (dirent* entry = readdir(d))
		{
			if (entry->d_type == DT_DIR)
				folders.push_back(folder + "/" + entry->d_name);
			else
				count++;
		}
		closedir(d);
	}
	CHECK(count == 2);
#ifdef _LIBCPP_VERSION
	count = 0;
	for (const auto& entry : fs::recursive_directory_iterator("/net/test/folder", ec))
		count += entry.is_regular_file(ec);
	CHECK(count == 2);
#endif

	// everything else is the C library's, as before
	const std::string local = root + "/local.txt";
	{
		std::FILE* out = std::fopen(local.c_str(), "w");
		CHECK(out && std::fputs("local", out) >= 0 && std::fclose(out) == 0);
		std::FILE* in = std::fopen(local.c_str(), "r");
		CHECK(in && std::fgets(line, sizeof(line), in) && std::string(line) == "local" && std::fclose(in) == 0);
		DIR* d = opendir(root.c_str());
		CHECK(d && readdir(d) && closedir(d) == 0);
		CHECK(access(local.c_str(), R_OK) == 0);
	}
	std::puts("ok");
	return 0;
}
