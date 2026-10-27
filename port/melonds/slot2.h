// SPDX-License-Identifier: GPL-3.0-or-later
// Optional DS Slot-2 input. This attaches a GBA cartridge to a DS; it is not a GBA emulator.
#pragma once
#include "../app/own_status.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ps5melonds::slot2
{
	struct Cartridge
	{
		std::vector<uint8_t> rom, save;
		std::string savePath, error;
	};
	inline bool Read(const std::filesystem::path& path, size_t minimum, size_t maximum, std::vector<uint8_t>& bytes, std::string& error)
	{
		std::error_code ec;
		if (std::filesystem::is_symlink(ps5files::OwnStatus(path, ec)) || ec || !std::filesystem::is_regular_file(path, ec))
		{ error = "Slot-2 file is missing or not a regular file: " + path.string(); return false; }
		const auto size = std::filesystem::file_size(path, ec);
		if (ec || size < minimum || size > maximum) { error = "Slot-2 file has an unsupported size: " + path.string(); return false; }
		bytes.resize((size_t)size);
		std::ifstream in(path, std::ios::binary);
		if (!in.read((char*)bytes.data(), (std::streamsize)bytes.size()) || in.peek() != std::char_traits<char>::eof())
		{ error = "Slot-2 file could not be read completely: " + path.string(); bytes.clear(); return false; }
		return true;
	}
	inline Cartridge Load(const std::string& root, const std::string& stem, bool enabled)
	{
		Cartridge result;
		if (!enabled) return result;
		if (stem.empty() || stem == "." || stem == ".." || stem.find_first_of("/\\") != std::string::npos)
		{ result.error = "Invalid DS game name for Slot-2."; return result; }
		const auto rom = std::filesystem::path(root) / "slot2" / (stem + ".gba");
		// no cartridge for this game: the slot stays empty, which is no error (Slot-2 is on for a side)
		std::error_code missing;
		if (ps5files::OwnStatus(rom, missing).type() == std::filesystem::file_type::not_found) return result;
		if (!Read(rom, 0xC0, 32u << 20, result.rom, result.error)) return result;
		result.savePath = (std::filesystem::path(root) / "slot2-saves" / (stem + ".sav")).string();
		std::error_code ec;
		const auto supplied = std::filesystem::path(root) / "slot2" / (stem + ".sav");
		const auto save = std::filesystem::exists(result.savePath, ec) ? std::filesystem::path(result.savePath) : supplied;
		if (std::filesystem::exists(save, ec)) Read(save, 1, 1u << 20, result.save, result.error);
		return result;
	}
}
