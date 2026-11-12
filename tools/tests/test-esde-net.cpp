// SPDX-License-Identifier: GPL-3.0-or-later
// ES-DE's getaddrinfo on the console (port/esde/ps5/net_ecam.cpp), on a PC: the PS5's resolver stood in
// for by the PC's own, through the names libc gives (RTLD_NEXT). Built twice by test-esde-net.sh: as a
// program that checks its answers, and as a library put before libc in curl, so a real request to the
// scraper's hosts goes through it (ESDE_NET_ONLINE=1).

#include "ecam_frontend.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

// the PS5's resolver, as net_ecam.cpp asks it: the PC's getaddrinfo (libc's, past net_ecam.cpp's own)
static int s_asked = 0; // names the resolver was asked, to see what net_ecam.cpp keeps

extern "C"
{
	int sceNetInit(void) { return 0; }
	int sceNetPoolCreate(const char*, int, int) { return 1; }
	int sceNetPoolDestroy(int) { return 0; }
	int sceNetResolverCreate(const char*, int, int) { return 2; }
	int sceNetResolverDestroy(int) { return 0; }
	int sceNetResolverStartNtoa(int, const char* host, uint32_t* address, int, int, int)
	{
		using Lookup = int (*)(const char*, const char*, const addrinfo*, addrinfo**);
		using Free = void (*)(addrinfo*);
		static const auto lookup = reinterpret_cast<Lookup>(dlsym(RTLD_NEXT, "getaddrinfo"));
		static const auto release = reinterpret_cast<Free>(dlsym(RTLD_NEXT, "freeaddrinfo"));
		s_asked++;
		addrinfo hints{};
		hints.ai_family = AF_INET;
		addrinfo* found = nullptr;
		if (!lookup || lookup(host, nullptr, &hints, &found) != 0 || !found)
			return (int)0x804101e1; // the console's "not found"
		*address = reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr.s_addr;
		release(found);
		return 0;
	}
}

#ifdef ECAM_NET_LIBRARY
// in curl: whether the names went through net_ecam.cpp, said as curl ends
__attribute__((destructor)) static void Said() { std::fprintf(stderr, "net_ecam: the resolver was asked %d times\n", s_asked); }
#else
static int s_failed = 0;

static void Check(bool ok, const char* what)
{
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	s_failed += !ok;
}

static uint16_t PortOf(const addrinfo* info) { return ntohs(reinterpret_cast<const sockaddr_in*>(info->ai_addr)->sin_port); }
static uint32_t AddressOf(const addrinfo* info) { return ntohl(reinterpret_cast<const sockaddr_in*>(info->ai_addr)->sin_addr.s_addr); }

int main(int argc, char** argv)
{
	if (argc < 2)
		return 2;
	ecam_frontend::Home() = argv[1];
	addrinfo hints{};
	hints.ai_socktype = SOCK_STREAM;
	addrinfo* answer = nullptr;

	// an address as written, the port as curl gives it
	Check(getaddrinfo("192.0.2.7", "443", &hints, &answer) == 0 && answer, "an address is itself");
	Check(answer && answer->ai_family == AF_INET && AddressOf(answer) == 0xc0000207 && PortOf(answer) == 443 &&
			  answer->ai_protocol == IPPROTO_TCP && !answer->ai_next,
		"its family, address, port and protocol");
	freeaddrinfo(answer);
	Check(s_asked == 0, "the resolver is not asked for an address");

	// a name, through the resolver, kept for the next time
	answer = nullptr;
	Check(getaddrinfo("localhost", "http", &hints, &answer) == 0 && answer && AddressOf(answer) == 0x7f000001 && PortOf(answer) == 80,
		"a name through the resolver, http as port 80");
	freeaddrinfo(answer);
	answer = nullptr;
	Check(getaddrinfo("localhost", "https", &hints, &answer) == 0 && answer && PortOf(answer) == 443 && s_asked == 1,
		"the name kept: the resolver asked once");
	freeaddrinfo(answer);

	// the canonical name, in the same allocation
	hints.ai_flags = AI_CANONNAME;
	answer = nullptr;
	Check(getaddrinfo("localhost", "80", &hints, &answer) == 0 && answer && answer->ai_canonname && !std::strcmp(answer->ai_canonname, "localhost"),
		"its canonical name");
	freeaddrinfo(answer);
	hints.ai_flags = 0;

	// what it refuses
	answer = nullptr;
	hints.ai_family = AF_INET6;
	Check(getaddrinfo("localhost", "80", &hints, &answer) == EAI_FAMILY && !answer, "IPv6 refused");
	hints.ai_family = AF_UNSPEC;
	Check(getaddrinfo("localhost", "gopher", &hints, &answer) == EAI_SERVICE, "an unknown service refused");
	hints.ai_flags = AI_NUMERICHOST;
	Check(getaddrinfo("localhost", "80", &hints, &answer) == EAI_NONAME, "a name where an address was asked for");
	hints.ai_flags = 0;
	Check(getaddrinfo(nullptr, nullptr, &hints, &answer) == EAI_NONAME, "neither a name nor a port");

	// no node: this console, to listen or to reach itself
	hints.ai_flags = AI_PASSIVE;
	answer = nullptr;
	Check(getaddrinfo(nullptr, "8080", &hints, &answer) == 0 && answer && AddressOf(answer) == INADDR_ANY && PortOf(answer) == 8080, "no node, passive: any");
	freeaddrinfo(answer);
	hints.ai_flags = 0;

	// a name not found: said in ES-DE's log
	const int asked = s_asked;
	answer = nullptr;
	Check(getaddrinfo("no-such-host.invalid", "443", &hints, &answer) == EAI_NONAME && !answer && s_asked == asked + 1, "a name not found");
	std::ifstream log(std::string(argv[1]) + "/es-de-ps5.log");
	const std::string logged((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
	Check(logged.find("net: no-such-host.invalid could not be found") != std::string::npos, "the name not found in ES-DE's log");

	std::printf(s_failed ? "%d failed\n" : "all passed\n", s_failed);
	return s_failed ? 1 : 0;
}
#endif
