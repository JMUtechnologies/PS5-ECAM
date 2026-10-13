// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the sampling profiler (profiler.h).

#include "profiler.h"
#include "log.h"
#include "../app/paths.h"

#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// The profiler's signal handler, by a name tools/profile-report.py finds in the ELF: the boot log
// gives its address, which places the eboot's code.
extern "C" void PS5Cemu_ProfileSignal(int, siginfo_t*, void* context);

namespace
{
	constexpr int kRip = 26, kRsp = 29;		   // the console's machine context, as qwords (crash.cpp)
	constexpr unsigned kPeriodUs = 4000;	   // of the app's CPU time, all its threads' together
	constexpr int kCallers = 6;				   // code addresses kept from the stack
	constexpr uint64_t kStackBytes = 512;	   // looked at above RSP, at most
	constexpr uint64_t kCodeReach = 0x2000000; // the eboot's code is under 32 MB: all of it this near the handler

	struct Sample
	{
		uint64_t rip;
		uint64_t thread; // pthread_self()
		uint64_t callers[kCallers];
	};
	static_assert(sizeof(Sample) == 64);

	// a bounded queue the handlers add to, on any thread, and the writer takes from (D. Vyukov's)
	struct Slot
	{
		std::atomic<uint64_t> sequence;
		Sample sample;
	};
	constexpr uint64_t kSlots = 1 << 15;
	Slot* s_ring;
	std::atomic<uint64_t> s_head{0};
	std::atomic<uint64_t> s_dropped{0};
	uint64_t s_codeLow, s_codeHigh;

	// Each thread's stack, once the writer has seen the thread: before that, and on a stack of
	// another's (an emulated CPU core's fibers), the stack is not read. A thread's stack ends at an
	// unmapped page, which a read past its top would fault on.
	struct Stack
	{
		std::atomic<uint64_t> thread;
		std::atomic<uint64_t> bottom, top;
	};
	constexpr uint64_t kStacks = 1024;
	Stack s_stacks[kStacks];

	uint64_t Hash(uint64_t thread)
	{
		return (thread >> 6) * 0x9e3779b97f4a7c15ull >> 54; // 10 bits: kStacks
	}

	const Stack* FindStack(uint64_t thread)
	{
		for (uint64_t i = Hash(thread), n = 0; n < kStacks; i = (i + 1) % kStacks, n++)
		{
			const uint64_t at = s_stacks[i].thread.load(std::memory_order_acquire);
			if (at == thread)
				return &s_stacks[i];
			if (at == 0)
				return nullptr;
		}
		return nullptr;
	}

	// The emulated CPU cores' fibers' stacks (port/cemu/FiberPS5.cpp), so that a sample taken on one
	// is read too: a stack that holds the sample's RSP is the running fiber's, alive while it runs.
	// An entry is free while its top is 0; bounds are written bottom first, read top first.
	struct FiberStackEntry
	{
		std::atomic<uint64_t> bottom, top;
	};
	constexpr uint64_t kFiberStacks = 2048;
	FiberStackEntry s_fiberStacks[kFiberStacks];
	std::mutex s_fiberStacksLock; // the adders' and removers', never the handler's

	bool FindFiberStack(uint64_t rsp, uint64_t& bottom, uint64_t& top)
	{
		for (FiberStackEntry& entry : s_fiberStacks)
		{
			const uint64_t t = entry.top.load(std::memory_order_acquire);
			if (t == 0 || rsp >= t)
				continue;
			const uint64_t b = entry.bottom.load(std::memory_order_acquire);
			if (rsp >= b && entry.top.load(std::memory_order_acquire) == t)
			{
				bottom = b, top = t;
				return true;
			}
		}
		return false;
	}

	// the writer's only
	void SetStack(uint64_t thread, uint64_t bottom, uint64_t top)
	{
		for (uint64_t i = Hash(thread), n = 0; n < kStacks; i = (i + 1) % kStacks, n++)
		{
			Stack& stack = s_stacks[i];
			const uint64_t at = stack.thread.load(std::memory_order_relaxed);
			if (at != thread && at != 0)
				continue;
			// a thread that reuses an ended one's handle: not read until both bounds are its own
			stack.top.store(0, std::memory_order_release);
			stack.bottom.store(bottom, std::memory_order_release);
			stack.top.store(top, std::memory_order_release);
			stack.thread.store(thread, std::memory_order_release);
			return;
		}
	}

	// The threads' names, as they name themselves through Cemu's SetThreadName (and the main thread):
	// the app imports nothing more to ask the system for them, so that nothing a console's libkernel
	// might lack keeps the app from starting. Other threads (the driver's) go by their handles.
	constexpr int kNamed = 256;
	struct Named
	{
		std::atomic<uint64_t> thread;
		char name[32];
	};
	Named s_named[kNamed];
	std::atomic<int> s_namedCount{0};

