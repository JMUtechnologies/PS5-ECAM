// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the amiibo dumps the in-game menus offer (the Wii U's, app/ingame.cpp; the Switch's,
// eden/ingame.cpp): the figures' dumps in the data folder's amiibo/, those right in it and those in
// its folders, however deep, a series each, as dumps are shared ("Kirby Amiibo/Kirby.bin"). A menu
// chooses a series, then a figure in it. A file that is not an NTAG215 dump (532 to 572 bytes: the keys, a key's
// secrets, an all-in-one bundle) is not a figure and is left out.

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ps5amiibo
{
	struct Series
	{
		std::string folder;				// the folder's name; empty for the dumps right in amiibo/
		std::vector<std::string> files; // each dump's path from amiibo/, by name
	};

	inline bool IsDump(const std::filesystem::directory_entry& entry)
	{
		std::error_code error;
		std::string extension = entry.path().extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if (extension != ".bin" || !entry.is_regular_file(error))
			return false;
		const auto size = entry.file_size(error);
		return !error && size >= 532 && size <= 572;
	}

	// The series in folder: each folder holding dumps, however deep ("Animal Crossing Amiibo/Amiibo
	// Cards/!Series 1"), by its path; the dumps right in folder first. A folder with none is left out.
	inline std::vector<Series> Find(const std::string& folder)
	{
		std::vector<Series> list;
		std::error_code error;
		const std::filesystem::path root(folder);
		std::map<std::string, std::vector<std::string>> byFolder; // a folder's path from root ("" for root's own)
		for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
			 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
		{
			if (it.depth() > 6)
				it.disable_recursion_pending(); // deep enough for any collection
			if (!IsDump(*it))
				continue;
			const std::string relative = it->path().lexically_relative(root).generic_string();
			const std::string parent = it->path().parent_path().lexically_relative(root).generic_string();
			byFolder[parent == "." ? std::string() : parent].push_back(relative);
		}
		for (auto& [path, files] : byFolder) // the map's order: root's own first, then by path
		{
			std::sort(files.begin(), files.end());
			list.push_back({path, std::move(files)});
		}
		return list;
	}

	// A series as a menu names it: its folders' names, each without the " Amiibo" it ends with and the
	// "!" a collection sorts by, " · " between them ("Animal Crossing · Amiibo Cards · Series 1");
	// "amiibo" for the dumps right in amiibo/
	inline std::string SeriesName(const Series& series)
	{
		if (series.folder.empty())
			return "amiibo";
		std::string name;
		for (const auto& part : std::filesystem::path(series.folder))
		{
			std::string piece = part.string();
			for (const char* ending : {" Amiibo", " amiibo", " AMIIBO"})
				if (piece.size() > std::char_traits<char>::length(ending) && piece.ends_with(ending))
					piece.resize(piece.size() - std::char_traits<char>::length(ending));
			while (piece.size() > 1 && piece[0] == '!')
				piece.erase(0, 1);
			name += (name.empty() ? "" : " · ") + piece;
		}
		return name;
	}

	// A dump as a menu names it: its file's name without .bin ("Meta Knight")
	inline std::string FigureName(const std::string& file)
	{
		return std::filesystem::path(file).stem().string();
	}

	// The step a menu's Left or Right takes through count choices, wrapping
	inline int Step(int index, int change, int count)
	{
		return count <= 0 ? 0 : ((std::clamp(index, 0, count - 1) + change) % count + count) % count;
	}
}
