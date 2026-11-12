// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: hosts' addresses for ES-DE's scraper. ES-DE's curl asks libc's getaddrinfo, and the one a
// program of this port is linked with refuses every name (the SDK platform's ps5_getaddrinfo answers
// EAI_FAIL), so no scraper site was ever reached. These take its place (tools/build-esde.sh keeps the
// platform's from being bound over them) and ask the PS5's own resolver, as the app's own downloads do
// (port/ps5/net.cpp): 2 s a try, three tries, each host found kept for the session. IPv4 only, one
// address a host. tools/tests/test-esde-net.cpp checks them on a PC with a stand-in resolver.

#include "ecam_frontend.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

// The names these are defined under: libc's on the console, others for the test on a PC
#ifndef ECAM_NET_NAME
#define ECAM_NET_NAME(name) name
#endif

extern "C"
{
	int sceNetInit(void);
	int sceNetPoolCreate(const char* name, int size, int flags);
	int sceNetPoolDestroy(int pool);
	int sceNetResolverCreate(const char* name, int pool, int flags);
	int sceNetResolverStartNtoa(int resolver, const char* host, uint32_t* address, int timeoutUs, int retries, int flags);
	int sceNetResolverDestroy(int resolver);
}

namespace
{
	constexpr int kTryUs = 2 * 1000 * 1000, kRetries = 2; // 2 s a try, three tries
	std::mutex s_mutex;
	std::map<std::string, uint32_t> s_found; // under s_mutex

	// A name the resolver did not find, in ES-DE's own log beside the program's (main_ecam.cpp)
	void Note(const char* host, int result)
	{
		if (std::FILE* log = std::fopen((ecam_frontend::Home() + "/es-de-ps5.log").c_str(), "a"))
		{
			std::fprintf(log, "%lld net: %s could not be found: %#x\n", (long long)std::time(nullptr), host, (unsigned)result);
			std::fclose(log);
		}
	}

	// The host's IPv4 address in network order, or 0 when it cannot be found
	uint32_t Resolve(const char* host)
	{
		{
			std::lock_guard lock(s_mutex);
			if (const auto found = s_found.find(host); found != s_found.end())
				return found->second;
		}
		static const int net = sceNetInit(); // once a process: libSceNet started for the resolver
		(void)net;
		const int pool = sceNetPoolCreate("ecam-esde-resolve", 16 * 1024, 0);
		if (pool < 0)
		{
			Note(host, pool);
			return 0;
		}
		uint32_t address = 0;
		const int resolver = sceNetResolverCreate("ecam-esde-resolve", pool, 0);
		const int result = resolver >= 0 ? sceNetResolverStartNtoa(resolver, host, &address, kTryUs, kRetries, 0) : resolver;
		if (resolver >= 0)
			sceNetResolverDestroy(resolver);
		sceNetPoolDestroy(pool);
		if (result < 0 || address == 0)
		{
			Note(host, result);
			return 0;
		}
		std::lock_guard lock(s_mutex);
		s_found[host] = address;
		return address;
	}

	// One answer and its address in one allocation (freeaddrinfo frees each), the name after them
	struct Answer
	{
		addrinfo info;
		sockaddr_in address;
	};
}

extern "C" int ECAM_NET_NAME(getaddrinfo)(const char* node, const char* service, const addrinfo* hints, addrinfo** result)
{
	if (!result)
		return EAI_FAIL;
	*result = nullptr;
	const int family = hints ? hints->ai_family : AF_UNSPEC;
	const int flags = hints ? hints->ai_flags : 0;
	if (family != AF_UNSPEC && family != AF_INET)
		return EAI_FAMILY;
	if (!node && !service)
		return EAI_NONAME;

	// the port: a number (curl gives one), http or https
	unsigned long port = 0;
	if (service && *service)
	{
		char* end = nullptr;
		port = std::strtoul(service, &end, 10);
		if (*end || port > 65535)
		{
			if (flags & AI_NUMERICSERV)
				return EAI_NONAME;
			if (!std::strcmp(service, "http"))
				port = 80;
			else if (!std::strcmp(service, "https"))
				port = 443;
			else
				return EAI_SERVICE;
		}
	}

	// the address: none given is this console's (any, to listen) or loopback; one written as an address is
	// that address; else the resolver's
	in_addr address{};
	if (!node)
		address.s_addr = htonl((flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
	else if (inet_pton(AF_INET, node, &address) != 1)
	{
		if (flags & AI_NUMERICHOST)
			return EAI_NONAME;
		if (!(address.s_addr = Resolve(node)))
			return EAI_NONAME;
	}

	const size_t nameSize = (node && (flags & AI_CANONNAME)) ? std::strlen(node) + 1 : 0;
	auto* answer = static_cast<Answer*>(std::calloc(1, sizeof(Answer) + nameSize));
	if (!answer)
		return EAI_MEMORY;
	answer->address.sin_family = AF_INET;
	answer->address.sin_port = htons((uint16_t)port);
	answer->address.sin_addr = address;
	answer->info.ai_family = AF_INET;
	answer->info.ai_socktype = (hints && hints->ai_socktype) ? hints->ai_socktype : SOCK_STREAM;
	answer->info.ai_protocol = (hints && hints->ai_protocol) ? hints->ai_protocol
	                           : answer->info.ai_socktype == SOCK_DGRAM ? IPPROTO_UDP
	                                                                    : IPPROTO_TCP;
	answer->info.ai_addrlen = sizeof(sockaddr_in);
	answer->info.ai_addr = reinterpret_cast<sockaddr*>(&answer->address);
	if (nameSize)
	{
		answer->info.ai_canonname = reinterpret_cast<char*>(answer + 1);
		std::memcpy(answer->info.ai_canonname, node, nameSize);
	}
	*result = &answer->info;
	return 0;
}

extern "C" void ECAM_NET_NAME(freeaddrinfo)(addrinfo* list)
{
	while (list)
	{
		addrinfo* next = list->ai_next;
		std::free(list); // the Answer it begins
		list = next;
	}
}
