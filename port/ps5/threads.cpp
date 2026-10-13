// SPDX-License-Identifier: GPL-3.0-or-later
#include "threads.h"
#include "log.h"
#include "profiler.h"

#include <sys/types.h>
#include <sys/cpuset.h>
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cpuid.h>
#include <map>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <x86intrin.h>

extern "C"
{
	int sceKernelGetCurrentCpu();
	uint64_t sceKernelGetTscFrequency();
	// Sony's thread affinity, a mask of the 64 CPUs a thread may run on: what titles use. FreeBSD's
	// pthread_getaffinity_np, with userland's 256-CPU set, is refused to a title.
	void* scePthreadSelf();
	int scePthreadGetprio(void* thread, int* priority);
	int scePthreadSetprio(void* thread, int priority);
	int scePthreadGetaffinity(void* thread, uint64_t* mask);
	int scePthreadSetaffinity(void* thread, uint64_t mask);
}

namespace ps5threads
{
	namespace
	{
		constexpr int kMaxCpus = 64;
		uint64_t s_title = 0;	  // the CPUs the title may run on
		std::vector<int> s_cores; // the cores it has both logical CPUs of, by their first CPU
		bool s_ready = false;
		std::atomic<bool> s_pinning{false};
		std::mutex s_mutex;

		// The calling thread's CPUs: Sony's call, or FreeBSD's when that is refused. 0 when neither
		// tells, with why in error.
		uint64_t ReadAffinity(std::string& error)
		{
			uint64_t mask = 0;
			const int sony = scePthreadGetaffinity(scePthreadSelf(), &mask);
			if (sony == 0 && mask)
				return mask;
			cpuset_t set;
			CPU_ZERO(&set);
			const int bsd = pthread_getaffinity_np(pthread_self(), sizeof(set), &set);
			if (bsd == 0)
			{
				for (int cpu = 0; cpu < kMaxCpus; cpu++)
					if (CPU_ISSET(cpu, &set))
						mask |= 1ull << cpu;
				if (mask)
					return mask;
			}
			error = fmt::format("scePthreadGetaffinity {:#x}, pthread_getaffinity_np {}", (uint32_t)sony, bsd);
			return 0;
		}

		// 0, or the reasons both calls gave
		std::string WriteAffinity(uint64_t mask)
		{
			const int sony = scePthreadSetaffinity(scePthreadSelf(), mask);
			if (sony == 0)
				return {};
			cpuset_t set;
			CPU_ZERO(&set);
			for (int cpu = 0; cpu < kMaxCpus; cpu++)
				if (mask & (1ull << cpu))
					CPU_SET(cpu, &set);
			const int bsd = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
			if (bsd == 0)
				return {};
			return fmt::format("scePthreadSetaffinity {:#x}, pthread_setaffinity_np {}", (uint32_t)sony, bsd);
		}

		std::string List(uint64_t mask)
		{
			std::string cpus;
			for (int cpu = 0; cpu < kMaxCpus; cpu++)
				if (mask & (1ull << cpu))
					cpus += (cpus.empty() ? "" : ",") + std::to_string(cpu);
			return cpus;
		}

		// A thread's CPUs: the emulated Wii U CPU cores (Cemu's scheduler threads, OSSched[core=N]) get
		// a core each, so they keep their caches and do not move; every other thread gets all of the
		// title's CPUs (-1). That includes the GPU thread and the shader and pipeline compilers, which
		// load a game's caches on many threads at once, and undoes the CPUs a thread inherits from the
		// one that started it.
		int SlotOf(const char* name)
		{
			unsigned core = 0;
			if (std::sscanf(name, "OSSched[core=%u]", &core) == 1)
				return (int)core;
			return -1;
		}

		uint64_t CoreMask(int core)
		{
			return ((1ull << core) | (1ull << (core + 1))) & s_title;
		}

		// -- isolation (SetIsolation) --------------------------------------------------------------
		std::atomic<bool> s_isolating{false};
		int s_dedicated[4] = {-1, -1, -1, -1}; // OSSched[core=0..2], LatteThread: one logical CPU each
		uint64_t s_dedicatedCores = 0;		   // both logical CPUs of those four physical cores
		uint64_t s_secondary = 0;			   // every other thread's CPUs
		uint64_t s_compilers = 0;			   // the compilers': those and the four cores' idle halves

