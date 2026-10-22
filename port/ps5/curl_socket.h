// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: what every curl handle needs on the console, as ProsperoEden's console_curl.c found it on
// hardware. The C library refuses curl's fcntl(O_NONBLOCK) on a socket (ps5/fcntl.cpp answers only
// F_SETFD), so its sockets stayed blocking: after an answer, curl then waited for the server to close
// the connection, its own timeouts unread. Settings > Multiplayer's public rooms stayed "Downloading…"
// for good that way, and the community graphic packs' download could fail. Each socket curl opens
// gets the console's own non-blocking option, SO_NBIO, instead.

#pragma once

#include <curl/curl.h>
#include <sys/socket.h>

namespace ps5curl
{
	constexpr int kSoNbio = 0x1200; // SOL_SOCKET's non-blocking option on the console

	inline int NonBlocking(void*, curl_socket_t socket, curlsocktype)
	{
		const int on = 1;
		(void)setsockopt(socket, SOL_SOCKET, kSoNbio, &on, sizeof(on)); // failed: the transfer still works, the old way
		return CURL_SOCKOPT_OK;
	}

	// After curl_easy_init: no signals, and each socket non-blocking
	inline void Setup(CURL* curl)
	{
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, NonBlocking);
	}
}
