// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.h"
#include "kernel.h"

#include <pthread.h>
#include <pthread_np.h>

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace
{
	// Lines are written by a thread of the log's own: a write to a file under /data can take 25 ms
	// and more on the console (ProsperoEden measured 20 unbuffered lines at 474 ms), and lines come
	// from the threads a game's frame rate rests on (Cemu's GPU thread among them). A line waits in
	// memory, the writer takes what has gathered and writes it with write(2), to the boot log and to
	// stdout (which the console's klog shows). Made once and never torn down, so a line written as
	// the process ends finds them still there.
	struct State
	{
		std::mutex mutex;
		std::condition_variable wake;
		std::string queued;					  // lines for the writer
		std::atomic<size_t> unwritten{0};	  // bytes queued or being written
		std::atomic<bool> writerStarted{false}; // StartWriter: until then the line's own thread writes it
	};
	State& S()
	{
		static State* state = new State;
		return *state;
	}

	std::atomic<FILE*> s_file{nullptr}; // its descriptor is the boot log's, for the writer and a crash report
	std::string s_path;
	std::string s_pending; // lines from before /data was reachable

	// The last few sessions are kept, so a crash's log survives the restarts after it: newest is
	// <stem>.prev<ext>, then <stem>.2<ext> up to <stem>.<kKept - 1><ext>
	constexpr int kKept = 5;

	std::string Older(const std::string& folder, const char* stem, const char* ext, int age)
	{
		return age == 1 ? fmt::format("{}/{}.prev{}", folder, stem, ext) : fmt::format("{}/{}.{}{}", folder, stem, age, ext);
	}

	void Rotate(const std::string& current, const std::string& folder, const char* stem, const char* ext)
	{
		struct stat st{};
		if (stat(current.c_str(), &st) != 0)
			return;
		for (int age = kKept - 1; age > 1; age--)
			rename(Older(folder, stem, ext, age - 1).c_str(), Older(folder, stem, ext, age).c_str());
		rename(current.c_str(), Older(folder, stem, ext, 1).c_str());
	}

	void WriteAll(int fd, const char* data, size_t size)
	{
		while (size)
		{
			const ssize_t written = write(fd, data, size);
			if (written < 0 && errno == EINTR)
				continue;
			if (written <= 0)
				return;
			data += written;
			size -= (size_t)written;
		}
	}

	void Writer()
	{
		pthread_set_name_np(pthread_self(), "ps5log");
		State& state = S();
		std::string batch;
		std::unique_lock lock(state.mutex);
		for (;;)
		{
			state.wake.wait(lock, [&] { return !state.queued.empty(); });
			batch.swap(state.queued);
			lock.unlock();
			WriteAll(STDOUT_FILENO, batch.data(), batch.size());
			if (FILE* file = s_file.load())
				WriteAll(fileno(file), batch.data(), batch.size());
			state.unwritten.fetch_sub(batch.size());
			batch.clear();
			lock.lock();
		}
	}

	void Queue(std::string&& text)
	{
		State& state = S();
		{
			std::lock_guard lock(state.mutex);
			state.unwritten.fetch_add(text.size());
			if (state.queued.empty())
				state.queued = std::move(text);
			else
				state.queued += text;
		}
		state.wake.notify_one();
	}

	// stderr's descriptor, which keeps where the title's stderr went: lines that are not the driver's
	// still go there
	int s_stderr = -1;

	bool IsDriverMessage(std::string_view line)
	{
		return line.starts_with("radv") || line.starts_with("wsi/") || line.starts_with("MESA") || line.starts_with("ACO");
	}

	void ForwardStderr(int from)
	{
		pthread_set_name_np(pthread_self(), "ps5log-stderr");
		std::string pending;
		char buffer[4096];
		for (;;)
		{
			const ssize_t got = read(from, buffer, sizeof(buffer));
			if (got < 0 && errno == EINTR)
				continue;
			if (got <= 0)
				return;
			pending.append(buffer, (size_t)got);
			size_t end;
			while ((end = pending.find('\n')) != std::string::npos)
			{
				const std::string_view line(pending.data(), end);
				// the boot log's line goes to stdout too, so the klog keeps it either way
				if (IsDriverMessage(line))
					ps5log::Write(fmt::format("[driver] {}", line));
				else
					(void)!write(s_stderr, pending.data(), end + 1);
				pending.erase(0, end + 1);
			}
			// a line with no end yet that will not fit: passed on as it is
			if (pending.size() > 16 * 1024)
			{
				(void)!write(s_stderr, pending.data(), pending.size());
				pending.clear();
			}
		}
	}
}

