// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: every side's saves backed up to a USB drive, and restored from one (docs/UI-REDESIGN.md,
// 6.13). A backup is a folder, PS5CEMU-HAR-saves/<date and time>/ on the drive, with:
//
//   wiiu/save/          mlc01/usr/save: every game's saves, every account's, and the accounts
//   3ds/sdmc/           the 3DS's games' saves (title/.../data) and extra data (extdata), from
//                       azahar/sdmc/Nintendo 3DS; never the games installed there
//   3ds/nand/           azahar/nand/data: the system's saves
//   3ds/states/         Azahar's save states
//   ds/saves/, ds/states/  melonDS's saves and save states
//   backup.json         when, by which version, how many files and bytes
//
// Restoring puts each of a backup's files back where it came from; a file there now is first moved
// to /data/ps5cemu/saves-before/<date and time>/, in the same shape, so restoring loses nothing. Saves
// the backup does not have stay as they are. Both run on a thread of their own, one at a time.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5saves
{
	struct Backup
	{
		std::string folder; // /mnt/usb0/PS5CEMU-HAR-saves/2026-10-12 214000
		std::string drive;	// /mnt/usb0
		std::string when;	// as its folder names it: 2026-10-12 21:40
		uint64_t bytes = 0;
		int files = 0;
	};

	// The backups on the drives plugged in, newest first
	std::vector<Backup> FindBackups();

	struct Status
	{
		enum class State
		{
			Idle,
			Running,
			Done,
			Failed,
		} state = State::Idle;
		bool restoring = false;
		uint64_t copied = 0, total = 0; // bytes
		int files = 0;
		std::string where;	 // the backup's folder
		std::string message; // why it failed
		int finished = 0;	 // how many runs have ended, so a caller sees each end once
	};

	// To the first USB drive that takes a folder; false, with why, when none does or one is running
	bool StartBackup(std::string& error);
	bool StartRestore(const Backup& backup, std::string& error);
	Status Current();
	void Finish(); // waits for the thread (before a game)
}
