// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ps5eden::transfer
{
	enum class Kind { Account, Device };
	struct Source { Kind kind; std::string folder; };
	// Detect the selected title only, using the pinned standalone's plain-folder/Ryujinx index
	// readers. No download, conversion or source modification. A Symlinked save is refused.
	std::vector<Source> Discover(const std::string& root, uint64_t title, std::string& error);
	// Explicit destinations; caller resolves the selected Eden player. All source trees are
	// staged and checked before replacing either destination. Old directories remain in recovery.
	// Must run while the emulator is stopped. These synchronous helpers belong on a worker thread.
	bool Import(const std::vector<Source>& sources, const std::string& account, const std::string& device,
		const std::string& recovery, std::string& kept, std::string& error);
	// A new target only, containing account/ and/or device/. Never overwrites an export.
	bool Export(const std::string& account, const std::string& device, const std::string& target, std::string& error);
}
