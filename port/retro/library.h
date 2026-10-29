// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "registry.h"
#include "../app/emulator.h"
#include <vector>
namespace ps5retro
{
	// Read-only discovery; roots are explicit and files are never migrated or renamed.
	std::vector<ps5emu::Game> Scan(const std::string& side, const std::string& folder, std::string& error);
	// Discover existing libraries only. Never creates folders or moves data.
	std::vector<std::string> StorageRoots();
	std::string StorageSignature(const std::vector<std::string>& storage);
	int IdentifySystem(const std::string& path); // unambiguous loose ROMs, -1 for unknown/archive
	struct ExistingFiles { std::string category, path; size_t files = 0; bool active = false, limited = false; };
	// Inventory known native folders without reading asset contents or installing anything.
	std::vector<ExistingFiles> FindExistingFiles(const std::string& activeData,
		const std::vector<std::string>& nativeNames, const std::vector<std::string>& storage);
	std::vector<ps5emu::Game> ScanLibraries(int side, const std::string& configured,
		const std::vector<std::string>& storage, std::string& error);
	std::string CoverPath(int side, const ps5emu::Game& game);
	bool ValidateRom(int side, const std::string& path, std::string& error, int backend = 0);
	void StartScan(int side, const std::string& folder, bool discover = true);
	bool Scanning(int side);
	std::vector<ps5emu::Game> ListGames(int side);
	std::string ScanError(int side);
	void FinishScans();
}
