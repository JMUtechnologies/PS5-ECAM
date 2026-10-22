// SPDX-License-Identifier: GPL-3.0-or-later
// The public room list as ProsperoEden reads it (Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later:
// headless/multiplayer_directory.cpp's ParseRoomDirectory and FetchRoomDirectory), with the app's curl
// and RapidJSON.
#include "rooms.h"
#include "../app/lang.h"
#include "../app/paths.h"
#include "../ps5/log.h"

#include <curl/curl.h>
#include <rapidjson/document.h>

#include <arpa/inet.h>

#include <algorithm>
#include <mutex>
#include <thread>

// The PS5's network library, as app/pack_updates.cpp uses it: getaddrinfo finds nothing on the
// console, so the host's address comes from the PS5's resolver and is given to curl
extern "C"
{
	int sceNetInit(void);
	int sceNetPoolCreate(const char* name, int size, int flags);
	int sceNetPoolDestroy(int pool);
	int sceNetResolverCreate(const char* name, int pool, int flags);
	int sceNetResolverStartNtoa(int resolver, const char* host, uint32_t* address, int timeoutUs, int retries, int flags);
	int sceNetResolverDestroy(int resolver);
}

namespace ps5rooms
{
	namespace
	{
		constexpr const char* kHost = "api.ynet-fun.xyz";
		constexpr const char* kDirectory = "https://api.ynet-fun.xyz/lobby";
		constexpr size_t kMaxBytes = 2 * 1024 * 1024;
		// ProsperoEden's own rooms: a name that starts with the project's, on one of its ports
		constexpr const char* kFeaturedName = "BlackBearReloaded ";
		constexpr uint16_t kFeaturedFirst = 24001, kFeaturedLast = 24005;

		std::mutex s_mutex;
		Status s_status; // under s_mutex
		bool s_asked = false;

		size_t Collect(char* data, size_t size, size_t count, void* out)
		{
			auto* body = static_cast<std::string*>(out);
			if (body->size() + size * count > kMaxBytes)
				return 0;
			body->append(data, size * count);
			return size * count;
		}

		// A room must not point the console at its own network or at itself: loopback, private,
		// carrier-grade NAT, link-local, multicast, reserved and broadcast addresses are left out
		bool PublicAddress(const std::string& text)
		{
			in_addr address{};
			if (inet_pton(AF_INET, text.c_str(), &address) != 1)
				return false;
			const uint32_t ip = ntohl(address.s_addr);
			const auto in = [ip](uint32_t network, int bits) { return (ip >> (32 - bits)) == (network >> (32 - bits)); };
			return !(in(0x00000000, 8) || in(0x0A000000, 8) || in(0x64400000, 10) || in(0x7F000000, 8) || in(0xA9FE0000, 16) ||
				in(0xAC100000, 12) || in(0xC0A80000, 16) || ip >= 0xE0000000);
		}

		// Text from the directory, on one row: control characters as spaces
		bool Text(const rapidjson::Value& row, const char* key, size_t limit, std::string& out)
		{
			if (!row.HasMember(key) || !row[key].IsString() || row[key].GetStringLength() > limit)
				return false;
			out.assign(row[key].GetString(), row[key].GetStringLength());
			if (out.find('\0') != std::string::npos)
				return false;
			for (char& c : out)
				if ((unsigned char)c < 32 || c == 127)
					c = ' ';
			return true;
		}

		bool Number(const rapidjson::Value& row, const char* key, uint64_t most, unsigned& out)
		{
			if (!row.HasMember(key) || !row[key].IsUint64() || row[key].GetUint64() > most)
				return false;
			out = (unsigned)row[key].GetUint64();
			return true;
		}

		std::vector<Room> Parse(const std::string& body, std::string& error)
		{
			std::vector<Room> rooms;
			rapidjson::Document document;
			document.Parse(body.c_str(), body.size());
			if (document.HasParseError() || !document.IsObject() || !document.HasMember("rooms") || !document["rooms"].IsArray())
			{
				error = ps5lang::Tr("The public room list could not be read.");
				return rooms;
			}
			const auto& list = document["rooms"].GetArray();
			for (const auto& row : list)
			{
				if (!row.IsObject() || rooms.size() >= 2048)
					continue;
				Room room;
				unsigned port = 0, version = 0;
				if (!Text(row, "name", 512, room.name) || room.name.empty() || !Text(row, "address", 15, room.address) ||
					!PublicAddress(room.address) || !Number(row, "port", 65535, port) || port == 0 ||
					!Number(row, "maxPlayers", 65535, room.capacity) || room.capacity == 0 || !row.HasMember("hasPassword") ||
					!row["hasPassword"].IsBool() || !Number(row, "netVersion", UINT32_MAX, version))
					continue;
				Text(row, "preferredGameName", 512, room.game);
				room.port = (uint16_t)port;
				room.password = row["hasPassword"].GetBool();
				room.compatible = version == kProtocol;
				room.featured = room.name.rfind(kFeaturedName, 0) == 0 && room.port >= kFeaturedFirst && room.port <= kFeaturedLast;
				if (row.HasMember("players") && row["players"].IsArray())
					room.players = (unsigned)std::min<size_t>(row["players"].Size(), 65535);
				rooms.push_back(std::move(room));
			}
			if (!list.Empty() && rooms.empty())
				error = ps5lang::Tr("No usable rooms were returned.");
			// ProsperoEden's own first, in the order of their ports; then where people are
			std::stable_sort(rooms.begin(), rooms.end(), [](const Room& a, const Room& b) {
				if (a.featured != b.featured)
					return a.featured;
				if (a.featured && a.port != b.port)
					return a.port < b.port;
				if (a.compatible != b.compatible)
					return a.compatible;
				if (a.players != b.players)
					return a.players > b.players;
				return a.name < b.name;
			});
			return rooms;
		}

