// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: every side's saves to a USB drive and back (saves.h).

#include "saves.h"
#include "lang.h"
#include "paths.h"
#include "../ps5/log.h"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace ps5saves
{
	namespace
	{
		constexpr const char* kFolder = "PS5CEMU-HAR-saves";

		// What a backup holds: its folder in the backup, where it comes from, and which of the files
		// there are saves (empty: all)
		struct Set
		{
			const char* name;
			std::string source;
			bool (*keep)(const std::string& relative);
		};

		// in azahar/sdmc/Nintendo 3DS: the games' saves (title/<high>/<low>/data) and extra data, never
		// the games and updates installed beside them (title/.../content)
		bool SdmcSave(const std::string& relative)
		{
			const std::string path = "/" + relative + "/";
			return (path.find("/title/") != std::string::npos && path.find("/data/") != std::string::npos) ||
				path.find("/extdata/") != std::string::npos;
		}

		std::vector<Set> Sets()
		{
			const std::string root = PS5CEMU_DATA;
			return {
				{"wiiu/save", root + "/mlc01/usr/save", nullptr},
				{"3ds/sdmc", root + "/azahar/sdmc/Nintendo 3DS", SdmcSave},
				{"3ds/nand", root + "/azahar/nand/data", nullptr},
				{"3ds/states", root + "/azahar/states", nullptr},
				{"ds/saves", root + "/melonds/saves", nullptr},
				{"ds/states", root + "/melonds/states", nullptr},
			};
		}

		struct File
		{
			fs::path from, to;
			uint64_t size;
		};

		std::mutex s_mutex;
		Status s_status; // under s_mutex
		std::thread s_thread;

		int s_finished = 0; // under s_mutex

		void SetState(Status::State state, std::string message = {})
		{
			std::lock_guard lock(s_mutex);
			s_status.state = state;
			s_status.message = std::move(message);
			if (state == Status::State::Done || state == Status::State::Failed)
				s_status.finished = ++s_finished;
		}

		std::string Stamp()
		{
			char stamp[32] = "saves";
			const std::time_t now = std::time(nullptr);
			if (const std::tm* local = std::localtime(&now))
				std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H%M%S", local);
			return stamp;
		}

		bool Copy(const File& file)
		{
			std::error_code ec;
			fs::create_directories(file.to.parent_path(), ec);
			std::FILE* in = std::fopen(file.from.string().c_str(), "rb");
			if (!in)
				return false;
			std::FILE* out = std::fopen(file.to.string().c_str(), "wb");
			if (!out)
			{
				std::fclose(in);
				return false;
			}
			std::vector<char> buffer(1 << 20);
			bool ok = true;
			for (;;)
			{
				const size_t read = std::fread(buffer.data(), 1, buffer.size(), in);
				if (read == 0)
				{
					ok = !std::ferror(in);
					break;
				}
				if (std::fwrite(buffer.data(), 1, read, out) != read)
				{
					ok = false;
					break;
				}
				std::lock_guard lock(s_mutex);
				s_status.copied += read;
			}
			std::fclose(in);
			return std::fclose(out) == 0 && ok;
		}

		// Each set's files: from the saves to the backup (or, restoring, back)
		std::vector<File> List(const fs::path& backup, bool restoring)
		{
			std::vector<File> files;
			std::error_code ec;
			for (const auto& set : Sets())
			{
				const fs::path from = restoring ? backup / set.name : fs::path(set.source);
				const fs::path to = restoring ? fs::path(set.source) : backup / set.name;
				if (!fs::is_directory(from, ec))
					continue;
				for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
				{
					if (!it->is_regular_file(ec))
						continue;
					const std::string relative = it->path().lexically_relative(from).generic_string();
					if (set.keep && !set.keep(relative))
						continue;
					files.push_back({it->path(), to / relative, it->file_size(ec)});
				}
			}
			return files;
		}

		void Run(fs::path backup, bool restoring)
		{
			std::vector<File> files = List(backup, restoring);
			uint64_t total = 0;
			for (const File& file : files)
				total += file.size;
			{
				std::lock_guard lock(s_mutex);
				s_status.total = total;
				s_status.files = (int)files.size();
			}
			std::error_code ec;
			// restoring: what is there now moves aside first, into saves-before, in the same shape
			const fs::path aside = fs::path(PS5CEMU_DATA) / "saves-before" / Stamp();
			int moved = 0;
			for (const File& file : files)
			{
				if (restoring && fs::exists(file.to, ec))
				{
					const fs::path kept = aside / file.to.lexically_relative(PS5CEMU_DATA);
					fs::create_directories(kept.parent_path(), ec);
					fs::rename(file.to, kept, ec);
					if (ec)
					{
						SetState(Status::State::Failed, ps5lang::TrF("A save could not be moved aside: {0}", file.to.string()));
						return;
					}
					moved++;
				}
				if (!Copy(file))
				{
					SetState(Status::State::Failed, ps5lang::TrF("A file could not be copied: {0}", file.from.filename().string()));
					return;
				}
			}
			if (!restoring)
			{
				std::ofstream json(backup / "backup.json");
				json << fmt::format("{{\"version\": \"{}\", \"date\": \"{}\", \"files\": {}, \"bytes\": {}}}\n", PS5CEMU_VERSION,
					backup.filename().string(), files.size(), total);
			}
			ps5log::Line("[saves] {} {} files, {} KB {} {}{}", restoring ? "restored" : "backed up", files.size(), total / 1000, restoring ? "from" : "to",
				backup.string(), restoring && moved ? fmt::format(" ({} kept aside in {})", moved, aside.string()) : std::string());
			SetState(Status::State::Done);
		}

		bool Begin(const fs::path& backup, bool restoring, std::string& error)
		{
			if (Current().state == Status::State::Running)
			{
				error = ps5lang::Tr("A backup is already running.");
				return false;
			}
			if (s_thread.joinable())
				s_thread.join();
			{
				std::lock_guard lock(s_mutex);
				s_status = {};
				s_status.finished = s_finished;
				s_status.state = Status::State::Running;
				s_status.restoring = restoring;
				s_status.where = backup.string();
			}
			s_thread = std::thread(Run, backup, restoring);
			return true;
		}
	}

	std::vector<Backup> FindBackups()
	{
		std::vector<Backup> backups;
		std::error_code ec;
		std::vector<std::string> drives;
		for (int i = 0; i < 8; i++)
			drives.push_back(fmt::format("/mnt/usb{}", i));
		drives.push_back("/mnt/ext0");
		drives.push_back("/mnt/ext1");
		for (const std::string& drive : drives)
		{
			const fs::path folder = fs::path(drive) / kFolder;
			if (!fs::is_directory(folder, ec))
				continue;
			for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
			{
				if (!it->is_directory(ec) || !fs::exists(it->path() / "backup.json", ec))
					continue;
				Backup backup;
				backup.folder = it->path().string();
				backup.drive = drive;
				const std::string name = it->path().filename().string(); // 2026-10-12 214000
				backup.when = name.size() >= 17 ? name.substr(0, 13) + ":" + name.substr(13, 2) : name;
				std::ifstream json(it->path() / "backup.json");
				const std::string text((std::istreambuf_iterator<char>(json)), std::istreambuf_iterator<char>());
				auto number = [&](const char* key) {
					const size_t at = text.find(key);
					return at == std::string::npos ? 0ull : std::strtoull(text.c_str() + at + std::strlen(key), nullptr, 10);
				};
				backup.files = (int)number("\"files\": ");
				backup.bytes = number("\"bytes\": ");
				backups.push_back(backup);
			}
		}
		std::sort(backups.begin(), backups.end(), [](const Backup& a, const Backup& b) { return a.when > b.when; });
		return backups;
	}

	bool StartBackup(std::string& error)
	{
		const std::string stamp = Stamp();
		for (int drive = 0; drive < 8; drive++)
		{
			std::error_code ec;
			const fs::path root = fmt::format("/mnt/usb{}", drive);
			if (!fs::is_directory(root, ec))
				continue;
			const fs::path backup = root / kFolder / stamp;
			fs::create_directories(backup, ec);
			if (ec)
				continue;
			return Begin(backup, false, error);
		}
		error = ps5lang::Tr("No USB drive found: plug one in, formatted exFAT or FAT32.");
		return false;
	}

	bool StartRestore(const Backup& backup, std::string& error)
	{
		std::error_code ec;
		if (!fs::is_directory(backup.folder, ec))
		{
			error = ps5lang::Tr("That backup is not there any more.");
			return false;
		}
		return Begin(backup.folder, true, error);
	}

	Status Current()
	{
		std::lock_guard lock(s_mutex);
		return s_status;
	}

	void Finish()
	{
		if (s_thread.joinable())
			s_thread.join();
	}
}