		// The dedicated CPU's slot of a thread of that name, or -1
		int DedicatedSlot(const char* name)
		{
			if (std::strcmp(name, "LatteThread") == 0)
				return 3;
			const int core = SlotOf(name);
			return core >= 0 && core < 3 ? core : -1; // the Wii U's three
		}

		// Cemu's shader and pipeline compilers, which load a game's caches on many threads at once
		bool IsCompiler(const char* name)
		{
			for (const char* compiler : {"compilePl", "vkShaderComp", "plCacheCompiler", "vkDriverPlCache"})
				if (std::strcmp(name, compiler) == 0)
					return true;
			return false;
		}

		// The calling thread's time, in TSC ticks, on four chains of multiplies, steps long each. A step
		// of a chain is a multiply and an add, four cycles on Zen 2, and a core's one multiplier takes
		// the four chains' multiplies one a cycle: four cycles a step either way, whatever the clock.
		uint64_t MultiplyTicks(uint32_t steps)
		{
			uint64_t a = 1, b = 3, c = 5, d = 7;
			const uint64_t begin = __rdtsc();
			for (uint32_t i = 0; i < steps; i++)
			{
				a = a * 6364136223846793005ull + 1;
				b = b * 2862933555777941757ull + 3;
				c = c * 3202034522624059733ull + 5;
				d = d * 1442695040888963407ull + 7;
				asm volatile("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d)); // each step as written
			}
			const uint64_t end = __rdtsc();
			asm volatile("" : : "r"(a ^ b ^ c ^ d));
			return end - begin;
		}

		// The clock, in MHz, that ran steps of MultiplyTicks in ticks
		unsigned ClockMhz(uint32_t steps, uint64_t ticks)
		{
			const uint64_t tsc = sceKernelGetTscFrequency();
			return ticks && tsc ? (unsigned)(4.0 * steps * (double)tsc / (double)ticks / 1e6 + 0.5) : 0;
		}

		constexpr uint32_t kPairSteps = 1500000; // six million multiplies: about 1.7 ms at 3.5 GHz

		// Two threads on CPUs a and b running MultiplyTicks at the same time: the longer one's ticks, 0
		// when either could not be put on its CPU. Two logical CPUs of one core share its multiplier and
		// take about twice as long as two CPUs of different cores (ProsperoEden's test).
		uint64_t PairTicks(int a, int b)
		{
			std::atomic<int> ready{0};
			std::atomic<bool> go{false};
			uint64_t ticks[2] = {};
			const auto body = [&](int cpu, int slot) {
				const bool placed = WriteAffinity(1ull << cpu).empty();
				ready.fetch_add(1);
				if (!placed)
					return;
				// bounded: the console does not time-slice threads of one priority
				for (uint32_t spins = 0; !go.load(std::memory_order_acquire) && spins < 200000000; spins++)
				{
				}
				ticks[slot] = MultiplyTicks(kPairSteps);
			};
			std::thread first(body, a, 0), second(body, b, 1);
			while (ready.load() < 2)
				std::this_thread::yield();
			go.store(true, std::memory_order_release);
			first.join();
			second.join();
			return ticks[0] && ticks[1] ? std::max(ticks[0], ticks[1]) : 0;
		}

		// The shortest of three runs of a pair, for whatever else ran meanwhile
		uint64_t BestPair(int a, int b)
		{
			uint64_t best = 0;
			for (int run = 0; run < 3; run++)
			{
				const uint64_t ticks = PairTicks(a, b);
				if (ticks && (!best || ticks < best))
					best = ticks;
			}
			return best;
		}

		// Two threads on CPUs a and b passing a cache line back and forth: nanoseconds a round trip, the
		// best of three runs of 20,000, or 0 when either could not be put on its CPU. Two cores of one
		// core complex share its L3 and pass a line in some tens of nanoseconds; between complexes it
		// crosses the fabric, a few times slower. Where the emulated cores and the GPU thread should
		// sit (one complex, or both) depends on it.
		double RoundTripNs(int a, int b)
		{
			constexpr uint32_t kTrips = 20000;
			constexpr uint64_t kGiveUp = 200000000; // bounded: the console does not time-slice one priority
			double best = 0;
			for (int run = 0; run < 3; run++)
			{
				alignas(64) std::atomic<uint32_t> ball{0};
				std::atomic<int> ready{0};
				std::atomic<bool> failed{false};
				uint64_t ticks = 0;
				const auto player = [&](int cpu, uint32_t parity) {
					if (!WriteAffinity(1ull << cpu).empty())
						failed = true;
					ready.fetch_add(1);
					for (uint64_t spins = 0; ready.load() < 2 && spins < kGiveUp; spins++)
					{
					}
					const uint64_t begin = __rdtsc();
					for (uint32_t i = 0; i < kTrips && !failed.load(std::memory_order_relaxed); i++)
					{
						const uint32_t mine = 2 * i + parity;
						for (uint64_t spins = 0; ball.load(std::memory_order_acquire) != mine; spins++)
							if (spins > kGiveUp || failed.load(std::memory_order_relaxed))
							{
								failed = true;
								break;
							}
						ball.store(mine + 1, std::memory_order_release);
					}
					if (parity == 0)
						ticks = __rdtsc() - begin;
				};
				std::thread first(player, a, 0u), second(player, b, 1u);
				first.join();
				second.join();
				const double tsc = (double)sceKernelGetTscFrequency();
				if (failed.load() || !tsc)
					return 0;
				const double ns = (double)ticks / kTrips * 1e9 / tsc;
				if (!best || ns < best)
					best = ns;
			}
			return best;
		}

		// Each of the title's CPUs' physical core, from its x2APIC ID (CPUID leaf 0xB: the ID above
		// the SMT level's shift), read on the CPU itself: OS numbering need not follow it
		std::map<uint32_t, uint64_t> PhysicalCores(std::string& error)
		{
			std::map<uint32_t, uint64_t> cores; // core ID: its CPUs
			unsigned maxLeaf = __get_cpuid_max(0, nullptr);
			if (maxLeaf < 0xb)
			{
				error = "no CPUID leaf 0xB";
				return {};
			}
			for (int cpu = 0; cpu < kMaxCpus; cpu++)
			{
				if (!(s_title & (1ull << cpu)))
					continue;
				if (!WriteAffinity(1ull << cpu).empty())
				{
					error = fmt::format("the main thread could not be put on CPU {}", cpu);
					cores.clear();
					break;
				}
				unsigned a = 0, b = 0, c = 0, d = 0;
				__cpuid_count(0xb, 0, a, b, c, d);
				if (b == 0 || ((c >> 8) & 0xff) != 1) // level 0 must be the SMT level
				{
					error = "CPUID leaf 0xB has no SMT level";
					cores.clear();
					break;
				}
				cores[d >> (a & 31)] |= 1ull << cpu;
			}
			WriteAffinity(s_title); // the main thread back on every CPU
			return cores;
		}

		// The title's whole physical cores (both of whose logical CPUs it has), in order, each as its
		// two CPUs; how they were told apart goes in how, or why they could not be
		std::vector<uint64_t> WholeCores(std::string& how)
		{
			std::string error;
			std::vector<uint64_t> whole;
			for (const auto& [id, cpus] : PhysicalCores(error))
				if (__builtin_popcountll(cpus) == 2)
					whole.push_back(cpus);
			if (whole.size() >= 5)
			{
				how = "CPUID's x2APIC IDs";
				return whole;
			}
			whole.clear();
			// CPUID does not tell them apart (newer system software gives every CPU one x2APIC ID): a
			// timing says whether CPU 0's other half is CPU 1 (2n and 2n+1 one core) or CPU 8 (n and n+8),
			// against CPU 2, of another core either way. The main thread waits on a CPU none of them is.
			const std::string cpuid = error.empty() ? "gives every CPU one core" : error;
			const int spare = 63 - __builtin_clzll(s_title);
			const uint64_t timed = (1ull << 0) | (1ull << 1) | (1ull << 2) | (1ull << 8);
			if ((s_title & timed) != timed || spare <= 8)
			{
				how = fmt::format("CPUID {}, and CPUs 0, 1, 2 and 8 are not all the title's to time", cpuid);
				return whole;
			}
			WriteAffinity(1ull << spare);
			const uint64_t adjacent = BestPair(0, 1), apart = BestPair(0, 2), half = BestPair(0, 8);
			WriteAffinity(s_title);
			const double tsc = (double)sceKernelGetTscFrequency();
			const auto ms = [&](uint64_t ticks) { return tsc > 0 ? ticks * 1000.0 / tsc : 0.0; };
			// one thread alone on a core runs the multiplies at the core's clock
			const std::string timings = fmt::format("six million multiplies on CPUs 0 and 1 at once {:.2f} ms, 0 and 2 {:.2f} ms, "
				"0 and 8 {:.2f} ms; a core alone at about {} MHz", ms(adjacent), ms(apart), ms(half), ClockMhz(kPairSteps, apart));
			// a pair of one core takes at least a quarter longer than a pair of two
			const auto shares = [&](uint64_t pair) { return pair && apart && pair * 4 >= apart * 5; };
			int step = 0;
			if (shares(adjacent) && !shares(half))
				step = 1;
			else if (shares(half) && !shares(adjacent))
				step = 8;
			if (!step)
			{
				how = fmt::format("neither CPUID ({}) nor a timing ({}) told the cores apart", cpuid, timings);
				return whole;
			}
			for (int cpu = 0; cpu + step < kMaxCpus; cpu++)
			{
				const bool first = step == 1 ? (cpu & 1) == 0 : cpu < 8;
				if (first && (s_title >> cpu & 1) && (s_title >> (cpu + step) & 1))
					whole.push_back((1ull << cpu) | (1ull << (cpu + step)));
			}
			how = fmt::format("a timing, as CPUID {}: CPUs {} share a core ({})", cpuid, step == 1 ? "2n and 2n+1" : "n and n+8", timings);
			return whole;
		}
	}

