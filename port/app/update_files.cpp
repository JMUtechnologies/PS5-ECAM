// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_files.h"
#include "lang.h"
#include "paths.h"

#include <zip.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

namespace ps5update
{
	void MakeFolders(const fs::path& under, const std::vector<fs::path>& files)
	{
		std::vector<fs::path> folders;
		for (const fs::path& file : files)
			if (file.has_parent_path())
				folders.push_back(file.parent_path());
		std::sort(folders.begin(), folders.end());
		folders.erase(std::unique(folders.begin(), folders.end()), folders.end());
		std::error_code ec;
		for (const fs::path& folder : folders)
			if (!fs::exists(under / folder, ec))
			{
				fs::create_directories(under / folder, ec);
				fs::permissions(under / folder, fs::perms::all, ec);
			}
	}

	void RemoveAll(const fs::path& folder)
	{
		std::error_code ec;
		std::vector<fs::path> files;
		for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
			if (!it->is_directory(ec))
				files.push_back(it->path());
		InParallel(files.size(), [&](int, size_t i) {
			std::error_code ignored;
			fs::remove(files[i], ignored);
			return true;
		});
		fs::remove_all(folder, ec);
	}

	bool Unpack(const fs::path& archive, const fs::path& folder, std::vector<fs::path>& files, std::string& error, const std::atomic<bool>& stop)
	{
		// a handle for each worker, libzip's being for one thread at a time
		struct Handle
		{
			zip_t* zip = nullptr;
			~Handle()
			{
				if (zip)
					zip_close(zip);
			}
		};
		std::array<Handle, kWorkers> handles;
		for (Handle& handle : handles)
		{
			int code = 0;
			if (!(handle.zip = zip_open(archive.c_str(), ZIP_RDONLY, &code)))
			{
				error = ps5lang::TrF("the download is not a ZIP ({0})", code);
				return false;
			}
		}
		std::vector<zip_uint64_t> indices; // each of files' entry in the archive
		const zip_int64_t entries = zip_get_num_entries(handles[0].zip, 0);
		for (zip_int64_t i = 0; i < entries; i++)
		{
			const char* name = zip_get_name(handles[0].zip, (zip_uint64_t)i, 0);
			if (!name)
				continue;
			std::string entry = name;
			// nothing outside the folder, whatever the archive says
			if (entry.empty() || entry[0] == '/' || entry.find("..") != std::string::npos || entry.find('\\') != std::string::npos)
				continue;
			if (entry.rfind(ps5paths::kTitleId + std::string("/"), 0) == 0)
				entry.erase(0, std::strlen(ps5paths::kTitleId) + 1);
			if (entry.empty() || entry.back() == '/')
				continue;
			indices.push_back((zip_uint64_t)i);
			files.push_back(entry);
		}
		if (std::find(files.begin(), files.end(), fs::path("eboot.bin")) == files.end())
		{
			error = ps5lang::Tr("the download has no eboot.bin");
			return false;
		}
		MakeFolders(folder, files);
		std::mutex failure; // error, from the first worker to fail
		std::array<std::vector<char>, kWorkers> buffers;
		const bool ok = InParallel(files.size(), [&](int worker, size_t i) {
			if (stop)
				return false;
			const fs::path path = folder / files[i];
			std::vector<char>& buffer = buffers[worker];
			buffer.resize(1 << 16);
			zip_file_t* in = zip_fopen_index(handles[worker].zip, indices[i], 0);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			const bool opened = in && out;
			for (zip_int64_t read; opened && (read = zip_fread(in, buffer.data(), buffer.size())) > 0;)
				out.write(buffer.data(), (std::streamsize)read);
			if (in)
				zip_fclose(in);
			out.close();
			if (!opened || !out)
			{
				std::lock_guard lock(failure);
				if (error.empty())
					error = !opened ? ps5lang::TrF("{0} could not be unpacked", files[i].string()) :
									  ps5lang::TrF("{0} could not be written (is /data full?)", files[i].string());
				return false;
			}
			// readable, writable and runnable by all, as the app's files are when copied over
			std::error_code ec;
			fs::permissions(path, fs::perms::all, ec);
			return true;
		});
		if (!ok && error.empty())
			error = ps5lang::Tr("stopped for a game");
		return ok;
	}
}
