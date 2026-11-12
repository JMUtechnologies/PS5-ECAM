// SPDX-License-Identifier: GPL-3.0-or-later
#include "boxart.h"
#include "banner_compose.h"
#include "paths.h"
#include "../ps5/log.h"
#include "../ps5/curl_socket.h"
#include "../ps5/net.h"

#include <curl/curl.h>
#include <rapidjson/document.h>
#include <png.h>
#include <turbojpeg.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <thread>

namespace fs = std::filesystem;


namespace ps5boxart
{
	namespace
	{
		constexpr size_t kMaxDownload = 8 * 1024 * 1024;
		constexpr auto kNoneAgain = std::chrono::hours(24 * 14); // a cover GameTDB had none of, asked for again after
		constexpr int kTriesEach = 4;							   // a cover that keeps failing waits for the next start
		constexpr const char* kHost = "art.gametdb.com";
		constexpr int kMaxDecoded = 8192; // a side, before scaling

		std::mutex s_mutex;
		std::condition_variable s_wake;
		std::deque<std::pair<System, std::string>> s_queue; // under s_mutex
		std::set<std::string> s_queued;						 // folder/ID, under s_mutex
		bool s_started = false;
		std::atomic<uint32_t> s_arrivals{0};
		std::atomic<uint32_t> s_mediaArrivals{0};
		constexpr std::string_view kMedia = "media/"; // before an ID on the queue: its disc or card, not its cover
		std::atomic<bool> s_enabled{true};
		std::atomic<bool> s_spines{false}; // 3D box art: each GameTDB cover's spine too
		std::atomic<bool> s_stopped{false}; // for good: a game is starting
		std::atomic<bool> s_busy{false};	// a cover is being fetched
		std::atomic<int> s_answered{-1};	// Answered()
		bool s_retryNow = false;			// under s_mutex: Refetch asked while the worker waits to try again
		std::vector<std::string> s_arrived;  // folder/ID, under s_mutex: TakeArrived's

		const char* Folder(System system)
		{
			return system == System::GameCubeWii ? "wii" : system == System::WiiU ? "wiiu" : system == System::Nds ? "ds" : system == System::Switch ? "switch" : "3ds";
		}

		std::string Base(System system, const std::string& id)
		{
			return fmt::format("{}/boxart/{}/{}", ps5paths::Covers(), Folder(system), id);
		}

		// The ID on the queue as GameTDB names it, and whether it asks for the disc or card
		std::string_view Bare(const std::string& id, bool* media = nullptr)
		{
			const bool isMedia = id.rfind(kMedia, 0) == 0;
			if (media)
				*media = isMedia;
			return std::string_view(id).substr(isMedia ? kMedia.size() : 0);
		}

		bool Here(System system, const std::string& id)
		{
			std::error_code ec;
			return fs::exists(Base(system, id) + ".tga", ec);
		}

