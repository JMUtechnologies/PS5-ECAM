// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.h"
#include "kernel.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
	std::mutex s_mutex;
	FILE* s_file = nullptr; // flushed after every line, so a raw write(2) can follow its content
	std::string s_path;
	std::string s_pending; // lines from before /data was reachable
}

namespace ps5log
{
	void Open(const char* folder)
	{
		std::lock_guard lock(s_mutex);
		if (s_file)
			return;
		mkdir(folder, 0777);
		s_path = std::string(folder) + "/boot.log";
		const std::string previous = std::string(folder) + "/boot.prev.log";
		rename(s_path.c_str(), previous.c_str());
		s_file = fopen(s_path.c_str(), "w");
		if (s_file)
		{
			std::fputs(s_pending.c_str(), s_file);
			std::fflush(s_file);
		}
		s_pending.clear();
		s_pending.shrink_to_fit();
	}

	const char* Path()
	{
		return s_path.c_str();
	}

	void Write(std::string_view line)
	{
		// milliseconds since the title started, which is what matters when reading a boot log
		const uint64_t ms = sceKernelGetProcessTime() / 1000;
		const std::string text = fmt::format("[{:6}.{:03}] {}\n", ms / 1000, ms % 1000, line);
		std::lock_guard lock(s_mutex);
		std::fputs(text.c_str(), stdout);
		std::fflush(stdout);
		if (s_file)
		{
			std::fputs(text.c_str(), s_file);
			// flushed to the kernel at once, so a crash does not take the last lines with it
			std::fflush(s_file);
		}
		else if (s_pending.size() < 256 * 1024)
			s_pending += text;
	}
}

// Cemu's crash report (patches/cemu, ExceptionHandler), line by line from its signal handler: with
// write(2) on the file's descriptor, no lock taken (the crashed thread may hold the log's) and no
// buffer, so each line is on disk before the next. Every line is "[crash] ...".
void PS5Cemu_CrashLine(std::string_view text, bool newLine)
{
	static bool s_lineStarted = false;
	FILE* file = s_file;
	if (!file)
		return;
	const int fd = fileno(file);
	if (!s_lineStarted)
		(void)!write(fd, "[crash] ", 8);
	(void)!write(fd, text.data(), text.size());
	s_lineStarted = !newLine;
	if (newLine)
		(void)!write(fd, "\n", 1);
}