	void SetIsolation(bool enabled)
	{
		if (!enabled || !s_ready)
			return;
		std::string how;
		const std::vector<uint64_t> whole = WholeCores(how);
		// the first whole core is left to the system and the rest of the app, as the console's own
		// work favours it; the next four are the emulated CPU cores' and the GPU thread's
		if (whole.size() < 5)
		{
			ps5log::Line("[threads] isolating the emulated CPU cores and the GPU thread: off, {} whole physical cores (5 needed); {}",
				whole.size(), how);
			return;
		}
		{
			// which cores share an L3, for where the four should go next: measured, before any of
			// Cemu's threads, from the first whole core to the first CPU of each other
			const int from = __builtin_ctzll(whole[0]);
			std::string trips;
			for (size_t i = 1; i < whole.size(); i++)
			{
				const int to = __builtin_ctzll(whole[i]);
				trips += fmt::format("{}{}: {:.0f} ns", trips.empty() ? "" : ", ", to, RoundTripNs(from, to));
			}
			ps5log::Line("[threads] a cache line's round trip from CPU {} to CPUs {}", from, trips);
		}
		uint64_t halves = 0;
		for (int i = 0; i < 4; i++)
		{
			const uint64_t core = whole[(size_t)i + 1];
			s_dedicated[i] = __builtin_ctzll(core);
			s_dedicatedCores |= core;
			halves |= core & ~(1ull << s_dedicated[i]);
		}
		s_secondary = s_title & ~s_dedicatedCores;
		s_compilers = s_secondary | halves;
		// the main thread, and so every thread from now on: Cemu's threads place themselves as they
		// take their names (Place)
		const std::string error = WriteAffinity(s_secondary);
		if (!error.empty())
		{
			ps5log::Line("[threads] isolating the emulated CPU cores and the GPU thread: off, the main thread could not be put on "
				"CPUs {} ({})", List(s_secondary), error);
			return;
		}
		s_isolating = true;
		ps5log::Line("[threads] isolated (isolateCores; cores told apart by {}): the emulated CPU cores on CPUs {}, {} and {}, the "
			"GPU thread on {}, each with its core to itself; the compilers on {}; every other thread on {}", how, s_dedicated[0],
			s_dedicated[1], s_dedicated[2], s_dedicated[3], List(s_compilers), List(s_secondary));
	}

