// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: /data/ps5cemu open to every user (file_modes.h).

#include "file_modes.h"
#include "paths.h"
#include "../ps5/log.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <pthread.h>
#include <pthread_np.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace ps5filemodes
{
	namespace
	{
		// written when a walk has been through kRoot: its number, and how many entries it left as they
		// were (another user's, the app not root). Later starts make no walk, but for one as root while
		// some are left. A build that opens more raises the number, and the walk is made again once
		constexpr const char* kWalked = PS5CEMU_DATA "/files-opened";
		constexpr int kVersion = 1;
		constexpr size_t kExamples = 3; // of those the app may not change, in the boot log
		// the sides' own game folders hold the player's files, which the app only reads: an FTP client's
		// upload keeps its owner's mode
		constexpr const char* kPlayers[] = {PS5CEMU_DATA "/games", PS5CEMU_DATA "/azahar/games", PS5CEMU_DATA "/melonds/games"};

		std::thread s_thread;
		std::atomic<bool> s_stop{false};

		struct Walk
		{
			size_t seen = 0, opened = 0, refused = 0;
			std::vector<std::string> examples;
		};

		// What the last walk wrote: whether this build's walk has been made, and how many entries it
		// left as they were
		struct Walked
		{
			bool made = false;
			long left = 0;
		};

		Walked LastWalk()
		{
			Walked walked;
			FILE* file = std::fopen(kWalked, "r");
			if (!file)
				return walked;
			int version = 0;
			const int read = std::fscanf(file, "%d %ld", &version, &walked.left);
			std::fclose(file);
			walked.made = read >= 1 && version >= kVersion;
			return walked;
		}

		// Whether a walk is to be made: none yet (or one by an earlier build), or one as another user
		// left entries that the app, root now, may change
		bool WalkDue()
		{
			const Walked walked = LastWalk();
			return !walked.made || (walked.left > 0 && geteuid() == 0);
		}

		// Folders rwx and files rw for every user, with whatever else they have (a tile's program keeps
		// its execute bits); 0 for what is left as it is (links, sockets, devices)
		mode_t Wanted(const struct stat& st)
		{
			if (S_ISDIR(st.st_mode))
				return (st.st_mode & 07777) | 0777;
			if (S_ISREG(st.st_mode))
				return (st.st_mode & 07777) | 0666;
			return 0;
		}

		void Refused(Walk& walk, const std::string& path, const struct stat& st, int error)
		{
			walk.refused++;
			if (walk.examples.size() < kExamples)
				walk.examples.push_back(fmt::format("{} ({:o}, user {}: {})", path, st.st_mode & 07777, st.st_uid, std::strerror(error)));
		}

		// Another user's, and the app not root; not an entry gone since it was listed (a log the launcher
		// turned over)
		bool AnotherUsers(int error)
		{
			return error == EPERM || error == EACCES;
		}

		// A mode change is a metadata write, near a millisecond on the console: made only where the mode
		// is not already so. False when it was refused
		bool Open(Walk& walk, const std::string& path, const struct stat& st)
		{
			walk.seen++;
			const mode_t wanted = Wanted(st);
			if (!wanted || (st.st_mode & 07777) == wanted)
				return true;
			if (chmod(path.c_str(), wanted) == 0)
			{
				walk.opened++;
				return true;
			}
			const int error = errno;
			if (!AnotherUsers(error))
				return true;
			Refused(walk, path, st, error);
			return false;
		}

		bool PlayersFolder(const std::string& path)
		{
			for (const char* folder : kPlayers)
				if (path == folder)
					return true;
			return false;
		}

		void Run()
		{
			pthread_set_name_np(pthread_self(), "ps5filemodes");
			const auto started = std::chrono::steady_clock::now();
			Walk walk;
			struct stat root{};
			if (lstat(ps5paths::kRoot, &root) != 0 || !S_ISDIR(root.st_mode))
				return;
			Open(walk, ps5paths::kRoot, root);
			struct Folder
			{
				std::string path;
				struct stat st;
				bool refused; // its own mode, already counted
			};
			std::vector<Folder> folders{{ps5paths::kRoot, root, false}};
			while (!folders.empty() && !s_stop)
			{
				const Folder folder = std::move(folders.back());
				folders.pop_back();
				DIR* dir = opendir(folder.path.c_str());
				if (!dir)
				{
					// what is in it is out of reach: counted once, with the folder
					const int error = errno;
					if (!folder.refused && AnotherUsers(error))
						Refused(walk, folder.path, folder.st, error);
					continue;
				}
				while (const dirent* entry = readdir(dir))
				{
					if (s_stop)
						break;
					if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
						continue;
					std::string path = folder.path + "/" + entry->d_name;
					struct stat st{};
					// another file system mounted in it is not the app's to change; a link is left as it is,
					// and not followed
					if (lstat(path.c_str(), &st) != 0 || st.st_dev != root.st_dev || PlayersFolder(path))
						continue;
					const bool open = Open(walk, path, st);
					if (S_ISDIR(st.st_mode))
						folders.push_back({std::move(path), st, !open});
				}
				closedir(dir);
			}
			const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
			if (s_stop)
			{
				ps5log::Line("[files] {}: {} opened to every user before a game stopped the walk ({} looked at in {} ms); the next start goes on",
					ps5paths::kRoot, walk.opened, walk.seen, ms);
				return;
			}
			if (FILE* file = std::fopen(kWalked, "w"))
			{
				std::fprintf(file, "%d %zu\n", kVersion, walk.refused);
				std::fclose(file);
			}
			if (walk.refused == 0)
			{
				ps5log::Line("[files] {}: every file and folder open to every user ({} opened, {} looked at in {} ms)", ps5paths::kRoot,
					walk.opened, walk.seen, ms);
				return;
			}
			std::string examples;
			for (const std::string& example : walk.examples)
				examples += (examples.empty() ? "" : "; ") + example;
			ps5log::Line("[files] {}: {} opened to every user, {} another user's that the app may not change ({} looked at in {} ms): {}. "
						 "The next start asks the elevation helper (elfldr) for root to open them; without elfldr, PPSA99360 in the "
						 "HEN's app jailbreak list with alwaysAskHen in ps5cemu.json does, or a file manager's chmod 777",
				ps5paths::kRoot, walk.opened, walk.refused, walk.seen, ms, examples);
		}
	}

	bool ClosedLeft()
	{
		const Walked walked = LastWalk();
		return walked.made && walked.left > 0;
	}

	void Start()
	{
		if (s_thread.joinable() || !WalkDue())
			return;
		s_stop = false;
		s_thread = std::thread(Run);
	}

	void Stop()
	{
		s_stop = true;
		if (s_thread.joinable())
			s_thread.join();
		s_stop = false;
	}
}
