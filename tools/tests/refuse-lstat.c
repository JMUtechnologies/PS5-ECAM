// SPDX-License-Identifier: GPL-3.0-or-later
// LD_PRELOAD for a host test: lstat refused (EPERM) under $REFUSE_LSTAT_UNDER, as the console's /data
// refuses it to an app without a HEN (port/app/own_status.h), std::filesystem's symlink_status as much
// as a direct call. A link there is still answered, so a test's own links are still told apart (the
// console's /data has none).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int lstat(const char* path, struct stat* st)
{
	static int (*real)(const char*, struct stat*);
	if (!real)
		real = (int (*)(const char*, struct stat*))dlsym(RTLD_NEXT, "lstat");
	const char* under = getenv("REFUSE_LSTAT_UNDER");
	const int result = real(path, st);
	if (!under || !*under || strncmp(path, under, strlen(under)) != 0 || (result == 0 && S_ISLNK(st->st_mode)))
		return result;
	errno = EPERM;
	return -1;
}
