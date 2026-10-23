// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: hosts' addresses for everything the app asks of the network (box art, updates, graphic
// packs, RetroAchievements, network shares). The console's libc finds no name (getaddrinfo fails), so the
// PS5's own resolver is asked: 2 s a try, three tries, never left waiting for good, and each host found
// is kept for the session, so the next download does not ask again. (The update's long waits, #73, were
// not the resolver's but curl's on a kept-alive connection: ps5/curl_socket.h, #78.)

#pragma once

#include <cstdint>
#include <string>

struct curl_slist;

namespace ps5net
{
	// The host's IPv4 address in network order, or 0 when it cannot be found; one written as an address
	// is that address
	uint32_t Resolve(const std::string& host);
	// list (curl's CURLOPT_RESOLVE) with "host:port:address" added, or as it was when the host is not
	// found; a null list makes a new one
	curl_slist* AddResolve(curl_slist* list, const std::string& host, int port);
}