	void EndIsolation()
	{
		if (!s_isolating.exchange(false))
			return;
		WriteAffinity(s_title);
		ps5log::Line("[threads] isolation off for this side: the threads started from now on get every CPU");
	}

	unsigned MeasureClockMhz()
	{
		// the shortest of three runs of a million multiplies (about 0.3 ms each), for a core still
		// waking up or another thread
		constexpr uint32_t kSteps = 250000;
		uint64_t best = 0;
		for (int run = 0; run < 3; run++)
		{
			const uint64_t ticks = MultiplyTicks(kSteps);
			if (ticks && (!best || ticks < best))
				best = ticks;
		}
		return ClockMhz(kSteps, best);
	}

	void Initialize()
	{
		std::string error;
		s_title = ReadAffinity(error);
		if (!s_title)
		{
			ps5log::Line("[threads] the title's CPUs could not be read ({}): threads stay where the system puts them", error);
			return;
		}
		for (int cpu = 0; cpu + 1 < kMaxCpus; cpu += 2)
			if (s_title & (3ull << cpu))
				s_cores.push_back(cpu);
		// the first core is left to the system and the rest of the app, when there are enough
		if (s_cores.size() >= 6)
			s_cores.erase(s_cores.begin());
		s_ready = !s_cores.empty();
		ps5log::Line("[threads] {}; the emulated CPU cores get console cores from CPU {} on", Describe(), s_ready ? s_cores.front() : -1);
		// whether the console gives the app its CPU's full clock (about 3500 MHz), every start
		ps5log::Line("[threads] the CPU's clock: about {} MHz (multiplies on the main thread)", MeasureClockMhz());
	}

