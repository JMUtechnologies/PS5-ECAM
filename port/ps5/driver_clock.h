// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the Vulkan driver's clock. RADV (and the Mesa runtime around it) reads the time
// through Mesa's os_time_get_nano, which is clock_gettime: a system call, about 890 ns in this
// app (the boot log's [clock] line; 20 us in PS5_Vulkan's measurement elsewhere), where a PC
// answers in tens of nanoseconds without entering the kernel. The driver reads it around every
// submission and on every turn of its waits for the GPU. The app links with
// --wrap=os_time_get_nano (tools/link.sh), so those reads come here, and here they can be the
// TSC's instead.

#pragma once

namespace ps5clock
{
	// Whether the TSC's reported frequency (sceKernelGetTscFrequency) agrees with its rate measured
	// against the monotonic clock, to 0.5%: the condition for any clock taken from the TSC (the
	// driver's here, Cemu's timers with tscClocks). Measured once, 20 ms, and said in the boot log.
	bool TscAgrees();

	// From now on the driver's clock is the TSC, scaled to the monotonic clock's nanoseconds from
	// this moment, when TscAgrees (false, and the driver keeps clock_gettime, when not). Before
	// either emulator's Vulkan starts.
	bool UseTscForDriver();
}