		void Fetch()
		{
			sceNetInit();
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* resolve = nullptr;
			if (const int pool = sceNetPoolCreate("ps5cemu-rooms", 16 * 1024, 0); pool >= 0)
			{
				uint32_t address = 0;
				const int resolver = sceNetResolverCreate("ps5cemu-rooms", pool, 0);
				// 5 s a try, twice: a lookup that never answers ends, and says so
				const int found = resolver >= 0 ? sceNetResolverStartNtoa(resolver, kHost, &address, 5 * 1000 * 1000, 2, 0) : resolver;
				if (resolver >= 0)
					sceNetResolverDestroy(resolver);
				if (found >= 0 && address != 0)
				{
					char text[INET_ADDRSTRLEN] = {};
					inet_ntop(AF_INET, &address, text, sizeof(text));
					resolve = curl_slist_append(resolve, (std::string(kHost) + ":443:" + text).c_str());
					ps5log::Line("[rooms] {} is {}", kHost, text);
				}
				else
					ps5log::Line("[rooms] the PS5's resolver did not find {} ({:#x}): curl looks it up", kHost, (uint32_t)found);
				sceNetPoolDestroy(pool);
			}
			std::string body, error;
			long http = 0;
			CURLcode result = CURLE_FAILED_INIT;
			char why[CURL_ERROR_SIZE] = {};
			if (CURL* curl = curl_easy_init())
			{
				curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, why);
				static const std::string ca = ps5paths::Assets() + "/cacert.pem"; // the console has no store of its own
				curl_slist* headers = curl_slist_append(nullptr, "api-version: 1");
				if (resolve)
					curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
				curl_easy_setopt(curl, CURLOPT_URL, kDirectory);
				curl_easy_setopt(curl, CURLOPT_USERAGENT, "PS5-ECAM/" PS5CEMU_VERSION);
				curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
				curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
				curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
				curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
				result = curl_easy_perform(curl);
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
				curl_easy_cleanup(curl);
				curl_slist_free_all(headers);
			}
			curl_slist_free_all(resolve);
			std::vector<Room> rooms;
			if (result != CURLE_OK)
				error = body.size() >= kMaxBytes ? ps5lang::Tr("The public room list is too large.") :
					ps5lang::Tr("Could not download the public rooms. Is the PS5 online? Cross tries again.");
			else if (http != 200)
				// tr: the room directory's answer was an HTTP error ({0}: its number)
				error = ps5lang::TrF("The public room list answered HTTP {0}. Try again later.", http);
			else
				rooms = Parse(body, error);
			ps5log::Line("[rooms] public rooms: curl {} ({}) http {} bytes {} rooms {}{}", (int)result, why[0] ? why : curl_easy_strerror(result), http,
				body.size(), rooms.size(), error.empty() ? std::string() : ": " + error);
			std::lock_guard lock(s_mutex);
			s_status.busy = false;
			s_status.fetched = true;
			s_status.error = error;
			// a list that failed keeps the one before
			if (error.empty() || !rooms.empty())
				s_status.rooms = std::move(rooms);
		}
	}

	void Refresh()
	{
		std::lock_guard lock(s_mutex);
		s_asked = true;
		if (s_status.busy)
			return;
		s_status.busy = true;
		ps5log::Line("[rooms] downloading the public list ({})", kDirectory);
		std::thread(Fetch).detach(); // a download of its own, which nothing waits for
	}

	bool Asked()
	{
		std::lock_guard lock(s_mutex);
		return s_asked;
	}

	Status Get()
	{
		std::lock_guard lock(s_mutex);
		return s_status;
	}

	bool ValidNickname(const std::string& name)
	{
		if (name.size() < 4 || name.size() > 20)
			return false;
		return std::all_of(name.begin(), name.end(), [](char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '-' || c == '_';
		});
	}
}