	void AddNamed(uint64_t thread, const char* name)
	{
		const int index = s_namedCount.fetch_add(1, std::memory_order_relaxed);
		if (index >= kNamed)
			return;
		std::snprintf(s_named[index].name, sizeof(s_named[index].name), "%s", name);
		s_named[index].thread.store(thread, std::memory_order_release);
	}

	// the newest name a thread took (a handle can come back for a new thread)
	std::string NameOf(uint64_t thread)
	{
		const int count = std::min(s_namedCount.load(std::memory_order_acquire), kNamed);
		for (int i = count - 1; i >= 0; i--)
			if (s_named[i].thread.load(std::memory_order_acquire) == thread)
				return s_named[i].name;
		return {};
	}

	// A line for each thread seen, and again when its name or stack changes: "handle bottom top name"
	void Describe(FILE* threads, uint64_t thread, std::unordered_map<uint64_t, std::string>& written)
	{
		const std::string name = NameOf(thread);
		uint64_t bottom = 0, top = 0;
		pthread_attr_t attributes;
		if (pthread_attr_init(&attributes) == 0)
		{
			void* address = nullptr;
			size_t size = 0;
			if (pthread_attr_get_np((pthread_t)thread, &attributes) == 0 && pthread_attr_getstackaddr(&attributes, &address) == 0 &&
				pthread_attr_getstacksize(&attributes, &size) == 0 && address)
			{
				bottom = (uint64_t)(uintptr_t)address;
				top = bottom + size;
			}
			pthread_attr_destroy(&attributes);
		}
		if (top)
			SetStack(thread, bottom, top);
		char line[160];
		std::snprintf(line, sizeof(line), "%llx %llx %llx %s", (unsigned long long)thread, (unsigned long long)bottom,
			(unsigned long long)top, name.c_str());
		std::string& last = written[thread];
		if (last != line)
		{
			std::fprintf(threads, "%s\n", line);
			last = line;
		}
	}

	// Where the timer is refused: Cemu's GPU thread and emulated CPU cores, signalled in turn
	void Interrupt()
	{
		pthread_set_name_np(pthread_self(), "ps5profileTick");
		for (;;)
		{
			usleep(kPeriodUs);
			const int count = std::min(s_namedCount.load(std::memory_order_acquire), kNamed);
			for (int i = 0; i < count; i++)
			{
				const uint64_t thread = s_named[i].thread.load(std::memory_order_acquire);
				if (thread && (std::strncmp(s_named[i].name, "LatteThread", 11) == 0 || std::strncmp(s_named[i].name, "OSSched", 7) == 0))
					pthread_kill((pthread_t)thread, SIGPROF);
			}
		}
	}

	void Write(FILE* samples, FILE* threads)
	{
		pthread_set_name_np(pthread_self(), "ps5profile");
		AddNamed((uint64_t)(uintptr_t)pthread_self(), "ps5profile");
		std::vector<Sample> batch;
		batch.reserve(8192);
		std::unordered_map<uint64_t, uint64_t> checked; // thread: the round its name and stack were looked at
		std::unordered_map<uint64_t, std::string> written;
		uint64_t tail = 0, dropped = 0;
		for (uint64_t round = 1;; round++)
		{
			usleep(250000);
			batch.clear();
			for (;;)
			{
				Slot& slot = s_ring[tail % kSlots];
				if (slot.sequence.load(std::memory_order_acquire) != tail + 1)
					break;
				batch.push_back(slot.sample);
				slot.sequence.store(tail + kSlots, std::memory_order_release);
				tail++;
			}
			// a thread's name and stack when first seen, and every two seconds after (a new thread
			// can take an ended one's handle)
			for (const Sample& sample : batch)
			{
				uint64_t& at = checked[sample.thread];
				if (at != 0 && round - at < 8)
					continue;
				at = round;
				Describe(threads, sample.thread, written);
			}
			if (!batch.empty())
				std::fwrite(batch.data(), sizeof(Sample), batch.size(), samples);
			std::fflush(samples);
			std::fflush(threads);
			const uint64_t nowDropped = s_dropped.load(std::memory_order_relaxed);
			if (nowDropped != dropped)
			{
				ps5log::Line("[profile] {} samples dropped: the writer fell behind", nowDropped - dropped);
				dropped = nowDropped;
			}
		}
	}

}

