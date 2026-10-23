// SPDX-License-Identifier: GPL-3.0-or-later
#include "net.h"
#include "log.h"

#include <curl/curl.h>
#include <fmt/format.h>

#include <arpa/inet.h>

#include <map>
#include <mutex>

extern "C"
{
	int sceNetInit(void);
	int sceNetPoolCreate(const char* name, int size, int flags);
	int sceNetPoolDestroy(int pool);
	int sceNetResolverCreate(const char* name, int pool, int flags);
	int sceNetResolverStartNtoa(int resolver, const char* host, uint32_t* address, int timeoutUs, int retries, int flags);
	int sceNetResolverDestroy(int resolver);
}

namespace ps5net
{
	namespace
	{
		constexpr int kTryUs = 2 * 1000 * 1000, kRetries = 2; // 2 s a try, three tries
		std::mutex s_mutex;
		std::map<std::string, uint32_t> s_found; // under s_mutex
	}

	uint32_t Resolve(const std::string& host)
	{
		in_addr given{};
		if (inet_pton(AF_INET, host.c_str(), &given) == 1)
			return given.s_addr;
		{
			std::lock_guard lock(s_mutex);
			if (const auto found = s_found.find(host); found != s_found.end())
				return found->second;
		}
		static const int net = sceNetInit(); // once a process: libSceNet started for the resolver
		(void)net;
		const int pool = sceNetPoolCreate("ps5ecam-resolve", 16 * 1024, 0);
		if (pool < 0)
		{
			ps5log::Line("[net] sceNetPoolCreate failed: {:#x}", (uint32_t)pool);
			return 0;
		}
		uint32_t address = 0;
		const int resolver = sceNetResolverCreate("ps5ecam-resolve", pool, 0);
		const int result = resolver >= 0 ? sceNetResolverStartNtoa(resolver, host.c_str(), &address, kTryUs, kRetries, 0) : resolver;
		if (resolver >= 0)
			sceNetResolverDestroy(resolver);
		sceNetPoolDestroy(pool);
		if (result < 0 || address == 0)
		{
			ps5log::Line("[net] {} could not be found: {:#x}", host, (uint32_t)result);
			return 0;
		}
		std::lock_guard lock(s_mutex);
		s_found[host] = address;
		return address;
	}

	curl_slist* AddResolve(curl_slist* list, const std::string& host, int port)
	{
		const uint32_t address = Resolve(host);
		if (!address)
			return list;
		char text[INET_ADDRSTRLEN] = {};
		inet_ntop(AF_INET, &address, text, sizeof(text));
		return curl_slist_append(list, fmt::format("{}:{}:{}", host, port, text).c_str());
	}
}
