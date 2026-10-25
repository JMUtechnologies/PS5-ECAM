// SPDX-License-Identifier: GPL-3.0-or-later
// Shared with Eden's separate executable. Discovery reads only; migration activates verified copies.
#ifndef ECAM_SYSTEM_LAYOUT_H
#define ECAM_SYSTEM_LAYOUT_H
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <sys/stat.h>
namespace ecam_layout
{
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
    inline bool Present(const std::string& path)
    {
        struct stat info{};
        return lstat(path.c_str(), &info) == 0 || (errno != ENOENT && errno != ENOTDIR);
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