	std::string Describe()
	{
		int count = 0;
		for (int cpu = 0; cpu < kMaxCpus; cpu++)
			count += (s_title >> cpu) & 1;
		return fmt::format("{} CPUs ({})", count, List(s_title));
	}

	void SetPinning(bool enabled)
	{
		s_pinning = enabled;
		ps5log::Line("[threads] pinning the emulated CPU cores: {}", enabled ? "on (pinCpuThreads)" : "off, the system places every thread");
	}

	void Place(const char* name)
	{
		if (!s_ready || !name)
			return;
		if (s_isolating)
		{
			const int slot = DedicatedSlot(name);
			const uint64_t mask = slot >= 0 ? 1ull << s_dedicated[slot] : IsCompiler(name) ? s_compilers : s_secondary;
			const std::string error = WriteAffinity(mask);
			if (!error.empty())
				ps5log::Line("[threads] {} could not be put on CPUs {} ({})", name, List(mask), error);
			else if (slot >= 0)
				ps5log::Line("[threads] {} on CPU {}, its core to itself (isolated)", name, s_dedicated[slot]);
			return;
		}
		if (!s_pinning)
			return;
		const int slot = SlotOf(name);
		const uint64_t mask = slot >= 0 ? CoreMask(s_cores[(size_t)slot % s_cores.size()]) : s_title;
		const int was = sceKernelGetCurrentCpu();
		const std::string error = WriteAffinity(mask);
		std::lock_guard lock(s_mutex);
		if (!error.empty())
			ps5log::Line("[threads] {} could not be put on CPUs {} ({}); it stays on {}", name, List(mask), error, was);
		else if (slot >= 0)
			ps5log::Line("[threads] {} on CPUs {} (it was on {})", name, List(mask), was);
	}
}

namespace ps5threads
{
	namespace
	{
		std::atomic<bool> s_compileBelowNormal{false};
		// Sony's priorities run from 256 (the highest) to 767, and threads start at 700: Windows'
		// below normal is a step down from normal, and this is one too
		constexpr int kBelowNormalStep = 32, kLowestPriority = 767;
	}

