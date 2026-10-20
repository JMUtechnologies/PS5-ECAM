// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: fcntl for a title. The app is linked with -Wl,--wrap=fcntl (tools/link.sh), so every
// call comes here first and goes on to the C library's own (__real_fcntl).
//
// The console answers F_SETFD on a socket with EINVAL. curl 8.18 (pacbrew's libcurl.a) marks each
// socket it opens close-on-exec that way and gives up on the connection when it fails ("fcntl set
// CLOEXEC: Invalid argument", curl error 7), so no download worked: box art, the update check,
// RetroAchievements, the community graphic packs. A title never execs, so close-on-exec means
// nothing here: a refused F_SETFD is answered as done.

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <fcntl.h>

extern "C" int __real_fcntl(int fd, int cmd, ...);

extern "C" int __wrap_fcntl(int fd, int cmd, ...)
{
	// every fcntl argument is an int, a long or a pointer, each passed in one register
	va_list args;
	va_start(args, cmd);
	const intptr_t arg = va_arg(args, intptr_t);
	va_end(args);
	const int result = __real_fcntl(fd, cmd, arg);
	if (result < 0 && cmd == F_SETFD && errno == EINVAL)
		return 0;
	return result;
}
