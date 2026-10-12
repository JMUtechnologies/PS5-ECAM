// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the Vulkan driver's clock (driver_clock.h).

#include "driver_clock.h"
#include "kernel.h"
#include "log.h"

#include <atomic>
#include <cstdint>
#include <ctime>
#include <x86intrin.h>

extern "C" uint64_t sceKernelGetTscFrequency();
// Mesa's own, which the linker's --wrap names __real_; weak, so a link without the driver (the
// stand-in check, tools/link.sh) still links
extern "C" int64_t __real_os_time_get_nano(void) __attribute__((weak));

namespace
{
	std::atomic<bool> s_tsc{false};
	uint64_t s_frequency = 0; // TSC ticks a second
	uint64_t s_baseTsc = 0;
	int64_t s_baseNs = 0; // the monotonic clock's nanoseconds at s_baseTsc

	int64_t MonotonicNs()
	{
		timespec now{};
		clock_gettime(CLOCK_MONOTONIC, &now);
		return (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
	}
}

extern "C" int64_t __wrap_os_time_get_nano(void)
{
	if (!s_tsc.load(std::memory_order_acquire))
		return __real_os_time_get_nano ? __real_os_time_get_nano() : MonotonicNs();
	// the TSC is invariant and the same on every core (Cemu's log: INVARIANT-TSC), so this only
	// ever goes forward, at the monotonic clock's rate
	const uint64_t ticks = __rdtsc() - s_baseTsc;
	return s_baseNs + (int64_t)((unsigned __int128)ticks * 1000000000u / s_frequency);
}

namespace ps5clock
{
	bool UseTscForDriver()
	{
		const uint64_t reported = sceKernelGetTscFrequency();
		// the reported frequency checked against the monotonic clock over 20 ms: a wrong one would
		// make every timeout of the driver's waits that much too long or too short
		const uint64_t tsc0 = __rdtsc();
		const int64_t ns0 = MonotonicNs();
		sceKernelUsleep(20000);
		const int64_t ns1 = MonotonicNs();
		const uint64_t tsc1 = __rdtsc();
		const double measured = ns1 > ns0 ? (double)(tsc1 - tsc0) * 1e9 / (double)(ns1 - ns0) : 0.0;
		if (reported < 100000000 || measured <= 0.0 || measured / (double)reported > 1.005 || measured / (double)reported < 0.995)
		{
			ps5log::Line("[vulkan] the driver's clock stays clock_gettime: the TSC is reported at {:.2f} MHz, measured at {:.2f} MHz",
				reported / 1e6, measured / 1e6);
			return false;
		}
		// the base: the monotonic clock read between two TSC reads, set at their midpoint
		const uint64_t before = __rdtsc();
		const int64_t ns = MonotonicNs();
		const uint64_t after = __rdtsc();
		s_frequency = reported;
		s_baseTsc = before + (after - before) / 2;
		s_baseNs = ns;
		s_tsc.store(true, std::memory_order_release);
		ps5log::Line("[vulkan] the driver's clock is the TSC ({:.2f} MHz reported, {:.2f} MHz measured): no system call a read",
			reported / 1e6, measured / 1e6);
		return true;
	}
}
