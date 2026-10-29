// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps5/directory_entry.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main()
{
	ps5cemu_complete_dirent(nullptr);
	dirent entry{};
	std::strcpy(entry.d_name, "Super Mario 64 (USA).z64");
	entry.d_type = DT_REG;
	ps5cemu_complete_dirent(&entry);
	assert(entry.d_ino != 0 && entry.d_reclen != 0 && entry.d_type == DT_REG);
	assert(std::strcmp(entry.d_name, "Super Mario 64 (USA).z64") == 0);
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__PROSPERO__)
	assert(entry.d_namlen == std::strlen(entry.d_name));
#endif
	entry.d_ino = 1234; entry.d_reclen = 48;
	ps5cemu_complete_dirent(&entry);
	assert(entry.d_ino == 1234 && entry.d_reclen == 48);
	std::puts("PASS platform directory records retain names and valid records while completing missing metadata");
}
