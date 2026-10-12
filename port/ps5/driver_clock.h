// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the Vulkan driver's clock. RADV (and the Mesa runtime around it) reads the time
// through Mesa's os_time_get_nano, which is clock_gettime: a system call, about 20 us on the
// console (PS5_Vulkan's measurement, docs/HARDWARE_FINDINGS.md), where a PC answers in tens of
// nanoseconds without entering the kernel. The driver reads it around every submission and on
// every turn of its waits for the GPU. The app links with --wrap=os_time_get_nano
// (tools/link.sh), so those reads come here, and here they can be the TSC's instead.

#pragma once

namespace ps5clock
{
	// From now on the driver's clock is the TSC, scaled to the monotonic clock's nanoseconds from
	// this moment, when the TSC's reported frequency agrees with a short measurement of it (false,
	// and the driver keeps clock_gettime, when it does not). Before either emulator's Vulkan starts.
	bool UseTscForDriver();
}
