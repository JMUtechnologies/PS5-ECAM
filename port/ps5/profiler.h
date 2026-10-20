// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a sampling profiler for performance work, off unless "profile": true in ps5cemu.json.
// Every few milliseconds of the app's CPU time (setitimer's ITIMER_PROF) the thread that used it is
// interrupted, and where it was is kept: its instruction pointer and the first code addresses on its
// stack (the calls it is in, roughly: the code has no frame pointers). A thread of the profiler's
// writes them to logs/profile.bin and the threads' names to logs/profile-threads.txt;
// tools/profile-report.py makes them, with the build's ps5cemu.elf, into where each thread's time
// went. A thread's name is the one it gave Cemu's SetThreadName (the driver's threads go by their
// handles): asking libkernel for names would be one more import the app could not start without.

#pragma once

#include <cstddef>

namespace ps5profile
{
	// Starts sampling. From the main thread, after the log is open and before the emulators start.
	void Start();
	// The calling thread's name, from Cemu's SetThreadName, for the threads' file.
	void Named(const char* name);
	// A fiber's stack (port/cemu/FiberPS5.cpp) as it is made (add) and freed, so that samples taken
	// while an emulated CPU core runs a game's thread on it get their callers too. Cheap; kept
	// whether or not the profiler runs.
	void FiberStack(const void* bottom, size_t bytes, bool add);
}
