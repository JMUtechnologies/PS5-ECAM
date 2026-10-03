// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash.h"
#include "log.h"

#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <string_view>

// The boot log, from a signal handler: write(2), no lock (log.cpp)
void PS5Cemu_CrashLine(std::string_view text, bool newLine);

namespace
{
	// One "[crash] ..." line, formatted on the stack: the crashed thread may hold the heap's lock.
	template<typename... Args>
	void Line(fmt::format_string<Args...> format, Args&&... args)
	{
		char buffer[1536];
		const auto result = fmt::format_to_n(buffer, sizeof(buffer), format, std::forward<Args>(args)...);
		PS5Cemu_CrashLine(std::string_view(buffer, std::min(result.size, sizeof(buffer))), true);
	}

	// The console's machine context, as qwords: its layout is FreeBSD's six qwords on (word 7 is
	// RDI, 23 the fault address, 26 RIP, 29 RSP)
	constexpr int kRip = 26, kRsp = 29;

	void Handler(int sig, siginfo_t* info, void* context)
	{
		const auto* machine = reinterpret_cast<const uint64_t*>(&static_cast<const ucontext_t*>(context)->uc_mcontext);
		const uintptr_t self = reinterpret_cast<uintptr_t>(&Handler);
		const uintptr_t fault = reinterpret_cast<uintptr_t>(info->si_addr);
		Line("signal {} at {:#x} (PS5CEMU-HAR's own handler: Cemu does not run in this session)", sig, fault);
		Line("PS5: rip {:#x}, rsp {:#x}, handler at {:#x}", machine[kRip], machine[kRsp], self);
		{
			char words[32 * 17];
			size_t used = 0;
			for (int i = 0; i < 32 && used < sizeof(words); i++)
				used += fmt::format_to_n(words + used, sizeof(words) - used, " {:x}", machine[i]).size;
			Line("PS5: machine context:{}", std::string_view(words, std::min(used, sizeof(words))));
		}

		// the words on the crashed code's stack that point into the app's code: the return
		// addresses of the frames it was in. From its RSP, or above RSP's page when the fault is in
		// that page (a stack overflow), up to the top of the thread's stack where it is known.
		uintptr_t here = machine[kRsp];
		if ((here ^ fault) < 0x4000)
			here = (here | 0x3fff) + 1;
		here &= ~static_cast<uintptr_t>(7);
		uintptr_t top = here + 16 * 1024;
		bool bounded = false;
		pthread_attr_t attributes;
		if (pthread_attr_init(&attributes) == 0)
		{
			void* stackAddress = nullptr;
			size_t stackSize = 0;
			if (pthread_attr_get_np(pthread_self(), &attributes) == 0 && pthread_attr_getstackaddr(&attributes, &stackAddress) == 0 &&
				pthread_attr_getstacksize(&attributes, &stackSize) == 0 && here >= reinterpret_cast<uintptr_t>(stackAddress) &&
				here < reinterpret_cast<uintptr_t>(stackAddress) + stackSize)
			{
				top = std::min<uintptr_t>(reinterpret_cast<uintptr_t>(stackAddress) + stackSize, here + 512 * 1024);
				bounded = true;
			}
			pthread_attr_destroy(&attributes);
		}
		const uintptr_t low = self > 0x3000000 ? self - 0x3000000 : 0x10000, high = self + 0x3000000;
		char frames[96 * 17];
		size_t used = 0;
		int found = 0;
		for (uintptr_t at = here; here && at + 8 <= top && found < 96; at += 8)
		{
			const uintptr_t word = *reinterpret_cast<const uintptr_t*>(at);
			if (word >= low && word < high && used < sizeof(frames))
			{
				used += fmt::format_to_n(frames + used, sizeof(frames) - used, " {:x}", word).size;
				found++;
			}
		}
		Line("PS5: code addresses on the stack ({} bytes{}):{}", top - here, bounded ? "" : ", stack bounds unknown",
			std::string_view(frames, std::min(used, sizeof(frames))));
		_Exit(1);
	}
}

namespace ps5crash
{
	void Install()
	{
		struct sigaction action{};
		sigfillset(&action.sa_mask); // nothing interrupts the report
		action.sa_flags = SA_SIGINFO;
		action.sa_sigaction = Handler;
		for (int sig : {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV, SIGSYS, SIGTRAP})
			sigaction(sig, &action, nullptr);
		ps5log::Line("[main] crash reports: the app's own handler, until an emulator that has one starts");
	}
}
