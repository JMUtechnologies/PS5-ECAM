// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <dirent.h>
#include <cstring>

// The platform getdents reader filters vacant inode records, but older builds
// omit inode/name-length fields from the returned struct. Directory consumers
// may interpret inode 0 as an empty slot.
// Complete the stream record; callers needing file identity must still stat it.
inline void ps5cemu_complete_dirent(struct dirent* entry)
{
	if (!entry) return;
	if (!entry->d_ino) entry->d_ino = 1;
	if (!entry->d_reclen) entry->d_reclen = sizeof(*entry);
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__PROSPERO__)
	entry->d_namlen = (decltype(entry->d_namlen))std::strlen(entry->d_name);
#endif
}
