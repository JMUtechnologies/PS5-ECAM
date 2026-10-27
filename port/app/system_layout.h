// SPDX-License-Identifier: GPL-3.0-or-later
// Shared with Eden's separate executable. Discovery reads only; migration activates verified copies.
#ifndef ECAM_SYSTEM_LAYOUT_H
#define ECAM_SYSTEM_LAYOUT_H
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <initializer_list>
#include <string>
#include <sys/stat.h>
// ShadowMountPlus's record of the folder the app is mounted from, and where the console mounts drives
// (tests give their own)
#ifndef ECAM_MOUNT_RECORD
#define ECAM_MOUNT_RECORD "/user/app/PPSA99360/mount.lnk"
#endif
#ifndef ECAM_DRIVES
#define ECAM_DRIVES "/mnt"
#endif
namespace ecam_layout
{
    // The launcher's data folder at the top of the drive the app is installed on (its DecideRoot):
    // the app's name before 5.0.0
    inline constexpr const char* kDriveData = "PS5CEMU-HAR-data";
    inline constexpr const char* kMarker = ".ecam-separated-v2";
    inline constexpr const char* kVersion = "ecam-system-layout-v2\n";
    inline std::string Parent(const std::string& path)
    {
        const size_t end = path.find_last_of('/');
        return end == 0 ? "/" : path.substr(0, end);
    }
    inline std::string Sibling(const std::string& shared, const char* system)
    {
        const std::string parent = Parent(shared);
        return parent + (parent == "/" ? "" : "/") + system;
    }
    // A path's own status, a link's rather than its target's. ShadowMountPlus 1.7 mounts /data into an
    // app without a HEN with lstat refused (EPERM) and stat answered: there, stat's, as nothing on /data
    // is a link
    inline int Status(const std::string& path, struct stat& info)
    {
        if (lstat(path.c_str(), &info) == 0) return 0;
        return errno == EPERM ? stat(path.c_str(), &info) : -1;
    }
    inline bool Present(const std::string& path)
    {
        struct stat info{};
        return Status(path, info) == 0 || (errno != ENOENT && errno != ENOTDIR);
    }
    inline bool Activated(const std::string& directory)
    {
        if (FILE* file = std::fopen((directory + "/" + kMarker).c_str(), "rb"))
        {
            char value[64]{};
            const size_t length = std::fread(value, 1, sizeof(value), file);
            std::fclose(file);
            return length == std::strlen(kVersion) && std::memcmp(value, kVersion, length) == 0;
        }
        return false;
    }
    inline std::string System(const std::string& shared, const char* system)
    {
        const std::string target = Sibling(shared, system), legacy = shared + "/" + system;
        return Activated(target) || !Present(legacy) ? target : legacy;
    }
    // The USB drive or extended storage a path is on ("/mnt/usb0" for "/mnt/usb0/homebrew/PPSA99360");
    // empty for any other place
    inline std::string DriveOf(const std::string& path)
    {
        for (const char* kind : {"/usb", "/ext"})
        {
            const std::string prefix = std::string(ECAM_DRIVES) + kind;
            size_t end = prefix.size();
            while (end < path.size() && path[end] >= '0' && path[end] <= '9')
                end++;
            if (path.compare(0, prefix.size(), prefix) == 0 && end > prefix.size() && (end == path.size() || path[end] == '/'))
                return path.substr(0, end);
        }
        return {};
    }
    // The folder the app was mounted from, by ShadowMountPlus's record, when it holds the app; empty
    // without one (an app mounted from an image has none)
    inline std::string MountSource(const char* record = ECAM_MOUNT_RECORD)
    {
        std::string path;
        if (FILE* file = std::fopen(record, "rb"))
        {
            char text[512]{};
            path.assign(text, std::fread(text, 1, sizeof(text) - 1, file));
            std::fclose(file);
        }
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' ' || path.back() == '\0' || path.back() == '/'))
            path.pop_back();
        struct stat info{};
        if (path.size() < 2 || path[0] != '/' || stat((path + "/eboot.bin").c_str(), &info) != 0 || !S_ISREG(info.st_mode))
            return {};
        return path;
    }
    // The launcher's data folder on the drive the app is mounted from, when it is there (the launcher
    // makes it on its first start there); empty for an app in /data or mounted from an image
    inline std::string DriveData(const char* record = ECAM_MOUNT_RECORD)
    {
        const std::string drive = DriveOf(MountSource(record));
        if (drive.empty()) return {};
        const std::string folder = drive + "/" + kDriveData;
        struct stat info{};
        return stat(folder.c_str(), &info) == 0 && S_ISDIR(info.st_mode) ? folder : std::string();
    }
    // Whether a folder holds anything but empty folders: a file, a link, or what cannot be read (not
    // taken for missing). The empty folders a start makes do not count.
    inline bool HoldsFiles(const std::string& path, int depth = 0)
    {
        struct stat info{};
        if (lstat(path.c_str(), &info) != 0) return errno != ENOENT && errno != ENOTDIR;
        if (!S_ISDIR(info.st_mode) || depth > 16) return true;
        DIR* folder = opendir(path.c_str());
        if (!folder) return true;
        bool holds = false;
        for (const dirent* entry; !holds && (entry = readdir(folder)) != nullptr;)
            if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0)
                holds = HoldsFiles(path + "/" + entry->d_name, depth + 1);
        closedir(folder);
        return holds;
    }
    // The Switch's folder, which the launcher and Eden's own program choose alike: on the console
    // (System(shared, "eden")), unless the app's data is on the drive it is installed on (driveData,
    // DriveData's): then the eden folder beside it there, once activated, or while the console's holds
    // no files (it never had the Switch side's). Switch files already on the console stay in use.
    inline std::string Eden(const std::string& shared, const std::string& driveData)
    {
        const std::string console = System(shared, "eden");
        if (driveData.empty()) return console;
        const std::string drive = Sibling(driveData, "eden");
        return Activated(drive) || !HoldsFiles(console) ? drive : console;
    }
    // The folder made the active one: its marker, as a verified migration leaves
    inline bool Activate(const std::string& directory)
    {
        if (Activated(directory)) return true;
        if (mkdir(directory.c_str(), 0777) != 0 && errno != EEXIST) return false;
        FILE* file = std::fopen((directory + "/" + kMarker).c_str(), "wb");
        if (!file) return false;
        const bool written = std::fputs(kVersion, file) >= 0;
        return std::fclose(file) == 0 && written && Activated(directory);
    }
    inline std::string Cemu(const std::string& shared)
    {
        const std::string target = Sibling(shared, "cemu");
        if (Activated(target)) return target;
        for (const char* name : {"settings.xml", "mlc01", "keys.txt", "controllerProfiles", "graphicPacks", "reshade"})
            if (Present(shared + "/" + name)) return shared;
        // Also discover a nested Cemu layout if a previous development build created one.
        return Present(shared + "/cemu") ? shared + "/cemu" : target;
    }
}

#endif