		bool Valid(const std::string& id)
		{
			if (id.size() < 4 || id.size() > 6)
				return false;
			return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); });
		}

		// GameTDB's region folders to try, the game's own first: the ID's fourth letter is its region. A
		// Switch game's five letters name none: America's first, as its English title found it
		std::vector<std::string> Regions(const std::string& id)
		{
			std::string own;
			switch (id.size() == 5 ? '\0' : id[3])
			{
			case '\0': own = "US"; break;
			case 'E': own = "US"; break;
			case 'J': own = "JA"; break;
			case 'K': own = "KO"; break;
			case 'D': own = "DE"; break;
			case 'F': own = "FR"; break;
			case 'S': own = "ES"; break;
			case 'I': own = "IT"; break;
			case 'H': own = "NL"; break;
			case 'U': own = "AU"; break;
			case 'R': own = "RU"; break;
			case 'C':
			case 'W':
			case 'Z': own = "ZH"; break;
			default: own = "EN"; break;
			}
			std::vector<std::string> regions{own};
			for (const char* other : {"US", "EN", "JA", "OTHER"})
				if (own != other)
					regions.emplace_back(other);
			return regions;
		}

		size_t Receive(char* data, size_t size, size_t count, void* user)
		{
			auto* out = static_cast<std::vector<uint8_t>*>(user);
			const size_t bytes = size * count;
			if (out->size() + bytes > kMaxDownload)
				return 0; // too big for a cover: stop
			out->insert(out->end(), data, data + bytes);
			return bytes;
		}

		// The HTTP status, or -1 when there was no answer (no network)
		long Get(CURL* curl, const std::string& url, std::vector<uint8_t>& out)
		{
			out.clear();
			char error[CURL_ERROR_SIZE] = {};
			static const std::string ca = ps5paths::Assets() + "/cacert.pem"; // for a redirect to HTTPS: the console has no store
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &Receive);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
			// GameTDB moving its pictures (to HTTPS, or another host) is followed, as a browser would
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "PS5-ECAM/" PS5CEMU_VERSION);
			const CURLcode result = curl_easy_perform(curl);
			if (result != CURLE_OK)
			{
				ps5log::Line("[boxart] {}: curl error {} ({})", url, (int)result, error[0] ? error : curl_easy_strerror(result));
				return -1;
			}
			long status = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
			return status;
		}

		bool DecodeJpeg(const std::vector<uint8_t>& data, std::vector<uint8_t>& rgba, int& width, int& height)
		{
			tjhandle decoder = tjInitDecompress();
			if (!decoder)
				return false;
			int subsampling = 0, colourspace = 0;
			bool ok = tjDecompressHeader3(decoder, data.data(), (unsigned long)data.size(), &width, &height, &subsampling, &colourspace) == 0 &&
				width > 0 && height > 0 && width <= kMaxDecoded && height <= kMaxDecoded;
			if (ok)
			{
				rgba.resize((size_t)width * height * 4);
				ok = tjDecompress2(decoder, data.data(), (unsigned long)data.size(), rgba.data(), width, 0, height, TJPF_RGBA, TJFLAG_ACCURATEDCT) == 0;
			}
			tjDestroy(decoder);
			return ok;
		}

		bool DecodePng(const std::vector<uint8_t>& data, std::vector<uint8_t>& rgba, int& width, int& height)
		{
			png_image image{};
			image.version = PNG_IMAGE_VERSION;
			if (!png_image_begin_read_from_memory(&image, data.data(), data.size()))
				return false;
			if (image.width == 0 || image.height == 0 || image.width > kMaxDecoded || image.height > kMaxDecoded)
			{
				png_image_free(&image);
				return false;
			}
			image.format = PNG_FORMAT_RGBA;
			rgba.resize(PNG_IMAGE_SIZE(image));
			if (!png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr))
			{
				png_image_free(&image);
				return false;
			}
			width = (int)image.width;
			height = (int)image.height;
			return true;
		}

		// Scaled down to fit kMaxWidth x kMaxHeight, each pixel the average of those it covers
		std::vector<uint8_t> Fit(const std::vector<uint8_t>& rgba, int width, int height, int& outWidth, int& outHeight)
		{
			const double scale = std::min({1.0, (double)kMaxWidth / width, (double)kMaxHeight / height});
			outWidth = std::max(1, (int)(width * scale + 0.5));
			outHeight = std::max(1, (int)(height * scale + 0.5));
			if (outWidth == width && outHeight == height)
				return rgba;
			std::vector<uint8_t> out((size_t)outWidth * outHeight * 4);
			for (int y = 0; y < outHeight; y++)
			{
				const int y0 = (int)((int64_t)y * height / outHeight), y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * height / outHeight));
				for (int x = 0; x < outWidth; x++)
				{
					const int x0 = (int)((int64_t)x * width / outWidth), x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * width / outWidth));
					uint32_t sum[4] = {};
					for (int sy = y0; sy < y1; sy++)
						for (int sx = x0; sx < x1; sx++)
							for (int c = 0; c < 4; c++)
								sum[c] += rgba[((size_t)sy * width + sx) * 4 + c];
					const uint32_t count = (uint32_t)((y1 - y0) * (x1 - x0));
					for (int c = 0; c < 4; c++)
						out[((size_t)y * outWidth + x) * 4 + c] = (uint8_t)((sum[c] + count / 2) / count);
				}
			}
			return out;
		}

		// A 32-bit top-down TGA, as the launcher reads them (ui/images.cpp), written whole
		// or not at all
		bool WriteTga(const std::string& path, const std::vector<uint8_t>& rgba, int width, int height)
		{
			std::vector<uint8_t> tga(18 + (size_t)width * height * 4);
			tga[2] = 2;
			tga[12] = width & 255, tga[13] = width >> 8, tga[14] = height & 255, tga[15] = height >> 8;
			tga[16] = 32, tga[17] = 0x28;
			for (size_t i = 0; i < (size_t)width * height; i++)
			{
				tga[18 + i * 4 + 0] = rgba[i * 4 + 2];
				tga[18 + i * 4 + 1] = rgba[i * 4 + 1];
				tga[18 + i * 4 + 2] = rgba[i * 4 + 0];
				tga[18 + i * 4 + 3] = rgba[i * 4 + 3];
			}
			const std::string temporary = path + ".tmp";
			{
				std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
				out.write((const char*)tga.data(), (std::streamsize)tga.size());
				if (!out)
					return false;
			}
			std::error_code ec;
			fs::rename(temporary, path, ec);
			return !ec;
		}

		enum class Outcome
		{
			Fetched, // here now
			None,	 // GameTDB answered "not found" in every region: remembered (<ID>.none)
			Later,	 // no network, or GameTDB refused or was busy: tried again later, nothing remembered
		};

		// One game's cover: GameTDB's large one in each region, then its small one
		Outcome FetchOne(CURL* curl, System system, const std::string& queued)
		{
			const std::string base = Base(system, queued);
			bool media = false;
			const std::string id(Bare(queued, &media));
			std::error_code ec;
			fs::create_directories(fs::path(base).parent_path(), ec);
			std::vector<uint8_t> data;
			// (GameTDB's Wii and GameCube covers are PNGs, at one size; its discs and cards are PNGs, clear
			// around their shapes: the discs as "disc", the DS's, 3DS's and Switch's cards as "cart")
			const bool wii = system == System::GameCubeWii;
			const bool disc = wii || system == System::WiiU;
			const std::vector<const char*> sizes = media ? std::vector<const char*>{disc ? "disc" : "cart"} : std::vector<const char*>{"coverHQ", "cover"};
			for (const char* size : sizes)
				for (const std::string& region : Regions(id))
				{
					if (wii && std::string_view(size) == "coverHQ")
						continue;
					const std::string url = fmt::format("http://art.gametdb.com/{}/{}/{}/{}.{}", Folder(system), size, region, id, wii || media ? "png" : "jpg");
					const long status = Get(curl, url, data);
					if (status < 0)
						return Outcome::Later;
					if (status == 404 || status == 410)
						continue; // not in this region: the next
					if (status != 200 || data.size() < 8)
					{
						// anything but "found" or "not found" (refused, too many requests, a server error):
						// never taken as "no cover"
						ps5log::Line("[boxart] {}: HTTP {}; tried again later", url, status);
						return Outcome::Later;
					}
					std::vector<uint8_t> rgba;
					int width = 0, height = 0;
					const bool png = std::memcmp(data.data(), "\x89PNG", 4) == 0;
					if (!(png ? DecodePng(data, rgba, width, height) : DecodeJpeg(data, rgba, width, height)))
					{
						ps5log::Line("[boxart] {} is not an image GameTDB's cover should be", url);
						continue;
					}
					int fitWidth, fitHeight;
					const std::vector<uint8_t> fitted = Fit(rgba, width, height, fitWidth, fitHeight);
					if (media && WriteTga(base + ".tga", fitted, fitWidth, fitHeight))
					{
						fs::remove(base + ".none", ec);
						s_mediaArrivals++;
						ps5log::Line("[boxart] {} {}'s {}: {}x{}", Folder(system), id, size, fitWidth, fitHeight);
					}
					else if (!media && WriteTga(base + ".tga", fitted, fitWidth, fitHeight))
					{
						fs::remove(base + ".none", ec);
						{
							std::lock_guard lock(s_mutex);
							s_arrived.push_back(fmt::format("{}/{}", Folder(system), id));
						}
						s_arrivals++;
						ps5log::Line("[boxart] {} {}: {}x{} from {}", Folder(system), id, fitWidth, fitHeight, url);
					}
					return Outcome::Fetched;
				}
			// GameTDB has none: asked for again in two weeks, or when Settings asks
			std::ofstream(base + ".none").put('\n');
			ps5log::Line("[boxart] {} {}: GameTDB has no {}", Folder(system), id, media ? (disc ? "disc" : "card") : "cover");
			return Outcome::None;
		}

		// A marker younger than the wait for a cover asked for again
		bool Recent(const std::string& marker)
		{
			std::error_code ec;
			const auto when = fs::last_write_time(marker, ec);
			return !ec && fs::file_time_type::clock::now() - when < kNoneAgain;
		}

		// One game's spine, for 3D box art: the strip between the back and the front of GameTDB's whole cover
		// (back, spine, front side by side, the back as wide as the front), cut out at its own resolution as
		// <ID>.spine.tga. The front's width there is the cover's shape at the whole cover's height
		Outcome FetchSpine(CURL* curl, System system, const std::string& id)
		{
			const std::string base = Base(system, id);
			int coverWidth = 0, coverHeight = 0;
			if (!ImageSize(base + ".tga", coverWidth, coverHeight) || coverWidth <= 0 || coverHeight <= 0)
				return Outcome::None;
			const bool wii = system == System::GameCubeWii;
			std::vector<uint8_t> data;
			for (const char* size : {"coverfullHQ", "coverfull"})
				for (const std::string& region : Regions(id))
				{
					const std::string url = fmt::format("http://art.gametdb.com/{}/{}/{}/{}.{}", Folder(system), size, region, id, wii ? "png" : "jpg");
					const long status = Get(curl, url, data);
					if (status < 0)
						return Outcome::Later;
					if (status == 404 || status == 410)
						continue;
					if (status != 200 || data.size() < 8)
					{
						ps5log::Line("[boxart] {}: HTTP {}; tried again later", url, status);
						return Outcome::Later;
					}
					std::vector<uint8_t> rgba;
					int width = 0, height = 0;
					const bool png = std::memcmp(data.data(), "\x89PNG", 4) == 0;
					if (!(png ? DecodePng(data, rgba, width, height) : DecodeJpeg(data, rgba, width, height)))
						continue;
					const int front = (int)std::lround((double)height * coverWidth / coverHeight);
					const int spine = width - 2 * front;
					// a whole cover of another shape (a sleeve, a cover with its flaps): no spine from it
					if (spine < width / 60 || spine > width / 5)
					{
						ps5log::Line("[boxart] {} {}: GameTDB's whole cover is {}x{}, no spine between a {} wide back and front", Folder(system), id,
							width, height, front);
						std::ofstream(base + ".spine.none").put('\n');
						return Outcome::None;
					}
					std::vector<uint8_t> strip((size_t)spine * height * 4);
					for (int y = 0; y < height; y++)
						std::memcpy(&strip[(size_t)y * spine * 4], &rgba[((size_t)y * width + front) * 4], (size_t)spine * 4);
					int fitWidth, fitHeight;
					const std::vector<uint8_t> fitted = Fit(strip, spine, height, fitWidth, fitHeight);
					if (!WriteTga(base + ".spine.tga", fitted, fitWidth, fitHeight))
						return Outcome::None;
					{
						std::lock_guard lock(s_mutex);
						s_arrived.push_back(fmt::format("{}/{}", Folder(system), id));
					}
					s_arrivals++;
					ps5log::Line("[boxart] {} {}: spine {}x{} from {}", Folder(system), id, fitWidth, fitHeight, url);
					return Outcome::Fetched;
				}
			std::ofstream(base + ".spine.none").put('\n');
			return Outcome::None;
		}

		// A cover GameTDB had none of, lately: not asked for again yet
		bool RecentlyNone(System system, const std::string& id)
		{
			std::error_code ec;
			const fs::path none = Base(system, id) + ".none";
			const auto when = fs::last_write_time(none, ec);
			return !ec && fs::file_time_type::clock::now() - when < kNoneAgain;
		}

		// The network, before the first download: GameTDB's address from the PS5's resolver (ps5/net.h;
		// libc's getaddrinfo finds nothing on the console), given to curl for HTTP and HTTPS, should GameTDB
		// send its pictures there. Null when there is no way to GameTDB
		curl_slist* PrepareNetwork()
		{
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* resolved = ps5net::AddResolve(ps5net::AddResolve(nullptr, kHost, 80), kHost, 443);
			if (resolved)
				ps5log::Line("[boxart] {}", resolved->data);
			return resolved;
		}

		void Worker()
		{
			curl_slist* resolved = PrepareNetwork();
			CURL* curl = curl_easy_init();
			// each cover's connection closed by GameTDB after its answer (ps5/curl_socket.h, #78): one closed
			// by curl instead held the worker until the server dropped it. The list lives with the worker
			static curl_slist* const headers = ps5curl::Headers();
			if (curl)
			{
				ps5curl::Setup(curl);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			}
			if (curl && resolved)
				curl_easy_setopt(curl, CURLOPT_RESOLVE, resolved);
			std::map<std::string, int> tries; // folder/ID: how many times it came back Later
			auto seconds = std::chrono::seconds(15);
			for (;;)
			{
				std::pair<System, std::string> item;
				{
					std::unique_lock lock(s_mutex);
					s_wake.wait(lock, [] { return !s_queue.empty(); });
					item = std::move(s_queue.front());
					s_queue.pop_front();
				}
				const std::string key = fmt::format("{}/{}", Folder(item.first), item.second);
				auto forget = [&] {
					// let go of: a later Fetch (box art turned on again, the next scan) asks for it again
					std::lock_guard lock(s_mutex);
					s_queued.erase(key);
				};
				if (!s_enabled || s_stopped || !curl)
				{
					forget();
					continue;
				}
				// a cover here: its spine still to come, for 3D box art (a disc or card has none: there or not)
				const bool media = item.second.rfind(kMedia, 0) == 0;
				const bool have = media ? Here(item.first, item.second) : !Path(item.first, item.second).empty();
				const std::string base = Base(item.first, item.second);
				std::error_code here;
				const bool spine = !media && s_spines && !fs::exists(base + ".spine.tga", here) && !Recent(base + ".spine.none");
				if (have ? !spine : RecentlyNone(item.first, item.second))
					continue;
				s_busy = true;
				Outcome outcome = have ? FetchSpine(curl, item.first, item.second) : FetchOne(curl, item.first, item.second);
				if (!have && !media && outcome == Outcome::Fetched && s_spines)
					outcome = FetchSpine(curl, item.first, item.second) == Outcome::Later ? Outcome::Later : Outcome::Fetched;
				s_busy = false;
				if (outcome != Outcome::Later)
				{
					s_answered = 1;
					seconds = std::chrono::seconds(15);
					continue;
				}
				// no network, or GameTDB busy: this one goes back first, and everything waits a while
				// (15 s, doubling to 4 min), or until Settings asks again; a game starting ends the wait
				if (s_answered != 1)
					s_answered = 0;
				std::unique_lock lock(s_mutex);
				if (++tries[key] < kTriesEach)
					s_queue.emplace_front(item);
				else
				{
					s_queued.erase(key);
					ps5log::Line("[boxart] {}: tried {} times, left for the next start", key, kTriesEach);
				}
				ps5log::Line("[boxart] GameTDB did not answer: trying again in {} s ({} covers waiting)", seconds.count(), s_queue.size());
				s_wake.wait_for(lock, seconds, [] { return s_retryNow || s_stopped.load(); });
				s_retryNow = false;
				seconds = std::min(seconds * 2, std::chrono::seconds(240));
			}
		}
	}

	std::string MediaPath(System system, const std::string& id)
	{
		if (!Valid(id))
			return {};
		const std::string path = Base(system, std::string(kMedia) + id) + ".tga";
		std::error_code ec;
		return fs::exists(path, ec) ? path : std::string();
	}

	uint32_t MediaArrivals()
	{
		return s_mediaArrivals;
	}

	std::string Path(System system, const std::string& id)
	{
		if (!Valid(id))
			return {};
		const std::string path = Base(system, id) + ".tga";
		std::error_code ec;
		return fs::exists(path, ec) ? path : std::string();
	}

	namespace
	{
		constexpr const char* kTitlesHost = "thumbnails.libretro.com";
		std::mutex s_titleMutex;
		std::deque<std::pair<bool, std::string>> s_titleQueue; // under s_titleMutex
		std::set<std::string> s_titleSeen;					   // under s_titleMutex: asked for in this run
		bool s_titleRunning = false;						   // under s_titleMutex

		// A "none here" marker younger than the wait (no marker: not recent)
		bool RecentMarker(const std::string& path)
		{
			std::error_code ec;
			const auto when = fs::last_write_time(path, ec);
			return !ec && fs::file_time_type::clock::now() - when < kNoneAgain;
		}

		// As libretro's thumbnails name theirs
		std::string TitleFileName(std::string name)
		{
			for (char& c : name)
				if (std::strchr("&*/:`<>?\\|\"", c))
					c = '_';
			return name;
		}
		std::string TitleBase(const std::string& name) { return fmt::format("{}/boxart/titles/{}", ps5paths::Covers(), TitleFileName(name)); }

#ifndef PS5ECAM_STEAMGRIDDB_KEY
#define PS5ECAM_STEAMGRIDDB_KEY ""
#endif
		std::string SteamGridKeyFile() { return ps5paths::Covers() + "/boxart/steamgriddb-key.txt"; }
		std::string SteamGridKey()
		{
			std::string key;
			std::ifstream(SteamGridKeyFile()) >> key;
			return key.empty() ? std::string(PS5ECAM_STEAMGRIDDB_KEY) : key;
		}

		// A game file's name as a search: its tags gone, "Legend of Zelda, The - X" as "The Legend of Zelda - X"
		std::string SteamGridSearch(std::string name)
		{
			if (const size_t tag = name.find(" ("); tag != std::string::npos)
				name.resize(tag);
			if (const size_t the = name.find(", The"); the != std::string::npos)
				name = "The " + name.substr(0, the) + name.substr(the + 5);
			return name;
		}

		// Wide art for the game: true when it's here. later: no answer, or a refusal, so nothing is remembered
		bool FetchSteamGrid(CURL* curl, curl_slist* plain, const std::string& key, const std::string& name, const std::string& base, bool& later)
		{
			curl_slist* authorised = ps5curl::Headers(curl_slist_append(nullptr, ("Authorization: Bearer " + key).c_str()));
			const auto api = [&](const std::string& url, rapidjson::Document& json) {
				std::vector<uint8_t> data;
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, authorised);
				const long status = Get(curl, url, data);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plain);
				if (status != 200)
				{
					if (status != 404)
					{
						later = true;
						ps5log::Line("[boxart] SteamGridDB: HTTP {}", status);
					}
					return false;
				}
				json.Parse((const char*)data.data(), data.size());
				return !json.HasParseError() && json.IsObject() && json.HasMember("data") && json["data"].IsArray() && !json["data"].Empty();
			};
			bool found = false;
			char* const term = curl_easy_escape(curl, SteamGridSearch(name).c_str(), 0);
			rapidjson::Document games, heroes;
			if (term && api(fmt::format("https://www.steamgriddb.com/api/v2/search/autocomplete/{}", term), games) && games["data"][0].IsObject() &&
				games["data"][0].HasMember("id") && games["data"][0]["id"].IsInt())
			{
				const int id = games["data"][0]["id"].GetInt();
				if (api(fmt::format("https://www.steamgriddb.com/api/v2/heroes/game/{}?types=static&nsfw=false&humor=false", id), heroes))
					for (const auto& hero : heroes["data"].GetArray())
					{
						if (!hero.IsObject() || !hero.HasMember("url") || !hero["url"].IsString())
							continue;
						std::vector<uint8_t> data, rgba;
						int width = 0, height = 0;
						if (Get(curl, hero["url"].GetString(), data) != 200 || data.size() < 8)
							continue;
						const bool png = std::memcmp(data.data(), "\x89PNG", 4) == 0;
						if (!(png ? DecodePng(data, rgba, width, height) : DecodeJpeg(data, rgba, width, height)) || width <= 0 || height <= 0)
							continue;
						found = WriteTga(base + ".hero.tga", ComposeBanner(rgba, width, height), kBannerWidth, kBannerHeight);
						if (found)
							ps5log::Line("[boxart] SteamGridDB art {}: {}x{}", name, width, height);
						break;
					}
			}
			if (term)
				curl_free(term);
			curl_slist_free_all(authorised);
			return found;
		}

		void TitleWorker()
		{
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* const resolved = ps5net::AddResolve(ps5net::AddResolve(ps5net::AddResolve(ps5net::AddResolve(nullptr, kTitlesHost, 80), kTitlesHost, 443),
				"www.steamgriddb.com", 443), "cdn2.steamgriddb.com", 443);
			static curl_slist* const headers = ps5curl::Headers();
			CURL* const curl = curl_easy_init();
			if (curl)
			{
				ps5curl::Setup(curl);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
				if (resolved)
					curl_easy_setopt(curl, CURLOPT_RESOLVE, resolved);
			}
			for (;;)
			{
				std::pair<bool, std::string> item;
				{
					std::lock_guard lock(s_titleMutex);
					if (s_titleQueue.empty() || s_stopped || !curl)
					{
						s_titleRunning = false;
						break;
					}
					item = std::move(s_titleQueue.front());
					s_titleQueue.pop_front();
				}
				const std::string base = TitleBase(item.second);
				{
					std::error_code dirs;
					fs::create_directories(fs::path(base).parent_path(), dirs);
					const std::string key = SteamGridKey();
					bool later = false;
					if (!key.empty() && !fs::exists(base + ".hero.tga", dirs) && !RecentMarker(base + ".hero.none"))
					{
						if (FetchSteamGrid(curl, headers, key, item.second, base, later))
							continue;
						if (!later)
							std::ofstream(base + ".hero.none").put('\n');
					}
					if (fs::exists(base + ".hero.tga", dirs) || fs::exists(base + ".tga", dirs))
						continue;
				}
				char* const escaped = curl_easy_escape(curl, TitleFileName(item.second).c_str(), 0);
				if (!escaped)
					continue;
				const std::string url = fmt::format("http://{}/Nintendo%20-%20{}/Named_Titles/{}.png", kTitlesHost, item.first ? "Wii" : "GameCube", escaped);
				curl_free(escaped);
				std::error_code ec;
				fs::create_directories(fs::path(base).parent_path(), ec);
				std::vector<uint8_t> data, rgba;
				int width = 0, height = 0;
				const long status = Get(curl, url, data);
				if (status == 404 || status == 410)
					std::ofstream(base + ".none").put('\n'); // asked for again after a while, as a cover is
				else if (status == 200 && DecodePng(data, rgba, width, height) && width > 0 && height > 0)
				{
					if (WriteTga(base + ".tga", ComposeBanner(rgba, width, height), kBannerWidth, kBannerHeight))
						ps5log::Line("[boxart] title art {}: {}x{}", item.second, width, height);
				}
				else
					ps5log::Line("[boxart] title art {}: HTTP {}; next start", item.second, status);
			}
			if (curl)
				curl_easy_cleanup(curl);
		}
	}

	bool HasSteamGridKey() { return !SteamGridKey().empty(); }
	std::string PlayerSteamGridKeyText()
	{
		std::string key;
		std::ifstream(SteamGridKeyFile()) >> key;
		return key;
	}
	bool PlayerSteamGridKey()
	{
		std::error_code ec;
		return fs::exists(SteamGridKeyFile(), ec);
	}
	void SetSteamGridKey(const std::string& key)
	{
		std::error_code ec;
		fs::create_directories(fs::path(SteamGridKeyFile()).parent_path(), ec);
		if (key.empty())
			fs::remove(SteamGridKeyFile(), ec);
		else
			std::ofstream(SteamGridKeyFile(), std::ios::trunc) << key << '\n';
		std::lock_guard lock(s_titleMutex);
		s_titleSeen.clear(); // asked for again, with it
	}

	std::string TitleArtPath(const std::string& name)
	{
		std::error_code ec;
		const std::string hero = TitleBase(name) + ".hero.tga", path = TitleBase(name) + ".tga";
		return fs::exists(hero, ec) ? hero : fs::exists(path, ec) ? path : std::string();
	}

	void FetchTitleArt(const std::vector<std::pair<bool, std::string>>& wiiAndNames)
	{
		if (!s_enabled || s_stopped)
			return;
		std::lock_guard lock(s_titleMutex);
		for (const auto& item : wiiAndNames)
		{
			std::error_code ec;
			const std::string base = TitleBase(item.second);
			if (!s_titleSeen.insert(item.second).second || fs::exists(base + ".hero.tga", ec))
				continue;
			const bool noTitle = fs::exists(base + ".tga", ec) || RecentMarker(base + ".none");
			if (noTitle && SteamGridKey().empty())
				continue;
			s_titleQueue.push_back(item);
		}
		if (!s_titleRunning && !s_titleQueue.empty())
		{
			s_titleRunning = true;
			std::thread(TitleWorker).detach();
		}
	}

	// -- the libretro sides' covers: libretro's thumbnails, by the game file's name ---------------------------

	namespace
	{
		std::mutex s_retroMutex;
		std::deque<std::pair<std::string, std::string>> s_retroQueue; // under s_retroMutex: the folder, the name
		std::set<std::string> s_retroSeen;							   // under s_retroMutex
		bool s_retroRunning = false;								   // under s_retroMutex
		std::atomic<uint32_t> s_retroArrivals{0};

		std::string RetroBase(const std::string& system, const std::string& name)
		{
			return fmt::format("{}/boxart/retro/{}/{}", ps5paths::Covers(), TitleFileName(system), TitleFileName(name));
		}

		// A cover libretro had none of, by the file's name or the best of its names ("matched", written since 6.0
		// matches names): a marker from before, written on the file's name alone, is asked for again
		constexpr const char* kRetroMissed = "matched";
		bool RetroMissedRecently(const std::string& base)
		{
			if (!RecentMarker(base + ".none"))
				return false;
			std::string line;
			std::getline(std::ifstream(base + ".none"), line);
			return line == kRetroMissed;
		}

		// A title as names are matched: lower case, what is in brackets left out (regions, revisions), letters
		// and digits only, in words, without the words names add or leave out ("the", "version")
		std::vector<std::string> Words(std::string name)
		{
			for (const auto& [from, to] : {std::pair<const char*, const char*>{"\xc3\xa9", "e"}, {"\xc3\x89", "e"}, {"&", " and "}})
				for (size_t at = 0; (at = name.find(from, at)) != std::string::npos; at += std::strlen(to))
					name.replace(at, std::strlen(from), to);
			static const std::set<std::string> kLeftOut = {"the", "version", "edition", "a", "and", "of"};
			std::vector<std::string> words;
			std::string word;
			int depth = 0;
			const auto flush = [&] {
				if (!word.empty() && !kLeftOut.count(word))
					words.push_back(word);
				word.clear();
			};
			for (const char c : name)
			{
				if (c == '(' || c == '[')
					flush(), depth++;
				else if ((c == ')' || c == ']') && depth)
					depth--;
				else if (!depth && std::isalnum((unsigned char)c))
					word += (char)std::tolower((unsigned char)c);
				else
					flush();
			}
			flush();
			return words;
		}

		std::string Joined(const std::vector<std::string>& words, size_t count = std::string::npos)
		{
			std::string out;
			for (size_t i = 0; i < words.size() && i < count; i++)
				out += words[i];
			return out;
		}

		// How well one of libretro's names fits a game file's: 3 the same title, 2 the file's title and then a
		// subtitle ("Super Mario Land 2" for "Super Mario Land 2 - 6 Golden Coins"), 1 so without the file's last
		// word (a hack's "DX"); 0 none. Words are compared run together ("FireRed" is "Fire Red")
		int NameFit(const std::vector<std::string>& file, const std::vector<std::string>& theirs)
		{
			const std::string mine = Joined(file), their = Joined(theirs);
			if (mine.size() < 4)
				return mine == their ? 3 : 0;
			if (mine == their)
				return 3;
			const auto startsWith = [&](const std::string& head) {
				for (size_t n = 1; n <= theirs.size(); n++)
					if (Joined(theirs, n) == head)
						return true;
				return false;
			};
			if (file.size() >= 2 && startsWith(mine))
				return 2;
			if (file.size() >= 3)
				if (const std::string shorter = Joined(file, file.size() - 1); shorter == their || startsWith(shorter))
					return 1;
			return 0;
		}

		// Among names that fit as well: an American or worldwide release first, then Europe's, never a beta's
		// or a demo's before a release; then the shortest
		int Preference(const std::string& name)
		{
			int score = 0;
			if (name.find("(USA") != std::string::npos || name.find("(World") != std::string::npos)
				score += 4;
			else if (name.find("Europe") != std::string::npos)
				score += 2;
			for (const char* worse : {"(Beta", "(Proto", "(Demo", "(Sample", "(Kiosk", "(Pirate", "(Unl", "[b]", "(Aftermarket"})
				if (name.find(worse) != std::string::npos)
					score -= 8;
			return score;
		}

		// libretro's names in a console's folder (its Named_Boxarts listing), kept beside its covers for two
		// weeks. Read on the worker's thread only
		std::map<std::string, std::vector<std::string>> s_retroIndex;
		const std::vector<std::string>& RetroIndex(CURL* curl, const std::string& system)
		{
			auto [entry, added] = s_retroIndex.try_emplace(system);
			std::vector<std::string>& names = entry->second;
			if (!added)
				return names;
			const std::string file = fmt::format("{}/boxart/retro/{}/index.txt", ps5paths::Covers(), TitleFileName(system));
			if (RecentMarker(file))
			{
				std::ifstream in(file);
				for (std::string line; std::getline(in, line);)
					if (!line.empty())
						names.push_back(line);
				if (!names.empty())
					return names;
			}
			char* const folder = curl_easy_escape(curl, system.c_str(), 0);
			std::vector<uint8_t> page;
			const long status = folder ? Get(curl, fmt::format("http://{}/{}/Named_Boxarts/", kTitlesHost, folder), page) : -1;
			curl_free(folder);
			if (status != 200)
				return names;
			// an index page: each picture a link to "<its name>.png", escaped
			const std::string text(page.begin(), page.end());
			for (size_t at = 0; (at = text.find("href=\"", at)) != std::string::npos;)
			{
				at += 6;
				const size_t end = text.find('"', at);
				if (end == std::string::npos)
					break;
				const std::string link = text.substr(at, end - at);
				if (link.size() > 4 && link.ends_with(".png") && link.find('/') == std::string::npos)
				{
					int length = 0;
					if (char* plain = curl_easy_unescape(curl, link.c_str(), (int)link.size() - 4, &length))
					{
						names.emplace_back(plain, (size_t)length);
						curl_free(plain);
					}
				}
				at = end;
			}
			std::error_code ec;
			fs::create_directories(fs::path(file).parent_path(), ec);
			std::ofstream out(file, std::ios::trunc);
			for (const std::string& name : names)
				out << name << '\n';
			ps5log::Line("[boxart] {}: {} names in libretro's thumbnails", system, names.size());
			return names;
		}

		// The name in libretro's thumbnails that fits a game file's best (its console's folder, then the one
		// whose games it plays too: a Game Boy's and a Game Boy Color's), as {folder, name}; empty when none fits
		std::pair<std::string, std::string> RetroMatch(CURL* curl, const std::string& system, const std::string& name)
		{
			std::vector<std::string> folders = {system};
			if (system == "Nintendo - Game Boy Color")
				folders.push_back("Nintendo - Game Boy");
			else if (system == "Nintendo - Game Boy")
				folders.push_back("Nintendo - Game Boy Color");
			const std::vector<std::string> file = Words(name);
			std::pair<std::string, std::string> best;
			int bestFit = 0, bestPreference = 0;
			for (const std::string& folder : folders)
			{
				for (const std::string& theirs : RetroIndex(curl, folder))
				{
					const int fit = NameFit(file, Words(theirs));
					if (!fit)
						continue;
					const int preference = Preference(theirs);
					if (fit > bestFit || (fit == bestFit && (preference > bestPreference ||
						(preference == bestPreference && theirs.size() < best.second.size()))))
						best = {folder, theirs}, bestFit = fit, bestPreference = preference;
				}
				if (bestFit == 3)
					break;
			}
			return best;
		}

		void RetroWorker()
		{
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* const resolved = ps5net::AddResolve(ps5net::AddResolve(nullptr, kTitlesHost, 80), kTitlesHost, 443);
			static curl_slist* const headers = ps5curl::Headers();
			CURL* const curl = curl_easy_init();
			if (curl)
			{
				ps5curl::Setup(curl);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
				if (resolved)
					curl_easy_setopt(curl, CURLOPT_RESOLVE, resolved);
			}
			int fetched = 0, missing = 0;
			for (;;)
			{
				std::pair<std::string, std::string> item;
				{
					std::lock_guard lock(s_retroMutex);
					if (s_retroQueue.empty() || s_stopped || !s_enabled || !curl)
					{
						s_retroRunning = false;
						break;
					}
					item = std::move(s_retroQueue.front());
					s_retroQueue.pop_front();
				}
				const std::string base = RetroBase(item.first, item.second);
				std::error_code ec;
				fs::create_directories(fs::path(base).parent_path(), ec);
				char* const folder = curl_easy_escape(curl, item.first.c_str(), 0);
				char* const file = curl_easy_escape(curl, TitleFileName(item.second).c_str(), 0);
				if (!folder || !file)
				{
					curl_free(folder);
					curl_free(file);
					continue;
				}
				const std::string url = fmt::format("http://{}/{}/Named_Boxarts/{}.png", kTitlesHost, folder, file);
				curl_free(folder);
				curl_free(file);
				std::vector<uint8_t> data, rgba;
				int width = 0, height = 0;
				long status = Get(curl, url, data);
				// no picture by the file's own name (a name of the player's: "Pokemon Fire Red"): the one whose
				// name in libretro's list fits it ("Pokemon - FireRed Version (USA, Europe)")
				if (status == 404 || status == 410)
					if (const auto [matchFolder, match] = RetroMatch(curl, item.first, item.second); !match.empty())
					{
						char* const otherFolder = curl_easy_escape(curl, matchFolder.c_str(), 0);
						char* const otherFile = curl_easy_escape(curl, TitleFileName(match).c_str(), 0);
						if (otherFolder && otherFile)
						{
							status = Get(curl, fmt::format("http://{}/{}/Named_Boxarts/{}.png", kTitlesHost, otherFolder, otherFile), data);
							ps5log::Line("[boxart] {}: {} for {}", matchFolder, match, item.second);
						}
						curl_free(otherFolder);
						curl_free(otherFile);
					}
				if (status == 404 || status == 410)
				{
					std::ofstream(base + ".none") << kRetroMissed << '\n'; // asked for again after a while
					missing++;
				}
				else if (status == 200 && DecodePng(data, rgba, width, height) && width > 0 && height > 0)
				{
					int fitWidth = 0, fitHeight = 0;
					const std::vector<uint8_t> fitted = Fit(rgba, width, height, fitWidth, fitHeight);
					if (WriteTga(base + ".tga", fitted, fitWidth, fitHeight))
					{
						fetched++;
						s_retroArrivals++;
						s_arrivals++;
					}
				}
				else
					ps5log::Line("[boxart] {} / {}: HTTP {}; next start", item.first, item.second, status);
			}
			if (fetched || missing)
				ps5log::Line("[boxart] libretro's covers: {} fetched, {} not there", fetched, missing);
			if (curl)
				curl_easy_cleanup(curl);
		}

		// A game file's name as libretro's thumbnails have it: its own name, without what a hack or a copy adds
		std::string RetroName(std::string name)
		{
			while (!name.empty() && name.back() == ' ')
				name.pop_back();
			return name;
		}
	}

	std::string BestName(const std::vector<std::string>& theirs, const std::string& name)
	{
		const std::vector<std::string> file = Words(name);
		std::string best;
		int bestFit = 0, bestPreference = 0;
		for (const std::string& one : theirs)
		{
			const int fit = NameFit(file, Words(one));
			if (!fit)
				continue;
			const int preference = Preference(one);
			if (fit > bestFit || (fit == bestFit && (preference > bestPreference || (preference == bestPreference && one.size() < best.size()))))
				best = one, bestFit = fit, bestPreference = preference;
		}
		return best;
	}

	std::string RetroPath(const std::string& system, const std::string& name)
	{
		if (system.empty() || name.empty())
			return {};
		std::error_code ec;
		const std::string path = RetroBase(system, RetroName(name)) + ".tga";
		return fs::exists(path, ec) ? path : std::string();
	}

	void FetchRetro(const std::string& system, const std::vector<std::string>& names, bool again)
	{
		if (!s_enabled || s_stopped || system.empty())
			return;
		std::lock_guard lock(s_retroMutex);
		size_t queued = 0;
		for (const std::string& raw : names)
		{
			const std::string name = RetroName(raw);
			std::error_code ec;
			const std::string base = RetroBase(system, name);
			if (name.empty() || (!s_retroSeen.insert(system + "/" + name).second && !again) || fs::exists(base + ".tga", ec) ||
				(!again && RetroMissedRecently(base)))
				continue;
			s_retroQueue.emplace_back(system, name);
			queued++;
		}
		if (queued)
			ps5log::Line("[boxart] {}: {} covers to look for in libretro's thumbnails", system, queued);
		if (!s_retroRunning && !s_retroQueue.empty())
		{
			s_retroRunning = true;
			std::thread(RetroWorker).detach();
		}
	}

	size_t RetroPending()
	{
		std::lock_guard lock(s_retroMutex);
		return s_retroQueue.size() + (s_retroRunning ? 1 : 0);
	}

	// -- the Timeline's cartridges for the libretro sides: the LaunchBox Games Database's pictures, as
	// ps5gameinfo::FindCart names them -------------------------------------------------------------------

	namespace
	{
		constexpr const char* kCartsHost = "images.launchbox-app.com";
		std::mutex s_cartMutex;
		std::deque<std::string> s_cartQueue;		  // under s_cartMutex: the pictures to fetch
		std::set<std::string> s_cartSeen;			  // under s_cartMutex
		bool s_cartRunning = false;					  // under s_cartMutex

		std::string CartBase(const std::string& picture) { return fmt::format("{}/boxart/carts/{}", ps5paths::Covers(), picture); }

		bool CartValid(const std::string& picture)
		{
			return !picture.empty() && picture.size() <= 64 &&
				std::all_of(picture.begin(), picture.end(), [](char c) { return std::isalnum((unsigned char)c) || c == '-' || c == '_'; });
		}

		void CartWorker()
		{
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* const resolved = ps5net::AddResolve(nullptr, kCartsHost, 443);
			static curl_slist* const headers = ps5curl::Headers();
			CURL* const curl = curl_easy_init();
			if (curl)
			{
				ps5curl::Setup(curl);
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
				if (resolved)
					curl_easy_setopt(curl, CURLOPT_RESOLVE, resolved);
			}
			int fetched = 0;
			for (;;)
			{
				std::string picture;
				{
					std::lock_guard lock(s_cartMutex);
					if (s_cartQueue.empty() || s_stopped || !s_enabled || !curl)
					{
						s_cartRunning = false;
						break;
					}
					picture = std::move(s_cartQueue.front());
					s_cartQueue.pop_front();
				}
				const std::string base = CartBase(picture);
				std::error_code ec;
				fs::create_directories(fs::path(base).parent_path(), ec);
				std::vector<uint8_t> data, rgba;
				int width = 0, height = 0;
				const long status = Get(curl, fmt::format("https://{}/{}.png", kCartsHost, picture), data);
				int fitWidth = 0, fitHeight = 0;
				if (status == 200 && DecodePng(data, rgba, width, height) && width > 0 && height > 0)
				{
					const std::vector<uint8_t> fitted = Fit(rgba, width, height, fitWidth, fitHeight);
					if (WriteTga(base + ".tga", fitted, fitWidth, fitHeight))
					{
						fetched++;
						s_mediaArrivals++;
					}
				}
				else
					ps5log::Line("[boxart] cartridge {}: HTTP {}; next start", picture, status);
			}
			if (fetched)
				ps5log::Line("[boxart] the Timeline's cartridges: {} fetched", fetched);
			if (curl)
				curl_easy_cleanup(curl);
		}
	}

	std::string CartPath(const std::string& picture)
	{
		if (!CartValid(picture))
			return {};
		std::error_code ec;
		const std::string path = CartBase(picture) + ".tga";
		return fs::exists(path, ec) ? path : std::string();
	}

	void FetchCarts(const std::vector<std::string>& pictures)
	{
		if (!s_enabled || s_stopped)
			return;
		std::lock_guard lock(s_cartMutex);
		size_t queued = 0;
		for (const std::string& picture : pictures)
		{
			std::error_code ec;
			if (!CartValid(picture) || !s_cartSeen.insert(picture).second || fs::exists(CartBase(picture) + ".tga", ec))
				continue;
			s_cartQueue.push_back(picture);
			queued++;
		}
		if (queued)
			ps5log::Line("[boxart] {} cartridges to fetch for the Timeline", queued);
		if (!s_cartRunning && !s_cartQueue.empty())
		{
			s_cartRunning = true;
			std::thread(CartWorker).detach();
		}
	}

	void Fetch(System system, const std::vector<std::string>& ids)
	{
		if (!s_enabled || s_stopped)
			return;
		std::lock_guard lock(s_mutex);
		size_t queued = 0;
		for (const std::string& id : ids)
		{
			if (!Valid(id) || !s_queued.insert(fmt::format("{}/{}", Folder(system), id)).second)
				continue;
			s_queue.emplace_back(system, id);
			queued++;
		}
		ps5log::Line("[boxart] {}: {} games have a box ID, {} new to look for", Folder(system), ids.size(), queued);
		if (!s_started && !s_queue.empty())
		{
			s_started = true;
			std::thread(Worker).detach();
		}
		s_wake.notify_one();
	}

	void FetchMedia(System system, const std::vector<std::string>& ids)
	{
		if (!s_enabled || s_stopped)
			return;
		std::lock_guard lock(s_mutex);
		size_t queued = 0;
		for (const std::string& id : ids)
		{
			const std::string media = std::string(kMedia) + id;
			if (!Valid(id) || Here(system, media) || RecentlyNone(system, media) || !s_queued.insert(fmt::format("{}/{}", Folder(system), media)).second)
				continue;
			s_queue.emplace_back(system, media);
			queued++;
		}
		if (queued)
			ps5log::Line("[boxart] {}: {} discs or cards to look for", Folder(system), queued);
		if (!s_started && !s_queue.empty())
		{
			s_started = true;
			std::thread(Worker).detach();
		}
		s_wake.notify_one();
	}

	void Refetch(System system, const std::vector<std::string>& ids, bool replace)
	{
		std::vector<std::string> again;
		{
			std::lock_guard lock(s_mutex);
			for (const std::string& id : ids)
			{
				if (!Valid(id))
					continue;
				std::error_code ec;
				fs::remove(Base(system, id) + ".none", ec);
				if (replace)
					fs::remove(Base(system, id) + ".tga", ec); // the launcher keeps showing its copy until the new one arrives
				const std::string key = fmt::format("{}/{}", Folder(system), id);
				if (std::none_of(s_queue.begin(), s_queue.end(), [&](const auto& item) { return item.first == system && item.second == id; }))
					s_queued.erase(key);
				again.push_back(id);
			}
			s_retryNow = true;
			s_answered = -1;
		}
		ps5log::Line("[boxart] {}: {} covers asked for again{}", Folder(system), again.size(), replace ? ", those here replaced" : "");
		Fetch(system, again);
	}

	size_t Pending()
	{
		std::lock_guard lock(s_mutex);
		return s_queue.size() + (s_busy ? 1 : 0);
	}

	std::vector<std::string> TakeArrived()
	{
		std::lock_guard lock(s_mutex);
		std::vector<std::string> arrived;
		arrived.swap(s_arrived);
		return arrived;
	}

	uint32_t Arrivals()
	{
		return s_arrivals;
	}

	int Answered()
	{
		return s_answered;
	}

	void SetEnabled(bool enabled)
	{
		s_enabled = enabled;
	}

	void SetSpines(bool spines)
	{
		// turned on: the covers already here are asked for again, for their spines
		if (spines && !s_spines.exchange(true))
		{
			std::lock_guard lock(s_mutex);
			s_queued.clear();
		}
		s_spines = spines;
	}

	std::string SpinePath(const std::string& cover)
	{
		return cover.size() > 4 && cover.compare(cover.size() - 4, 4, ".tga") == 0 ? cover.substr(0, cover.size() - 4) + ".spine.tga" : std::string();
	}

	void Stop()
	{
		s_stopped = true;
		size_t dropped;
		{
			std::lock_guard lock(s_mutex);
			dropped = s_queue.size();
			s_queue.clear();
		}
		s_wake.notify_all(); // a wait to try again ends
		// a cover on its way is let finish (30 s at most, most take well under one), so no download
		// or file write runs beside the game; past 5 s the game starts anyway
		int waited = 0;
		for (; s_busy && waited < 250; waited++)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		ps5log::Line("[boxart] stopped for the game: {} covers left for the next start{}", dropped,
			s_busy ? "; one download still running" : waited ? fmt::format("; waited {} ms for one", waited * 20) : std::string());
	}

	bool ImageSize(const std::string& path, int& width, int& height)
	{
		uint8_t header[18];
		std::ifstream in(path, std::ios::binary);
		if (!in.read((char*)header, sizeof(header)))
			return false;
		width = header[12] | header[13] << 8;
		height = header[14] | header[15] << 8;
		return width > 0 && height > 0;
	}
}