extern "C" void PS5Cemu_ProfileSignal(int, siginfo_t*, void* context)
{
	const auto* machine = reinterpret_cast<const uint64_t*>(&static_cast<const ucontext_t*>(context)->uc_mcontext);
	uint64_t position = s_head.load(std::memory_order_relaxed);
	Slot* slot;
	for (;;)
	{
		slot = &s_ring[position % kSlots];
		const int64_t ahead = (int64_t)(slot->sequence.load(std::memory_order_acquire) - position);
		if (ahead == 0)
		{
			if (s_head.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
				break;
		}
		else if (ahead < 0)
		{
			s_dropped.fetch_add(1, std::memory_order_relaxed); // full: the writer is behind
			return;
		}
		else
			position = s_head.load(std::memory_order_relaxed);
	}
	Sample& sample = slot->sample;
	sample.rip = machine[kRip];
	sample.thread = (uint64_t)(uintptr_t)pthread_self();
	int found = 0;
	const uint64_t rsp = machine[kRsp] & ~uint64_t(7);
	uint64_t bottom = 0, top = 0;
	if (const Stack* stack = FindStack(sample.thread))
	{
		bottom = stack->bottom.load(std::memory_order_acquire);
		top = stack->top.load(std::memory_order_acquire);
	}
	// not on the thread's own stack: on an emulated CPU core's fiber's, if on any
	if (!(top && rsp >= bottom && rsp < top) && !FindFiberStack(rsp, bottom, top))
		top = 0;
	if (top && rsp >= bottom && rsp < top)
		for (uint64_t at = rsp, end = std::min(top, rsp + kStackBytes); at + 8 <= end && found < kCallers; at += 8)
		{
			const uint64_t word = *reinterpret_cast<const uint64_t*>(at);
			if (word - s_codeLow < s_codeHigh - s_codeLow)
				sample.callers[found++] = word;
		}
	while (found < kCallers)
		sample.callers[found++] = 0;
	slot->sequence.store(position + 1, std::memory_order_release);
}

namespace ps5profile
{
	void Start()
	{
		s_ring = new Slot[kSlots];
		for (uint64_t i = 0; i < kSlots; i++)
			s_ring[i].sequence.store(i, std::memory_order_relaxed);
		const uint64_t handler = (uint64_t)(uintptr_t)&PS5Cemu_ProfileSignal;
		s_codeLow = handler > kCodeReach ? handler - kCodeReach : 0;
		s_codeHigh = handler + kCodeReach;

		const std::string logs = ps5paths::kLogs;
		std::rename((logs + "/profile.bin").c_str(), (logs + "/profile.prev.bin").c_str());
		std::rename((logs + "/profile-threads.txt").c_str(), (logs + "/profile-threads.prev.txt").c_str());
		FILE* samples = std::fopen((logs + "/profile.bin").c_str(), "wb");
		FILE* threads = std::fopen((logs + "/profile-threads.txt").c_str(), "w");
		if (!samples || !threads)
		{
			ps5log::Line("[profile] not sampling: {} could not be written", logs);
			return;
		}
		struct sigaction action{};
		sigemptyset(&action.sa_mask);
		action.sa_flags = SA_SIGINFO | SA_RESTART;
		action.sa_sigaction = PS5Cemu_ProfileSignal;
		if (sigaction(SIGPROF, &action, nullptr) != 0)
		{
			ps5log::Line("[profile] not sampling: SIGPROF's handler is refused ({})", std::strerror(errno));
			return;
		}
		AddNamed((uint64_t)(uintptr_t)pthread_self(), "main");
		std::thread(Write, samples, threads).detach();

		itimerval timer{};
		timer.it_interval.tv_usec = kPeriodUs;
		timer.it_value.tv_usec = kPeriodUs;
		if (setitimer(ITIMER_PROF, &timer, nullptr) != 0)
		{
			// The firmware refuses some CPU-time calls to a title (ProsperoEden's notes): then the
			// threads that matter, the GPU thread and the emulated CPU cores, are interrupted in turn
			// every period instead (a sleeping one too, which then shows where it waits)
			const int error = errno;
			std::thread(Interrupt).detach();
			ps5log::Line("[profile] the CPU-time timer is refused ({}): Cemu's GPU thread and emulated CPU cores are sampled every "
						 "{} ms into {}/profile.bin instead; handler at {:#x}",
				std::strerror(error), kPeriodUs / 1000, logs, handler);
			return;
		}
		ps5log::Line("[profile] sampling every {} ms of the app's CPU time into {}/profile.bin; handler at {:#x}",
			kPeriodUs / 1000, logs, handler);
	}

	void Named(const char* name)
	{
		if (s_ring && name)
			AddNamed((uint64_t)(uintptr_t)pthread_self(), name);
	}

	void FiberStack(const void* bottom, size_t bytes, bool add)
	{
		const uint64_t low = (uint64_t)(uintptr_t)bottom, high = low + bytes;
		std::lock_guard lock(s_fiberStacksLock);
		for (FiberStackEntry& entry : s_fiberStacks)
		{
			if (add && entry.top.load(std::memory_order_relaxed) == 0)
			{
				entry.bottom.store(low, std::memory_order_release);
				entry.top.store(high, std::memory_order_release);
				return;
			}
			if (!add && entry.bottom.load(std::memory_order_relaxed) == low && entry.top.load(std::memory_order_relaxed) == high)
			{
				entry.top.store(0, std::memory_order_release);
				return;
			}
		}
	}
}