namespace ps5log
{
	void Open(const char* folder)
	{
		State& state = S();
		std::lock_guard lock(state.mutex);
		if (s_file.load())
			return;
		mkdir(folder, 0777);
		s_path = std::string(folder) + "/boot.log";
		Rotate(s_path, folder, "boot", ".log");
		// Cemu's log.txt, one folder up, is rewritten each start: kept beside the boot logs it goes with
		std::string root = folder;
		root.erase(root.find_last_of('/'));
		Rotate(root + "/log.txt", folder, "cemu", ".txt");
		FILE* file = fopen(s_path.c_str(), "w");
		if (!file)
			return;
		s_file.store(file);
		// the lines from before, to the file only: they went to stdout as they came
		WriteAll(fileno(file), s_pending.data(), s_pending.size());
		s_pending.clear();
		s_pending.shrink_to_fit();
	}

	void StartWriter()
	{
		State& state = S();
		std::lock_guard lock(state.mutex);
		if (state.writerStarted.load() || !s_file.load())
			return;
		std::thread(Writer).detach();
		state.writerStarted.store(true);
		// what is still queued as the process ends normally (it restarts through LoadExec, which
		// flushes first)
		std::atexit([] { ps5log::Flush(1000); });
	}

	const char* Path()
	{
		return s_path.c_str();
	}

	void WriteRaw(const char* text, size_t length)
	{
		if (FILE* file = s_file.load())
			(void)!write(fileno(file), text, length);
	}

	void Flush(int timeoutMs)
	{
		// polled, with no lock: also from a signal handler (a crash report, before its own lines)
		for (int waited = 0; S().unwritten.load() != 0 && waited < timeoutMs; waited++)
			usleep(1000);
	}

	void FlushAll()
	{
		std::fflush(stderr);
		Flush();
	}

	void ForwardDriverMessages()
	{
		static std::once_flag s_once;
		std::call_once(s_once, [] {
			// The stream stderr is moved to the pipe, not descriptor 2: a title may not dup2 (the
			// console ends the app at the call, 0xA002030A "illegal function call", seen on 13.60;
			// PS5_PayloadSDK's ps5platform/klog.h moves the stream for the same reason). Everything
			// written through stderr (the driver's fprintf) arrives; a raw write(2) to descriptor 2
			// goes where it did.
			int ends[2];
			if (pipe(ends) != 0)
				return;
			// a writer never waits on the forwarding thread: with the pipe full, its line is lost
			fcntl(ends[1], F_SETFL, fcntl(ends[1], F_GETFL) | O_NONBLOCK);
			FILE* stream = fdopen(ends[1], "w");
			if (!stream)
			{
				close(ends[0]);
				close(ends[1]);
				return;
			}
			setvbuf(stream, nullptr, _IOLBF, 0);
			std::fflush(stderr);
			s_stderr = STDERR_FILENO;
			stderr = stream;
			std::thread(ForwardStderr, ends[0]).detach();
		});
	}

	void Write(std::string_view line)
	{
		// milliseconds since the title started, which is what matters when reading a boot log
		const uint64_t ms = sceKernelGetProcessTime() / 1000;
		std::string text = fmt::format("[{:6}.{:03}] {}\n", ms / 1000, ms % 1000, line);
		if (!s_file.load())
		{
			// before /data is reachable: on stdout at once, and kept for the file
			WriteAll(STDOUT_FILENO, text.data(), text.size());
			std::lock_guard lock(S().mutex);
			if (!s_file.load())
			{
				if (s_pending.size() < 256 * 1024)
					s_pending += text;
				return;
			}
		}
		else if (!S().writerStarted.load())
		{
			// start-up, one thread: written here, to stdout and the file
			std::lock_guard lock(S().mutex);
			if (!S().writerStarted.load())
			{
				WriteAll(STDOUT_FILENO, text.data(), text.size());
				WriteAll(fileno(s_file.load()), text.data(), text.size());
				return;
			}
		}
		Queue(std::move(text));
	}
}

// A line from Cemu's code into the boot log (patches/cemu), queued like the port's own.
void PS5Cemu_LogLine(std::string_view line)
{
	ps5log::Write(line);
}

// Cemu's crash report (patches/cemu, ExceptionHandler), line by line from its signal handler: with
// write(2) on the file's descriptor, no lock taken (the crashed thread may hold the log's) and no
// buffer, so each line is on disk before the next. Every line is "[crash] ...". The lines queued
// before it are given a moment to be written first.
void PS5Cemu_CrashLine(std::string_view text, bool newLine)
{
	static bool s_lineStarted = false, s_flushed = false;
	FILE* file = s_file.load();
	if (!file)
		return;
	if (!s_flushed)
	{
		s_flushed = true;
		ps5log::Flush(200);
	}
	const int fd = fileno(file);
	if (!s_lineStarted)
		(void)!write(fd, "[crash] ", 8);
	(void)!write(fd, text.data(), text.size());
	s_lineStarted = !newLine;
	if (newLine)
		(void)!write(fd, "\n", 1);
}
