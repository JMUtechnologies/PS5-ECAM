// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the reports' sender (reports.h). A few seconds after the launcher is up, the outbox is
// posted to the intake (ps5reports::kUrl), one report at a time, on a thread of its own; a game stops it
// first (StopSending, beside ps5update::Stop), so nothing is sent while one runs. Answers: 2xx sent, and
// deleted; 400 or 413 turned away for good, and deleted; anything else (no network, 429, 5xx) kept for
// the next time the launcher is up. Only the report's own JSON goes: no cookie, no user name, the app's
// version as the user agent.

#include "reports.h"
#include "paths.h"
#include "../ps5/curl_socket.h"
#include "../ps5/log.h"
#include "../ps5/net.h"

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <string>
#include <thread>

extern "C" int sceNetInit(void);

namespace ps5reports
{
	namespace
	{
		std::thread s_thread;
		std::atomic<bool> s_stop{false};
		std::atomic<bool> s_running{false};

		size_t Discard(char*, size_t size, size_t count, void*) { return size * count; }

		int Progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return s_stop ? 1 : 0; }

		// "https://host/..." -> "host"
		std::string Host(const std::string& url)
		{
			const size_t start = url.find("://");
			if (start == std::string::npos)
				return {};
			const size_t from = start + 3;
			const size_t end = url.find_first_of(":/", from);
			return url.substr(from, end == std::string::npos ? std::string::npos : end - from);
		}

		// The answer's HTTP status, or 0 when there was none
		long Post(curl_slist* resolve, const std::string& body)
		{
			CURL* curl = curl_easy_init();
			if (!curl)
				return 0;
			ps5curl::Setup(curl);
			static const std::string ca = ps5paths::Assets() + "/cacert.pem"; // the console has no store of its own
			curl_easy_setopt(curl, CURLOPT_URL, kUrl);
			if (resolve)
				curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "Playtendoh/" PS5CEMU_VERSION);
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Discard);
			curl_slist* headers = ps5curl::Headers(curl_slist_append(nullptr, "Content-Type: application/json"));
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			const CURLcode result = curl_easy_perform(curl);
			long status = 0;
			if (result == CURLE_OK)
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
			else if (!s_stop)
				ps5log::Line("[reports] not sent: {}", curl_easy_strerror(result));
			curl_easy_cleanup(curl);
			curl_slist_free_all(headers);
			return status;
		}

		void Run()
		{
			struct Running
			{
				~Running() { s_running = false; }
			} running;
			// the launcher first: its first seconds are the player's
			for (int i = 0; i < 50 && !s_stop; i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (s_stop || !Enabled())
				return;
			const std::vector<std::string> pending = Pending(std::time(nullptr));
			if (pending.empty())
				return;
			sceNetInit();
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* resolve = ps5net::AddResolve(nullptr, Host(kUrl), 443);
			if (!resolve)
			{
				ps5log::Line("[reports] the intake could not be found: {} kept for later", pending.size());
				return;
			}
			int sent = 0, dropped = 0;
			for (const std::string& path : pending)
			{
				if (s_stop || !Enabled())
					break;
				const std::string body = Read(path);
				if (body.empty())
					continue;
				const long status = Post(resolve, body);
				if (status >= 200 && status < 300)
				{
					Remove(path);
					sent++;
				}
				else if (status == 400 || status == 413 || status == 415)
				{
					Remove(path); // never taken: not tried again for good
					dropped++;
				}
				else
				{
					if (status)
						ps5log::Line("[reports] the intake answered HTTP {}: kept for later", status);
					break;
				}
			}
			curl_slist_free_all(resolve);
			if (sent || dropped)
				ps5log::Line("[reports] {} sent, {} turned away", sent, dropped);
		}
	}

	void StartSending()
	{
		if (!Available() || !Enabled() || s_running)
			return;
		if (s_thread.joinable())
			s_thread.join();
		s_stop = false;
		s_running = true;
		s_thread = std::thread(Run);
	}

	void StopSending()
	{
		s_stop = true;
		if (!s_thread.joinable())
			return;
		// a post under way is cut short at its next progress call; waited on briefly, as the update check
		for (int waited = 0; s_running && waited < 50; waited++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (s_running)
			s_thread.detach();
		else
			s_thread.join();
	}
}
