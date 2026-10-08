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
#include <vector>

extern "C"
{
	int sceKernelGetCurrentCpu();
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
		uint64_t s_dedicated[4] = {}; // OSSched[core=0..2], LatteThread: a physical core's CPUs each
		uint64_t s_secondary = 0;	  // every other thread's CPUs
		// the threads named so far, by Sony's handle, to move when isolation starts
		std::vector<std::pair<void*, std::string>> s_named; // under s_mutex

		// The dedicated core of a thread of that name, or -1
		int DedicatedSlot(const char* name)
		{
			if (std::strcmp(name, "LatteThread") == 0)
				return 3;
			const int core = SlotOf(name);
			return core >= 0 && core < 3 ? core : -1; // the Wii U's three
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
	}

	void SetIsolation(bool enabled)
	{
		if (!enabled || !s_ready)
			return;
		std::string error;
		const auto cores = PhysicalCores(error);
		// the cores with both logical CPUs, in order; the first one is left to the system and the
		// rest of the app, as the console's own work favours it
		std::vector<uint64_t> whole;
		for (const auto& [id, cpus] : cores)
			if (__builtin_popcountll(cpus) == 2)
				whole.push_back(cpus);
		if (whole.size() < 6)
		{
			ps5log::Line("[threads] isolating the emulated CPU cores and the GPU thread: off, {}", error.empty() ?
				fmt::format("{} whole physical cores (6 needed)", whole.size()) : error);
			return;
		}
		uint64_t dedicated = 0;
		for (int i = 0; i < 4; i++)
		{
			s_dedicated[i] = whole[i + 1];
			dedicated |= s_dedicated[i];
		}
		s_secondary = s_title & ~dedicated;
		s_isolating = true;
		// the main thread, and so every thread it starts from now on, on the other cores
		WriteAffinity(s_secondary);
		std::vector<std::pair<void*, std::string>> named;
		{
			std::lock_guard lock(s_mutex);
			named = s_named;
		}
		int moved = 0;
		for (const auto& [thread, name] : named)
			if (DedicatedSlot(name.c_str()) < 0 && scePthreadSetaffinity(thread, s_secondary) == 0)
				moved++;
		ps5log::Line("[threads] isolated (isolateCores): emulated CPU cores on CPUs {}, {} and {}, the GPU thread on {}; every "
			"other thread on {} ({} named threads moved there)", List(s_dedicated[0]), List(s_dedicated[1]), List(s_dedicated[2]),
			List(s_dedicated[3]), List(s_secondary), moved);
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
			const uint64_t mask = slot >= 0 ? s_dedicated[slot] : s_secondary;
			const std::string error = WriteAffinity(mask);
			if (!error.empty())
				ps5log::Line("[threads] {} could not be put on CPUs {} ({})", name, List(mask), error);
			else if (slot >= 0)
				ps5log::Line("[threads] {} on CPUs {} (isolated)", name, List(mask));
			return;
		}
		{
			std::lock_guard lock(s_mutex);
			s_named.emplace_back(scePthreadSelf(), name);
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
