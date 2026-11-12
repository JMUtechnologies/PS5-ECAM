// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the compatibility feed's fetch (compatibility.h). Three seconds after the launcher is up, on
// a thread of its own, once a launch: the site's status.json, asked with the kept copy's ETag (an answer
// of 304 leaves it as it is), 20 seconds at most. A new one is kept and read, and the launcher's badges
// change where it differs; anything else (no network, an error, a feed that does not parse) leaves the
// kept copy, or the bundled one, in use. A game stops it first (StopFetch), as the other launcher work.

#include "compatibility.h"
#include "../ps5/curl_socket.h"
#include "../ps5/log.h"
#include "../ps5/net.h"
#include "paths.h"

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

extern "C" int sceNetInit(void);

namespace ps5compat
{
	namespace
	{
		constexpr const char* kHost = "premohq.github.io";
		constexpr size_t kMostBytes = 4 << 20; // a feed far past what it will be: cut off

		std::thread s_thread;
		std::atomic<bool> s_stop{false};
		std::atomic<bool> s_running{false};
		std::atomic<bool> s_fetched{false}; // once a launch: the launcher back after a game keeps what it has

		struct Answer
		{
			std::string body, etag;
			bool tooLong = false;
		};

		size_t Collect(char* data, size_t size, size_t count, void* user)
		{
			Answer& answer = *static_cast<Answer*>(user);
			if (answer.body.size() + size * count > kMostBytes)
			{
				answer.tooLong = true;
				return 0;
			}
			answer.body.append(data, size * count);
			return size * count;
		}

		// "ETag: W/\"abc\"" -> W/"abc"
		size_t Header(char* data, size_t size, size_t count, void* user)
		{
			const size_t length = size * count;
			std::string line(data, length);
			if (line.size() > 5 && strncasecmp(line.c_str(), "etag:", 5) == 0)
			{
				std::string tag = line.substr(5);
				const size_t first = tag.find_first_not_of(" \t"), last = tag.find_last_not_of(" \t\r\n");
				static_cast<Answer*>(user)->etag = first == std::string::npos ? std::string() : tag.substr(first, last - first + 1);
			}
			return length;
		}

		int Progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return s_stop ? 1 : 0; }

		void Run()
		{
			struct Running
			{
				~Running() { s_running = false; }
			} running;
			// the launcher first: its first seconds are the player's
			for (int i = 0; i < 30 && !s_stop; i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (s_stop)
				return;
			const auto started = std::chrono::steady_clock::now();
			sceNetInit();
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* resolve = ps5net::AddResolve(nullptr, kHost, 443);
			if (!resolve)
			{
				ps5log::Line("[compat] {} not found: the kept list stays", kHost);
				s_fetched = true;
				return;
			}
			CURL* curl = curl_easy_init();
			if (!curl)
			{
				curl_slist_free_all(resolve);
				return;
			}
			ps5curl::Setup(curl);
			static const std::string ca = ps5paths::Assets() + "/cacert.pem"; // the console has no store of its own
			Answer answer;
			const std::string tag = CachedTag();
			curl_slist* headers = ps5curl::Headers(curl_slist_append(nullptr, "Accept: application/json"));
			if (!tag.empty())
				headers = curl_slist_append(headers, ("If-None-Match: " + tag).c_str());
			curl_easy_setopt(curl, CURLOPT_URL, kUrl);
			curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "Playtendoh/" PS5CEMU_VERSION);
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // gzip: GitHub Pages sends it
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer);
			curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Header);
			curl_easy_setopt(curl, CURLOPT_HEADERDATA, &answer);
			const CURLcode result = curl_easy_perform(curl);
			long status = 0;
			if (result == CURLE_OK)
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
			curl_easy_cleanup(curl);
			curl_slist_free_all(headers);
			curl_slist_free_all(resolve);
			const long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
			if (s_stop)
				return; // cut short for a game: tried again when the launcher is back
			s_fetched = true;
			if (result != CURLE_OK)
				ps5log::Line("[compat] not fetched: {}{}", answer.tooLong ? "too long, " : "", curl_easy_strerror(result));
			else if (status == 304)
				ps5log::Line("[compat] the kept list is the newest ({} ms)", ms);
			else if (status != 200)
				ps5log::Line("[compat] the site answered HTTP {}: the kept list stays", status);
			else if (!Keep(answer.body, answer.etag))
				ps5log::Line("[compat] the site's list did not read: the kept one stays");
			else
				ps5log::Line("[compat] a new list, {} KB, in {} ms", answer.body.size() / 1024, ms);
		}
	}

	void StartFetch()
	{
		if (s_fetched || s_running)
			return;
		if (s_thread.joinable())
			s_thread.join();
		s_stop = false;
		s_running = true;
		s_thread = std::thread(Run);
	}

	void StopFetch()
	{
		s_stop = true;
		if (!s_thread.joinable())
			return;
		// a fetch under way is cut short at its next progress call; waited on briefly, as the reports' sender
		for (int waited = 0; s_running && waited < 50; waited++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (s_running)
			s_thread.detach();
		else
			s_thread.join();
	}
}
