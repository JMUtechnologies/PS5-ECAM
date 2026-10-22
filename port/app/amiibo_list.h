// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the amiibo dumps the in-game menus offer (the Wii U's, app/ingame.cpp; the Switch's,
// eden/ingame.cpp): the figures' dumps in the data folder's amiibo/, those right in it and those in
// its folders, a series each, as dumps are shared ("Kirby Amiibo/Kirby.bin"). A menu chooses a series,
// then a figure in it. A file that is not an NTAG215 dump (532 to 572 bytes: the keys, a key's
// secrets, an all-in-one bundle) is not a figure and is left out.

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
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

	// The series in folder: its own dumps first, then each folder's (one level down) by name. A
	// folder with no dump is left out.
	inline std::vector<Series> Find(const std::string& folder)
	{
		std::vector<Series> list;
		Series loose;
		std::vector<std::filesystem::path> folders;
		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(folder, error))
		{
			if (entry.is_directory(error))
				folders.push_back(entry.path());
			else if (IsDump(entry))
				loose.files.push_back(entry.path().filename().string());
		}
		std::sort(loose.files.begin(), loose.files.end());
		if (!loose.files.empty())
			list.push_back(std::move(loose));
		std::sort(folders.begin(), folders.end());
		for (const auto& path : folders)
		{
			Series series;
			series.folder = path.filename().string();
			for (const auto& entry : std::filesystem::directory_iterator(path, error))
				if (IsDump(entry))
					series.files.push_back(series.folder + "/" + entry.path().filename().string());
			std::sort(series.files.begin(), series.files.end());
			if (!series.files.empty())
				list.push_back(std::move(series));
		}
		return list;
	}

	// A series as a menu names it: its folder without the " Amiibo" its name ends with ("Kirby");
	// "amiibo" for the dumps right in amiibo/
	inline std::string SeriesName(const Series& series)
	{
		if (series.folder.empty())
			return "amiibo";
		std::string name = series.folder;
		for (const char* ending : {" Amiibo", " amiibo", " AMIIBO"})
			if (name.size() > std::char_traits<char>::length(ending) && name.ends_with(ending))
				name.resize(name.size() - std::char_traits<char>::length(ending));
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
