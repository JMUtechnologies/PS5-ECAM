// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: where the emulator's threads run. By default the system places every thread, as in
// 1.0: pinning made Breath of the Wild slower on a console (a pinned thread cannot be moved away
// from a busy core), so it is only an experiment, turned on with "pinCpuThreads": true in
// ps5cemu.json. Then the Wii U's three emulated CPU cores (Cemu's OSSched[core=0..2], named by
// util/helpers) get a core each, both of its logical CPUs, counted from the title's second core,
// and every other named thread all of the title's CPUs.

#pragma once

#include <string>

namespace ps5threads
{
	// Reads the CPUs the title runs on (the main thread's, which the threads it starts inherit).
	// Call it from the main thread before any other starts.
	void Initialize();
	// Those CPUs, for the boot log ("CPUs 0-13: 7 cores").
	std::string Describe();
	// The experiment above, from the launcher's settings: off, nothing is placed.
	void SetPinning(bool enabled);
	// Puts the calling thread on the CPUs a thread of that name gets, when pinning or isolation is on.
	void Place(const char* name);
	// Another experiment, as ProsperoEden places its threads (+17% in its heaviest measured window):
	// the Wii U's three emulated CPU cores and Cemu's GPU thread each get a physical core of their own,
	// both of its logical CPUs, and every other thread is kept off those four cores, so none of the
	// four shares its core (Zen 2's SMT halves a core between two busy threads) and none waits on
	// another thread. Physical cores are told by their x2APIC IDs. Off: the system places every
	// thread. From the main thread, as a Wii U game starts (isolateCores in ps5cemu.json); the
	// threads named before are moved then.
	void SetIsolation(bool enabled);
	// For a game that stops drawing (a hang, not a crash): Cemu's GPU thread (LatteThread) and its
	// emulated CPU cores (OSSched[core=N]), remembered as they name themselves, each write to the boot
	// log from a signal handler on itself where it is: "[hang] NAME: rip ..., code on the stack: ...",
	// the return addresses of the calls it is in among them. The first line gives the handler's
	// address, from which tools/symbolize-crash.py names them.
	void DumpStacks(const char* why);
	// Whether Cemu's pipeline compile threads (all but the first) go below the others' priority, as
	// they do on Windows (compileBelowNormal in ps5cemu.json, an experiment, off: see settings.h)
	void SetCompileBelowNormal(bool enabled);
}