	void SetCompileBelowNormal(bool enabled)
	{
		s_compileBelowNormal = enabled;
	}
}

// The hang dump's signal handler, by a name tools/symbolize-crash.py finds in the ELF
extern "C" void PS5Cemu_HangSignal(int, siginfo_t*, void* context);

namespace ps5threads
{
	namespace
	{
		// the threads watched, each with its stack's bounds taken as it named itself (a signal handler
		// may not ask for them: the call takes locks)
		struct Watched
		{
			std::atomic<uint64_t> thread{0};
			char name[32] = {};
			uint64_t bottom = 0, top = 0;
		};
		constexpr int kWatched = 8; // the GPU thread and three cores, and room for more
		Watched s_watched[kWatched];
		std::atomic<int> s_watchedCount{0};
		constexpr int kHangSignal = SIGUSR2;
		std::once_flag s_hangHandler;
		// the one thread being dumped (DumpStacks signals them in turn) and whether it has answered
		std::atomic<const Watched*> s_dumping{nullptr};
		std::atomic<bool> s_dumped{false};
		constexpr uint64_t kCodeReach = 0x2000000; // the eboot's code is under 32 MB: all of it this near the handler

		void Remember(const char* name)
		{
			if (!name || (std::strncmp(name, "LatteThread", 11) != 0 && std::strncmp(name, "OSSched", 7) != 0))
				return;
			const uint64_t self = (uint64_t)(uintptr_t)pthread_self();
			int index = -1;
			const int count = std::min(s_watchedCount.load(), kWatched);
			for (int i = 0; i < count; i++)
				if (std::strncmp(s_watched[i].name, name, sizeof(s_watched[i].name)) == 0)
					index = i; // a thread of that name again (the next game's): it takes the place
			if (index < 0)
			{
				index = s_watchedCount.fetch_add(1);
				if (index >= kWatched)
					return;
			}
			Watched& watched = s_watched[index];
			watched.thread.store(0);
			std::snprintf(watched.name, sizeof(watched.name), "%s", name);
			watched.bottom = watched.top = 0;
			pthread_attr_t attributes;
			if (pthread_attr_init(&attributes) == 0)
			{
				void* address = nullptr;
				size_t size = 0;
				if (pthread_attr_get_np(pthread_self(), &attributes) == 0 && pthread_attr_getstackaddr(&attributes, &address) == 0 &&
					pthread_attr_getstacksize(&attributes, &size) == 0 && address)
				{
					watched.bottom = (uint64_t)(uintptr_t)address;
					watched.top = watched.bottom + size;
				}
				pthread_attr_destroy(&attributes);
			}
			watched.thread.store(self);
		}

		// "0x" and the hex digits of value at out, returning the end: no library call in a handler
		char* Hex(char* out, uint64_t value)
		{
			*out++ = '0';
			*out++ = 'x';
			char digits[16];
			int count = 0;
			do
			{
				digits[count++] = "0123456789abcdef"[value & 15];
				value >>= 4;
			} while (value && count < 16);
			while (count)
				*out++ = digits[--count];
			return out;
		}

