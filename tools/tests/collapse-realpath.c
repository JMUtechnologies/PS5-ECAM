// SPDX-License-Identifier: GPL-3.0-or-later
// Model the successful-but-identical realpath results observed in a PS5 title.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
char* realpath(const char* path, char* resolved)
{
    static char* (*original)(const char*, char*);
    if (!original) original = (char* (*)(const char*, char*))dlsym(RTLD_NEXT, "realpath");
    const char* under = getenv("COLLAPSE_REALPATH_UNDER");
    if (under && *under && strncmp(path, under, strlen(under)) == 0 && strstr(path, "/storage/"))
        return resolved ? strcpy(resolved, "/") : strdup("/");
    return original(path, resolved);
}
