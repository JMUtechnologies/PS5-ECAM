// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: an install from before given a folder for each system, and put back (reorganize.h).

#include "reorganize.h"

#include "paths.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ps5reorganize
{
	namespace
	{
		struct System
		{
			const char* name;
			std::string legacy, folder; // where its files were, and its own folder
			bool whole;					// moved as one folder: all but Cemu's, whose files lay among the launcher's
		};

		// The four. Cemu's files were at the top of the data folder (or, from a development build, in
		// its cemu/ when nothing of Cemu's is at the top); they go back to the top.
		std::vector<System> Systems(const std::string& root, const std::string& console, bool separate)
		{
			std::string cemu = root;
			if (separate)
			{
				bool top = false;
				for (const char* item : ps5paths::kCemuItems)
					top = top || ps5paths::Present(root + "/" + item);
				if (!top && ps5paths::Present(root + "/cemu"))
					cemu = root + "/cemu";
			}
			return {
				{"cemu", cemu, ecam_layout::Sibling(root, "cemu"), false},
				{"azahar", root + "/azahar", ecam_layout::Sibling(root, "azahar"), true},
				{"melonds", root + "/melonds", ecam_layout::Sibling(root, "melonds"), true},
				{"eden", console + "/eden", ecam_layout::Sibling(console, "eden"), true},
			};
		}

		struct Move
		{
			std::string from, to;
		};
		std::vector<Move> Moves(const System& system, bool separate)
		{
			const std::string& from = separate ? system.legacy : system.folder;
			const std::string& to = separate ? system.folder : system.legacy;
			std::vector<Move> moves;
			if (system.whole)
			{
				if (ps5paths::Present(from))
					moves.push_back({from, to});
				return moves;
			}
			for (const char* item : ps5paths::kCemuItems)
				if (ps5paths::Present(from + "/" + item))
					moves.push_back({from + "/" + item, to + "/" + item});
			return moves;
		}

		std::vector<std::string> Names(const std::string& folder)
		{
			std::vector<std::string> names;
			if (DIR* dir = opendir(folder.c_str()))
			{
				while (const dirent* entry = readdir(dir))
					if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0)
						names.push_back(entry->d_name);
				closedir(dir);
			}
			return names;
		}

		// Whether from can be moved to to: nothing there yet, on the same drive (a rename moves nothing
		// across drives), or a folder there whose files from's do not meet
		bool Check(const std::string& from, const std::string& to, Layout& layout)
		{
			struct stat source{}, target{};
			if (lstat(from.c_str(), &source) != 0)
				return true;
			if (lstat(to.c_str(), &target) != 0)
			{
				std::string parent = ecam_layout::Parent(to);
				struct stat there{};
				while (stat(parent.c_str(), &there) != 0 && parent != "/")
					parent = ecam_layout::Parent(parent);
				if (there.st_dev == source.st_dev)
					return true;
				layout.blocked = to;
				layout.otherDrive = true;
				return false;
			}
			if (S_ISDIR(source.st_mode) && S_ISDIR(target.st_mode))
			{
				for (const std::string& name : Names(from))
					if (!Check(from + "/" + name, to + "/" + name, layout))
						return false;
				return true;
			}
			layout.blocked = to;
			return false;
		}

		bool Blocked(const std::vector<System>& systems, const std::vector<std::string>& names, bool separate, Layout& layout)
		{
			for (const System& system : systems)
				if (std::find(names.begin(), names.end(), system.name) != names.end())
					for (const Move& move : Moves(system, separate))
						if (!Check(move.from, move.to, layout))
							return true;
			return false;
		}

		// A folder and those above it, open to every user as the app's folders are (app/file_modes.h)
		void MakeFolders(const std::string& path)
		{
			struct stat info{};
			if (path.empty() || path == "/" || stat(path.c_str(), &info) == 0)
				return;
			MakeFolders(ecam_layout::Parent(path));
			if (mkdir(path.c_str(), 0777) == 0)
				chmod(path.c_str(), 0777);
		}

		// from renamed to to; into a folder there already, what it holds, one by one, and from's folder
		// removed once empty
		bool MoveInto(const std::string& from, const std::string& to, std::string& error)
		{
			struct stat source{}, target{};
			if (lstat(from.c_str(), &source) != 0)
			{
				if (errno == ENOENT)
					return true;
				error = from + ": " + std::strerror(errno);
				return false;
			}
			if (lstat(to.c_str(), &target) != 0)
			{
				MakeFolders(ecam_layout::Parent(to));
				if (rename(from.c_str(), to.c_str()) == 0)
					return true;
				error = from + " to " + to + ": " + std::strerror(errno);
				return false;
			}
			if (!S_ISDIR(source.st_mode) || !S_ISDIR(target.st_mode))
			{
				error = to + " is there already";
				return false;
			}
			for (const std::string& name : Names(from))
				if (!MoveInto(from + "/" + name, to + "/" + name, error))
					return false;
			if (rmdir(from.c_str()) == 0)
				return true;
			error = from + ": " + std::strerror(errno);
			return false;
		}

		bool Transfer(const std::vector<Move>& moves, std::string& error)
		{
			for (const Move& move : moves)
				if (!MoveInto(move.from, move.to, error))
					return false;
			return true;
		}

		bool Mark(const std::string& folder)
		{
			MakeFolders(folder);
			const std::string path = folder + "/" + ecam_layout::kMarker;
			FILE* file = std::fopen(path.c_str(), "wb");
			if (!file)
				return false;
			bool ok = std::fputs(ecam_layout::kVersion, file) >= 0 && std::fflush(file) == 0;
			fsync(fileno(file));
			ok = std::fclose(file) == 0 && ok;
			chmod(path.c_str(), 0666);
			return ok && ecam_layout::Activated(folder);
		}

		std::string File(const std::string& root) { return root + "/reorganize.txt"; }

		std::vector<std::string> ReadLines(const std::string& path)
		{
			std::vector<std::string> lines;
			if (FILE* file = std::fopen(path.c_str(), "rb"))
			{
				char line[256];
				while (std::fgets(line, sizeof(line), file))
				{
					std::string text = line;
					while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
						text.pop_back();
					if (!text.empty())
						lines.push_back(text);
				}
				std::fclose(file);
			}
			return lines;
		}

		// written whole, then put in place of the one before: a power cut leaves one or the other
		bool WriteLines(const std::string& path, const std::vector<std::string>& lines)
		{
			const std::string next = path + ".new";
			FILE* file = std::fopen(next.c_str(), "wb");
			if (!file)
				return false;
			bool ok = true;
			for (const std::string& line : lines)
				ok = ok && std::fputs((line + "\n").c_str(), file) >= 0;
			ok = ok && std::fflush(file) == 0;
			fsync(fileno(file));
			ok = std::fclose(file) == 0 && ok;
			chmod(next.c_str(), 0666);
			return ok && rename(next.c_str(), path.c_str()) == 0;
		}

		std::string Joined(const std::vector<std::string>& names)
		{
			std::string text;
			for (const std::string& name : names)
				text += (text.empty() ? "" : ", ") + name;
			return text;
		}
	}

	Layout Look(const std::string& root, const std::string& console)
	{
		Layout layout;
		const auto systems = Systems(root, console, true);
		for (const System& system : systems)
			if (ecam_layout::Activated(system.folder))
				layout.separated.push_back(system.name);
			else if (!Moves(system, true).empty())
				layout.separate.push_back(system.name);
		// what is in the way of what the launcher would offer: separating, else putting back
		if (!layout.separate.empty())
			Blocked(systems, layout.separate, true, layout);
		else
			Blocked(Systems(root, console, false), layout.separated, false, layout);
		return layout;
	}

	bool Ask(const std::string& root, Request request)
	{
		if (request == Request::None)
			return unlink(File(root).c_str()) == 0 || errno == ENOENT;
		return WriteLines(File(root), {request == Request::Separate ? "separate" : "back"});
	}

	Request Asked(const std::string& root)
	{
		const auto lines = ReadLines(File(root));
		return lines.empty() ? Request::None : lines[0] == "separate" ? Request::Separate : lines[0] == "back" ? Request::PutBack : Request::None;
	}

	Outcome Run(const std::string& root, const std::string& console, std::vector<std::string>& log)
	{
		Outcome outcome;
		const std::string file = File(root);
		std::vector<std::string> lines = ReadLines(file);
		if (lines.empty())
			return outcome;
		const bool separate = lines[0] == "separate";
		if (!separate && lines[0] != "back")
		{
			log.push_back("reorganize.txt asks for " + lines[0] + ", which is not a request: removed");
			unlink(file.c_str());
			return outcome;
		}
		outcome.request = separate ? Request::Separate : Request::PutBack;
		const auto systems = Systems(root, console, separate);
		std::vector<std::string> names(lines.begin() + 1, lines.end());
		if (names.empty())
		{
			// a request just made: which systems, and whether each can move, before any does
			const Layout layout = Look(root, console);
			names = separate ? layout.separate : layout.separated;
			Layout check;
			if (Blocked(systems, names, separate, check))
			{
				log.push_back("nothing moved: " + check.blocked + (check.otherDrive ? " is on another drive" : " is there already"));
				outcome.failed = names;
				unlink(file.c_str());
				return outcome;
			}
			if (names.empty())
			{
				log.push_back(separate ? "every system is in its own folder already" : "no system is in its own folder");
				unlink(file.c_str());
				return outcome;
			}
			// what is under way, for the start after a power cut
			std::vector<std::string> pending = {lines[0]};
			pending.insert(pending.end(), names.begin(), names.end());
			WriteLines(file, pending);
			log.push_back((separate ? "a folder for each system: " : "putting back: ") + Joined(names));
		}
		else
			log.push_back((separate ? "a folder for each system, after a start that did not finish: " : "putting back, after a start that did not finish: ") +
				Joined(names));
		for (size_t i = 0; i < names.size(); i++)
		{
			const auto system = std::find_if(systems.begin(), systems.end(), [&](const System& s) { return names[i] == s.name; });
			if (system != systems.end())
			{
				const std::string marker = system->folder + "/" + ecam_layout::kMarker;
				std::string error;
				// put back, its own folder is let go of first: a power cut then leaves it to this list
				if (!separate)
					unlink(marker.c_str());
				const bool moved = Transfer(Moves(*system, separate), error) && (!separate || Mark(system->folder));
				if (moved)
				{
					log.push_back(std::string(system->name) + (separate ? ": now in " + system->folder : ": back in " + system->legacy));
					outcome.moved.push_back(system->name);
				}
				else
				{
					// all of it back where it was, so it is whole in one place
					log.push_back(std::string(system->name) + ": " + error + "; left as it was");
					std::string undo;
					unlink(marker.c_str());
					if (!Transfer(Moves(*system, !separate), undo))
						log.push_back(std::string(system->name) + ": " + undo);
					if (!separate)
						Mark(system->folder);
					outcome.failed.push_back(system->name);
				}
			}
			std::vector<std::string> rest = {lines[0]};
			rest.insert(rest.end(), names.begin() + i + 1, names.end());
			WriteLines(file, rest);
		}
		unlink(file.c_str());
		return outcome;
	}
}
