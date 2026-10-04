// SPDX-License-Identifier: GPL-3.0-or-later
#include "updates.h"
#include "paths.h"
#include "../ps5/log.h"
#include "../ps5/notify.h"

#include <curl/curl.h>
#include <rapidjson/document.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <thread>
#include <tuple>

// The PS5's network library, as app/boxart.cpp uses it: getaddrinfo finds nothing on the console,
// so the address comes from the PS5's resolver and is given to curl
extern "C"
{
	int sceNetInit(void);
	int sceNetPoolCreate(const char* name, int size, int flags);
	int sceNetPoolDestroy(int pool);
	int sceNetResolverCreate(const char* name, int pool, int flags);
	int sceNetResolverStartNtoa(int resolver, const char* host, uint32_t* address, int timeoutUs, int retries, int flags);
	int sceNetResolverDestroy(int resolver);
}

namespace ps5update
{
	namespace
	{
		constexpr const char* kHost = "api.github.com";
		constexpr const char* kLatest = "https://api.github.com/repos/premohq/PS5CEMU-HAR/releases/latest";
		constexpr const char* kReleases = "github.com/premohq/PS5CEMU-HAR/releases";

		std::thread s_thread;
		std::atomic<bool> s_stop{false};

		// "2.0.0d", or "v2.0.0d": numbers, then a letter (none is before "a")
		using Version = std::tuple<int, int, int, int>;
		bool Parse(std::string text, Version& out)
		{
			if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
				text.erase(0, 1);
			int major = 0, minor = 0, patch = 0, used = 0;
			if (std::sscanf(text.c_str(), "%d.%d.%d%n", &major, &minor, &patch, &used) != 3)
				return false;
			const std::string rest = text.substr(used);
			const int letter = rest.empty() ? 0 : std::tolower((unsigned char)rest[0]) - 'a' + 1;
			out = {major, minor, patch, letter};
			return true;
		}

		std::string Resolve()
		{
			const int pool = sceNetPoolCreate("ps5cemu-update", 16 * 1024, 0);
			if (pool < 0)
				return {};
			uint32_t address = 0;
			const int resolver = sceNetResolverCreate("ps5cemu-update", pool, 0);
			const int result = resolver >= 0 ? sceNetResolverStartNtoa(resolver, kHost, &address, 0, 0, 0) : resolver;
			if (resolver >= 0)
				sceNetResolverDestroy(resolver);
			sceNetPoolDestroy(pool);
			if (result < 0 || address == 0)
				return {};
			char text[INET_ADDRSTRLEN] = {};
			inet_ntop(AF_INET, &address, text, sizeof(text));
			return fmt::format("{}:443:{}", kHost, text);
		}

		size_t Collect(char* data, size_t size, size_t count, void* out)
		{
			auto* body = static_cast<std::string*>(out);
			if (body->size() > 256 * 1024)
				return 0; // a release's details are a few kilobytes
			body->append(data, size * count);
			return size * count;
		}

		int Progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
		{
			return s_stop ? 1 : 0; // a game is starting: cut short
		}

		void Check()
		{
			sceNetInit();
			curl_global_init(CURL_GLOBAL_DEFAULT);
			const std::string entry = Resolve();
			if (entry.empty())
			{
				ps5log::Line("[update] {} could not be found: no update check", kHost);
				return;
			}
			CURL* curl = curl_easy_init();
			if (!curl)
				return;
			curl_slist* resolve = curl_slist_append(nullptr, entry.c_str());
			curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
			std::string body;
			const std::string ca = ps5paths::Assets() + "/cacert.pem"; // the console has no store of its own
			curl_easy_setopt(curl, CURLOPT_URL, kLatest);
			curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "PS5CEMU-HAR/" PS5CEMU_VERSION);
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
			const CURLcode result = curl_easy_perform(curl);
			long status = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
			curl_easy_cleanup(curl);
			curl_slist_free_all(resolve);
			curl_slist_free_all(headers);
			if (result != CURLE_OK || status != 200)
			{
				ps5log::Line("[update] no answer from GitHub ({}, HTTP {})", s_stop ? "stopped for a game" : curl_easy_strerror(result), status);
				return;
			}
			rapidjson::Document json;
			if (json.Parse(body.c_str()).HasParseError() || !json.IsObject() || !json.HasMember("tag_name") || !json["tag_name"].IsString())
			{
				ps5log::Line("[update] GitHub's answer had no release in it");
				return;
			}
			const std::string latest = json["tag_name"].GetString();
			Version mine, theirs;
			if (!Parse(PS5CEMU_VERSION, mine) || !Parse(latest, theirs))
				return;
			if (theirs <= mine)
			{
				ps5log::Line("[update] {} is the latest release", Readable(PS5CEMU_VERSION));
				return;
			}
			const std::string message = fmt::format("PS5CEMU-HAR {} is out (this is {}). Get it from {}", Readable(latest),
				Readable(PS5CEMU_VERSION), kReleases);
			ps5log::Line("[update] {}", message);
			ps5notify::Send(message);
		}
	}

	void Start()
	{
		if (s_thread.joinable())
			return;
		s_thread = std::thread(Check);
	}

	void Stop()
	{
		s_stop = true;
		if (s_thread.joinable())
			s_thread.join();
	}
}
