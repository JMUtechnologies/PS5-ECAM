// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: getting out of the app sandbox.
//
// A homebrew title starts sandboxed: no /data, and no JIT memory for Cemu's recompiler. At start
// PS5Cemu asks, in order:
//  1. etaHEN (or OnionHEN, which watches the same file) to jailbreak this process: write
//     {"PID":n} to /download0/etahen_jailbreak and wait for the HEN to consume it. The HEN only
//     does this for title IDs in its app-jailbreak allowlist, so PPSA99360 must be listed there.
//     This is how PS5SX2 gets JIT memory and /data (its ProsperoHenJailbreak.cpp).
//  2. failing that, the console's ELF loader (elfldr, port 9021, which etaHEN provides) to run the
//     bundled helper /app0/sandbox-elevator.elf, which grants filesystem access only
//     (ps5-native-app-boilerplate's sandbox-elevation, as ProsperoEden does).
// Without the HEN's JIT memory, Cemu's recompiler takes executable direct memory from the platform
// layer instead (ps5platform/exec.h), which any title gets; only when that fails too does Cemu run
// its interpreter.

#pragma once

#include <string>
#include <vector>

namespace ps5privilege
{
	struct Result
	{
		bool jailbroken = false; // the HEN jailbroke the process
		bool filesystem = false; // /data is reachable
		bool jit = false;		 // JIT memory can be created (the recompiler can run)
		std::string summary;	 // one line for the log and the launcher's status
	};

	// Blocking, at most a few seconds; call it before starting threads.
	Result Acquire();
	const Result& Current();
	// Once the settings are read, still before threads start: when a game folder, or a drive
	// plugged in, is there but refused (a HEN that opened /data but not the drives), the bundled
	// helper is asked for the whole filesystem. Current().summary says what came of it.
	void ReachFolders(const std::vector<std::string>& folders);
}

// For Cemu (ActiveSettings::GetCPUMode).
bool PS5_JitAvailable();
