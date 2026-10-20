// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the Vulkan driver's clock (driver_clock.h).

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
	uint64_t s_nsPerTick = 0; // nanoseconds a TSC tick, 32.32 fixed point: a multiply a read, no division
	uint64_t s_baseTsc = 0;
	int64_t s_baseNs = 0; // the monotonic clock's nanoseconds at s_baseTsc
	int s_agrees = -1;	  // TscAgrees' answer once it has measured: 0 or 1

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
	return s_baseNs + (int64_t)(((unsigned __int128)ticks * s_nsPerTick) >> 32);
}

namespace ps5clock
{
	bool TscAgrees()
	{
		if (s_agrees >= 0)
			return s_agrees != 0;
		const uint64_t reported = sceKernelGetTscFrequency();
		// the reported frequency checked against the monotonic clock over 20 ms: a wrong one would run
		// every timer on the TSC that much fast or slow (the emulated vsync, the driver's timeouts)
		const uint64_t tsc0 = __rdtsc();
		const int64_t ns0 = MonotonicNs();
		sceKernelUsleep(20000);
		const int64_t ns1 = MonotonicNs();
		const uint64_t tsc1 = __rdtsc();
		const double measured = ns1 > ns0 ? (double)(tsc1 - tsc0) * 1e9 / (double)(ns1 - ns0) : 0.0;
		const double ratio = reported ? measured / (double)reported : 0.0;
		s_agrees = reported >= 100000000 && ratio > 0.995 && ratio < 1.005 ? 1 : 0;
		ps5log::Line("[clock] the TSC: {:.2f} MHz reported, {:.2f} MHz measured over 20 ms{}", reported / 1e6, measured / 1e6,
			s_agrees ? "" : ": too far apart, so no clock is taken from it");
		return s_agrees != 0;
	}

	bool UseTscForDriver()
	{
		if (!TscAgrees())
		{
			ps5log::Line("[vulkan] the driver's clock stays clock_gettime: the TSC's reported rate is not its real one");
			return false;
		}
		const uint64_t reported = sceKernelGetTscFrequency();
		s_nsPerTick = (uint64_t)(((unsigned __int128)1000000000u << 32) / reported);
		// the base: the monotonic clock read between two TSC reads, set at their midpoint
		const uint64_t before = __rdtsc();
		const int64_t ns = MonotonicNs();
		const uint64_t after = __rdtsc();
		s_baseTsc = before + (after - before) / 2;
		s_baseNs = ns;
		s_tsc.store(true, std::memory_order_release);
		ps5log::Line("[vulkan] the driver's clock is the TSC: no system call a read (driverClockTsc in ps5cemu.json)");
		return true;
	}
}
