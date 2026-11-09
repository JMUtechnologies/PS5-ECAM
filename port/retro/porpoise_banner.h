// SPDX-License-Identifier: GPL-3.0-or-later
// Wii banner (opening.bnr) extraction for launcher images. Based on Porpoise-X's porpoise_banner.cpp.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ps5porpoise
{
	// Extract the widescreen banner (first frame) from a Wii disc's opening.bnr.
	// Returns true if banner was successfully extracted and saved as PNG at output_path.
	// The output PNG will be 16:9 (e.g., 608x456 or similar).
	bool ExtractWiiBanner(const std::string& disc_path, const std::string& output_path, std::string& error);
}