		char* Text(char* out, const char* text)
		{
			while (*text)
				*out++ = *text++;
			return out;
		}
	}
}

extern "C" void PS5Cemu_HangSignal(int, siginfo_t*, void* context)
{
	using namespace ps5threads;
	const Watched* watched = s_dumping.load();
	const auto* machine = reinterpret_cast<const uint64_t*>(&static_cast<const ucontext_t*>(context)->uc_mcontext);
	const uint64_t rip = machine[26], rsp = machine[29] & ~uint64_t(7); // the console's mcontext (crash.cpp)
	const uint64_t handler = (uint64_t)(uintptr_t)&PS5Cemu_HangSignal;
	const uint64_t low = handler > kCodeReach ? handler - kCodeReach : 0, high = handler + kCodeReach;
	char line[1600];
	char* out = Text(line, "[hang] ");
	out = Text(out, watched ? watched->name : "?");
	out = Text(out, ": rip ");
	out = Hex(out, rip);
	out = Text(out, ", code on the stack:");
	// only within the thread's own stack (an emulated core's fiber's is another, and not read)
	if (watched && watched->top && rsp >= watched->bottom && rsp < watched->top)
	{
		int found = 0;
		for (uint64_t at = rsp; at + 8 <= watched->top && at < rsp + 64 * 1024 && found < 48; at += 8)
		{
			const uint64_t word = *reinterpret_cast<const uint64_t*>(at);
			if (word - low < high - low)
			{
				*out++ = ' ';
				out = Hex(out, word);
				found++;
			}
		}
	}
	else
		out = Text(out, " (not its own stack)");
	*out++ = '\n';
	ps5log::WriteRaw(line, (size_t)(out - line));
	s_dumped.store(true);
}

namespace ps5threads
{
	void DumpStacks(const char* why)
	{
		std::call_once(s_hangHandler, [] {
			struct sigaction action{};
			action.sa_sigaction = PS5Cemu_HangSignal;
			action.sa_flags = SA_SIGINFO | SA_RESTART;
			sigemptyset(&action.sa_mask);
			sigaction(kHangSignal, &action, nullptr);
		});
		ps5log::Line("[hang] {}: where Cemu's GPU thread and emulated CPU cores are; handler at {:#x}", why,
			(uint64_t)(uintptr_t)&PS5Cemu_HangSignal);
		ps5log::Flush(); // in the file before the threads' own lines, which the handler writes directly
		const int count = std::min(s_watchedCount.load(), kWatched);
		for (int i = 0; i < count; i++)
		{
			const Watched& watched = s_watched[i];
			const uint64_t thread = watched.thread.load();
			if (!thread)
				continue;
			s_dumping.store(&watched);
			s_dumped.store(false);
			if (pthread_kill((pthread_t)thread, kHangSignal) != 0)
			{
				ps5log::Line("[hang] {} could not be signalled", watched.name);
				continue;
			}
			// its line comes from the handler; a thread that never takes the signal is said so
			for (int waited = 0; waited < 50 && !s_dumped.load(); waited++)
				usleep(10000);
			if (!s_dumped.load())
				ps5log::Line("[hang] {} did not answer", watched.name);
		}
		s_dumping.store(nullptr);
	}
}

// Cemu's pipeline compile threads (patches/cemu, VulkanPipelineCompiler.cpp), as Windows'
// THREAD_PRIORITY_BELOW_NORMAL.
void PS5Cemu_ThreadBelowNormal()
{
	if (!ps5threads::s_compileBelowNormal)
		return;
	void* self = scePthreadSelf();
	int was = 0;
	if (scePthreadGetprio(self, &was) != 0 || was <= 0)
		return;
	const int priority = std::min(was + ps5threads::kBelowNormalStep, ps5threads::kLowestPriority);
	const int result = scePthreadSetprio(self, priority);
	static std::atomic<bool> logged{false};
	if (!logged.exchange(true))
		ps5log::Line("[threads] pipeline compile threads after the first: priority {} -> {}{}", was, priority,
			result == 0 ? "" : fmt::format(" refused ({:#x})", (uint32_t)result));
}

// Cemu's SetThreadName (util/helpers, patches/cemu).
void PS5Cemu_ThreadNamed(const char* name)
{
	ps5profile::Named(name);
	ps5threads::Place(name);
	ps5threads::Remember(name);
}

// The platform library's thread start (PS5_PayloadSDK platform/src/threads.c: a 2 MiB stack in direct
// memory), its __wrap_pthread_create, which tools/link.sh renames so that the one below comes first.
// Weak for a link check, which has no platform library and wraps nothing: pthread_create is then the
// system's own.
extern "C" int ps5_platform_pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*start)(void*),
	void* argument) __attribute__((weak));

// Every thread the app and its libraries start. A thread starts on its creator's CPUs, and one an
// isolated thread starts (the driver starts its flip and submission threads on whichever thread first
// presents or submits) would share that thread's one CPU, where the console does not time-slice threads
// of one priority: such a thread is started on the other threads' CPUs instead.
extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*start)(void*), void* argument)
{
	using namespace ps5threads;
	const auto create = [&] {
		return ps5_platform_pthread_create ? ps5_platform_pthread_create(thread, attributes, start, argument) :
											 pthread_create(thread, attributes, start, argument);
	};
	uint64_t mine = 0;
	if (!s_isolating.load(std::memory_order_relaxed) || scePthreadGetaffinity(scePthreadSelf(), &mine) != 0 ||
		__builtin_popcountll(mine) != 1 || !(mine & s_dedicatedCores))
		return create();
	scePthreadSetaffinity(scePthreadSelf(), s_secondary);
	const int result = create();
	scePthreadSetaffinity(scePthreadSelf(), mine);
	return result;
}
