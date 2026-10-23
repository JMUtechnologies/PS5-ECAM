// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: what every curl handle needs on the console, as ProsperoEden's console_curl.c found it on
// hardware. The C library refuses curl's fcntl(O_NONBLOCK) on a socket (ps5/fcntl.cpp answers only
// F_SETFD), so its sockets stayed blocking: after an answer, curl then waited for the server to close
// the connection, its own timeouts unread. Settings > Multiplayer's public rooms stayed "Downloading…"
// for good that way, and the community graphic packs' download could fail. Each socket curl opens
// gets the console's own non-blocking option, SO_NBIO, instead.
//
// And every request asks the server to close the connection after its answer (Headers): kept alive,
// it held curl_easy_cleanup until the server dropped it, 30 s for api.github.com and minutes for
// raw.githubusercontent.com, as curl reads what is left before closing a socket (#78, found and fixed
// on a console by dolphin78: an update took 20 to 30 minutes, then 74 s).

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

	// The headers every request sends (CURLOPT_HTTPHEADER): "Connection: close", added to list (a null
	// list makes a new one). Freed with curl_slist_free_all once the handle is cleaned up
	inline curl_slist* Headers(curl_slist* list = nullptr)
	{
		return curl_slist_append(list, "Connection: close");
	}
}
