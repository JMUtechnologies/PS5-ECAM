// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the port's boot log, /data/ps5cemu/logs/boot.log. Every line also goes to stdout, which
// the console's klog shows. From StartWriter on, lines are written by a thread of the log's own (a
// write to /data can take 25 ms on the console): Flush before the process ends or the file is read.
// Cemu's own log (log.txt) is separate and lives in /data/ps5cemu. Open keeps the four sessions
// before: boot.prev.log, boot.2.log to boot.4.log, each with Cemu's log of it as cemu.prev.txt,
// cemu.2.txt and so on.

#pragma once

#include <fmt/format.h>
#include <string_view>

namespace ps5log
{
	void Open(const char* folder);
	// The writer thread, once the process may have threads: until then (and without /data) a line is
	// written by the thread that writes it. The elevation helper must find the process with no thread
	// but the main one (ps5privilege::ReachFolders), so this comes after it.
	void StartWriter();
	void Write(std::string_view line);
	const char* Path();
	// From a signal handler: bytes straight to the file with write(2), no lock and no allocation
	// (the interrupted thread may hold either). The caller ends its line with '\n'.
	void WriteRaw(const char* text, size_t length);
	// Waits, at most this long, until every line written so far is in the file. Polled with no
	// lock, so a signal handler may call it too.
	void Flush(int timeoutMs = 1000);
	// The same, and what stderr holds (BufferStderr): before the process ends or a log is copied.
	// Not from a signal handler.
	void FlushAll();
	// stderr, already pointed at a file (logs/driver.log), fully buffered, and written by the log's
	// thread once a second: the Vulkan driver prints on it from the thread that submits, Cemu's GPU
	// thread, which a write to /data would hold up.
	void BufferStderr();

	template<typename... Args>
	void Line(fmt::format_string<Args...> format, Args&&... args)
	{
		Write(fmt::format(format, std::forward<Args>(args)...));
	}
}
