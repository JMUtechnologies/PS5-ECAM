// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: an update's files (updates.cpp): the release's ZIP unpacked, and its folders made, moved and
// deleted, a few files at a time, as install.cpp copies a game. The PS5's drive takes about 10 ms to
// make, link or move a file: one at a time, the 1,852 files of 5.0.1 took a minute to install (#78).
// tools/tests/test-update.sh runs it on a PC.

#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace ps5update
{
	// How many files it works on at once
	constexpr int kWorkers = 4;

	// work(worker, i) for each i below count, shared out among kWorkers threads (worker: which one, 0
	// to kWorkers - 1), until one returns false: false then, the others ending at their next item
	template <class Work>
	bool InParallel(size_t count, Work work)
	{
		std::atomic<size_t> next{0};
		std::atomic<bool> failed{false};
		auto worker = [&](int w) {
			for (size_t i = next++; i < count && !failed; i = next++)
				if (!work(w, i))
					failed = true;
		};
		std::vector<std::thread> workers;
		for (int w = 1; w < kWorkers; w++)
			workers.emplace_back(worker, w);
		worker(0);
		for (std::thread& thread : workers)
			thread.join();
		return !failed;
	}

	// Each file's folder under under made, one at a time before the workers, rather than by all at once
	void MakeFolders(const std::filesystem::path& under, const std::vector<std::filesystem::path>& files);
	// folder and all in it deleted, its files kWorkers at a time
	void RemoveAll(const std::filesystem::path& folder);
	// The release's PPSA99360 folder unpacked into folder (made new); its files, relative to it, in the
	// archive's order. False, with why (in the player's language), when it could not be, or stop was set
	// meanwhile (a game starting)
	bool Unpack(const std::filesystem::path& archive, const std::filesystem::path& folder, std::vector<std::filesystem::path>& files,
		std::string& error, const std::atomic<bool>& stop);
}
