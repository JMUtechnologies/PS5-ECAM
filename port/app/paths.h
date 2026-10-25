// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: where things live on the console.
//
//   /data/homebrew/PPSA99360/        the app (eboot.bin, sce_sys, assets) as ShadowMountPlus mounts it
//   /app0/                           the same, read-only, from inside the app's sandbox (AppDir)
//   /data/cemu/                      Wii U games, MLC, keys, packs and Cemu settings
//   /data/azahar/                    3DS games, SD/NAND storage, keys and settings
//   /data/melonds/                   DS games, BIOS/firmware, saves and cheats
//   /data/eden/                      Switch games, keys, firmware, add-ons and user storage
//   /data/ps5cemu/                   shared launcher settings, logs, covers and downloads
// Existing installs under ps5cemu are discovered without moving data. tools/separate-systems.py
// backs up and verifies copies before activating the separate folders.

#pragma once

#include "system_layout.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace ps5paths
{
	constexpr const char* kTitleId = "PPSA99360";
	constexpr const char* kInstallDir = "/data/homebrew/PPSA99360";
	constexpr const char* kMountedEboot = "/data/homebrew/PPSA99360/eboot.bin";
	// where the PS5 mounts the app it runs, whatever ShadowMountPlus mounted it from (a folder on
	// /data, an extended storage or USB drive, or an image)
	constexpr const char* kSystemMount = "/system_ex/app/PPSA99360";
	// ShadowMountPlus's record of that source: mount.lnk holds the folder's path, mount_img.lnk is
	// there for an image
	constexpr const char* kMountRecord = "/user/app/PPSA99360/mount.lnk";
	constexpr const char* kImageMountRecord = "/user/app/PPSA99360/mount_img.lnk";

// where PS5-ECAM writes everything, unless DecideRoot finds a folder of its own on the app's
// drive: on the console /data/ps5cemu; the launcher's preview on a PC (tools/preview-shell.sh) gives
// a folder of its own
#ifndef PS5CEMU_DATA
#define PS5CEMU_DATA "/data/ps5cemu"
#endif
	constexpr const char* kDefaultRoot = PS5CEMU_DATA;
	// the data folder's name at the top of the app's drive (DecideRoot): the app's name before 5.0.0,
	// kept, as /data/ps5cemu is, so the data people have there stays theirs
	constexpr const char* kDriveRoot = "PS5CEMU-HAR-data";

	namespace detail
	{
		inline std::string& RootSlot()
		{
			static std::string root = kDefaultRoot;
			return root;
		}
	}

	namespace detail
	{
		struct SystemRoots
		{
			bool decided = false;
			std::string azahar, melonds, eden;
		};
		inline SystemRoots& SelectedSystems() { static SystemRoots roots; return roots; }
	}

	// The data folder: kDefaultRoot until main has decided it (DecideRoot), then the one decided
	inline const std::string& Root() { return detail::RootSlot(); }
	inline std::string CemuFolder() { return ecam_layout::Sibling(Root(), "cemu"); }
	inline std::string Azahar() { const auto& s = detail::SelectedSystems(); return s.decided ? s.azahar : ecam_layout::System(Root(), "azahar"); }
	inline std::string MelonDs() { const auto& s = detail::SelectedSystems(); return s.decided ? s.melonds : ecam_layout::System(Root(), "melonds"); }
	// Eden's separate process shares this exact selection algorithm; its storage stays on /data.
	inline std::string Eden() { const auto& s = detail::SelectedSystems(); return s.decided ? s.eden : ecam_layout::System(kDefaultRoot, "eden"); }
	inline std::string EdenRequestFile() { return Eden() + "/launch-request.txt"; }
	inline std::string EdenSessionFile() { return Eden() + "/ecam-session.txt"; }
	inline std::string EdenResultFile() { return Eden() + "/ecam-result.txt"; }
	inline bool Present(const std::string& path) { return ecam_layout::Present(path); }
	namespace detail
	{
		inline std::string FindCemuRoot(const std::string& root) { return ecam_layout::Cemu(root); }
	}
	inline const std::string& CemuRoot()
	{
		// Used only after DecideRoot and elevation. Cemu's own paths are fixed for the process.
		static const std::string folder = detail::FindCemuRoot(Root());
		return folder;
	}
	// Main calls this after elevation and DecideRoot, before any thread. Early settings reads
	// can discover defaults without fixing a choice while the process is still in its sandbox.
	inline void DecideSystemRoots()
	{
		auto& s = detail::SelectedSystems();
		if (s.decided) return;
		s.azahar = ecam_layout::System(Root(), "azahar");
		s.melonds = ecam_layout::System(Root(), "melonds");
		s.eden = ecam_layout::System(kDefaultRoot, "eden");
		(void)CemuRoot();
		s.decided = true;
	}
	inline std::string Mlc() { return CemuRoot() + "/mlc01"; }
	inline std::string Games() { return CemuFolder() + "/games"; }
	inline std::string CemuCache() { return CemuRoot() + "/cache"; }
	inline std::string DiscKeys() { return CemuRoot() + "/keys.txt"; }

	inline std::vector<std::string> GameFolders(const std::vector<std::string>& configured)
	{
		std::vector<std::string> folders;
		auto add = [&](const std::string& folder) {
			if (std::find(folders.begin(), folders.end(), folder) == folders.end())
				folders.push_back(folder);
		};
		const std::string legacy = Root() + "/games";
		for (const std::string& folder : configured.empty() ? std::vector<std::string>{Games()} : configured)
		{
			if (folder == Games() || folder == legacy || folder == Root() + "/cemu/games")
			{
				add(Games());
				if (Present(legacy) && !ecam_layout::Activated(CemuFolder()))
					add(legacy);
				if (Present(Root() + "/cemu/games") && !ecam_layout::Activated(CemuFolder()))
					add(Root() + "/cemu/games");
			}
			else
				add(folder); // custom local, USB and network paths remain explicit choices
		}
		return folders;
	}
	// Native input and storage directories, made at startup without inventing unsupported content.
	inline std::vector<std::string> SystemDirectories()
	{
		std::vector<std::string> directories{Games()};
		auto add = [&](const std::string& base, std::initializer_list<const char*> names) {
			for (const char* name : names) directories.push_back(base + "/" + name);
		};
		add(CemuRoot(), {"updates", "dlc", "mlc01", "graphicPacks", "controllerProfiles"});
		add(Azahar(), {"games", "updates", "dlc", "sysdata", "nand", "sdmc", "cheats", "load"});
		add(MelonDs(), {"games", "bios", "saves", "states", "cheats"});
		add(Eden(), {"roms", "updates", "keys", "firmware", "mods", "user", "config"});
		return directories;
	}
	inline std::string Cache() { return Root() + "/cache"; }
	inline std::string RadvCache() { return Root() + "/cache/radv"; }
	inline std::string Logs() { return Root() + "/logs"; }
	inline std::string LauncherSettings() { return Root() + "/ps5cemu.json"; }
	inline std::string Covers() { return Root() + "/covers"; }

	// A path in kDefaultRoot, in Root() instead when the data is elsewhere: a folder setting saved
	// while the data was in /data follows the data to the drive it was moved to (#52)
	inline std::string Relocated(const std::string& path)
	{
		if (Root() != kDefaultRoot)
			for (const char* system : {"cemu", "azahar", "melonds"})
			{
				const std::string old = ecam_layout::Sibling(kDefaultRoot, system);
				if (path == old || path.starts_with(old + "/"))
					return ecam_layout::Sibling(Root(), system) + path.substr(old.size());
			}
		const size_t length = std::strlen(kDefaultRoot);
		if (Root() == kDefaultRoot || path.compare(0, length, kDefaultRoot) != 0 || (path.size() > length && path[length] != '/'))
			return path;
		return Root() + path.substr(length);
	}

	// The folder ShadowMountPlus mounted the running app from, by its record (kMountRecord), when it
	// holds the app; empty without one. image: whether the app was mounted from an image instead,
	// whose files can't be replaced one by one.
	inline std::string MountSource(bool* image = nullptr)
	{
		struct stat info{};
		if (image)
			*image = stat(kImageMountRecord, &info) == 0;
		std::string path;
		if (FILE* file = std::fopen(kMountRecord, "rb"))
		{
			char text[512] = {};
			path.assign(text, std::fread(text, 1, sizeof(text) - 1, file));
			std::fclose(file);
		}
		while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' ' || path.back() == '\0' || path.back() == '/'))
			path.pop_back();
		if (path.size() < 2 || path[0] != '/' || stat((path + "/eboot.bin").c_str(), &info) != 0 || !S_ISREG(info.st_mode))
			return {};
		return path;
	}

	// The app's own folder. The sandbox mounts it as /app0, but a process the HEN has jailbroken
	// sees the console's root, which has no /app0: there the app is read where the PS5 mounts it,
	// which follows ShadowMountPlus wherever it found the app (an install on a USB drive with an older
	// copy left in /data/homebrew included), else where it is usually installed, or where the sandbox
	// mounts it from (as ProsperoEden's storage_paths.h finds its own).
	inline std::string FindAppDir()
	{
		auto holdsApp = [](const std::string& folder) {
			struct stat info{};
			return stat((folder + "/eboot.bin").c_str(), &info) == 0 && S_ISREG(info.st_mode);
		};
		for (const char* candidate : {"/app0", kSystemMount, kInstallDir, "/mnt/sandbox/PPSA99360_000/app0"})
			if (holdsApp(candidate))
				return std::string(candidate);
		// none of those: the folder ShadowMountPlus recorded (MountSource checks it holds the app),
		// then the homebrew folders on the drives people install to (#40 tried /mnt/ext1/homebrew)
		if (std::string source = MountSource(); !source.empty())
			return source;
		for (const char* drive : {"/mnt/ext0", "/mnt/ext1", "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3"})
			if (std::string folder = std::string(drive) + "/homebrew/" + kTitleId; holdsApp(folder))
				return folder;
		return std::string(kInstallDir);
	}

	namespace detail
	{
		inline std::string& AppDirSlot()
		{
			static std::string directory;
			return directory;
		}
	}

	// FindAppDir's answer, decided on first use (after ps5privilege::Acquire) and again by RefindAppDir
	inline const std::string& AppDir()
	{
		std::string& directory = detail::AppDirSlot();
		if (directory.empty())
			directory = FindAppDir();
		return directory;
	}

	// The app's folder looked for again, by the elevation helper's callers, before any thread starts:
	// the helper takes the process out of the sandbox, to the console's root, where the /app0 found
	// before is gone (3.5.0 then read its launcher's font from there, and on a console whose drives
	// the HEN leaves out the launcher never started, #34). Whether it moved.
	inline bool RefindAppDir()
	{
		std::string& directory = detail::AppDirSlot();
		if (directory.empty())
			return false;
		std::string found = FindAppDir();
		if (found == directory)
			return false;
		directory = std::move(found);
		return true;
	}

	// The app's own download0, which its sandbox mounts as /download0 and a process the HEN or the
	// elevation helper has taken out of it sees at /mnt/sandbox/PPSA99360_000/download0: the Switch
	// side's program keeps its trace there until it reaches /data (patches/eden)
	inline std::string SandboxDownload()
	{
		struct stat info{};
		if (stat("/download0", &info) == 0 && S_ISDIR(info.st_mode))
			return "/download0";
		return std::string("/mnt/sandbox/") + kTitleId + "_000/download0";
	}

	// The USB drive or extended storage a path is on ("/mnt/usb0" for "/mnt/usb0/homebrew/PPSA99360");
	// empty for any other place
	inline std::string DriveOf(const std::string& path)
	{
		for (const char* prefix : {"/mnt/usb", "/mnt/ext"})
		{
			const size_t length = std::strlen(prefix);
			size_t end = length;
			while (end < path.size() && path[end] >= '0' && path[end] <= '9')
				end++;
			if (path.compare(0, length, prefix) == 0 && end > length && (end == path.size() || path[end] == '/'))
				return path.substr(0, end);
		}
		return {};
	}

	// The data folder, decided once by main, when the process sees the console's drives (after the
	// elevation helper): a PS5CEMU-HAR-data folder at the top of the drive the app is installed on
	// (where ShadowMountPlus mounted it from, else the app's folder) holds everything, and nothing is
	// made in /data (#52). A first start on a drive, with no /data/ps5cemu yet, makes that folder; an
	// app that has kept its data in /data goes on doing so until the folder is made (the guide's
	// "Everything on the app's drive" says how to move it). An app in /data, or mounted from an
	// image, keeps kDefaultRoot. Not a drive's ps5cemu folder: that one holds Wii U mods (app/mods.h),
	// and the app's backups and logs on a drive are PS5CEMU-HAR-saves and PS5-ECAM-logs-... already.
	inline const std::string& DecideRoot()
	{
		std::string& root = detail::RootSlot();
		std::string drive = DriveOf(MountSource());
		if (drive.empty())
			drive = DriveOf(AppDir());
		if (drive.empty())
			return root;
		const std::string folder = drive + "/" + kDriveRoot;
		struct stat info{};
		const bool there = stat(folder.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
		const bool inData = stat(kDefaultRoot, &info) == 0;
		if (there || (!inData && mkdir(folder.c_str(), 0777) == 0))
			root = folder;
		return root;
	}

	inline std::string Eboot() { return AppDir() + "/eboot.bin"; }
	inline std::string Assets() { return AppDir() + "/assets"; }
	// Cemu's read-only data (gameProfiles, resources)
	inline std::string CemuData() { return Assets() + "/cemu"; }
}
