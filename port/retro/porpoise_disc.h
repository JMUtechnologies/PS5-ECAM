// SPDX-License-Identifier: GPL-3.0-or-later
// Wii/GameCube disc reading for banner extraction. Based on Porpoise-X's porpoise_disc.cpp.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ps5porpoise::disc
{
	// Read a file from a GameCube or Wii disc image.
	// Supports: ISO, GCM, RVZ, WIA, CISO, GCZ, WBFS, WAD
	// Returns true if file was read successfully into 'out'.
	bool read_file(const std::string& path, const std::string& name, std::vector<std::uint8_t>& out, std::string& error);

	// Read the disc header (0x100 bytes) from various container formats.
	bool read_header(const std::string& path, std::uint8_t out[0x100], std::string& error);

	// Check if a path is a WAD file
	bool is_wad(const std::string& path);
}