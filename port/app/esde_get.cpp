// SPDX-License-Identifier: GPL-3.0-or-later
#include "esde_get.h"
#include "update_files.h"
#include "lang.h"
#include "paths.h"
#include "../esde/esde.h"
#include "../ps5/curl_socket.h"
#include "../ps5/log.h"
#include "../ps5/net.h"

#include <curl/curl.h>
#include <rapidjson/document.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

extern "C" int sceNetInit(void);

namespace fs = std::filesystem;

namespace ps5esdeget
{
	namespace
	{
		// the repository by its names, newest first (GitHub sends an old name on to the new one)
		constexpr const char* kRepositories[] = {"premohq/PlayTendoh", "premohq/PS5-ECAM"};
		// the API, then where a release's asset is served from (GitHub sends it on, from one of the others)
		constexpr const char* kHosts[] = {"api.github.com", "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"};
		constexpr uint64_t kMostBytes = 2048ull << 20;

		std::mutex s_mutex;
		Status s_status; // under s_mutex
		std::thread s_thread;
		std::atomic<bool> s_stop{false};
		std::atomic<bool> s_running{false};

		void Set(Status::State state, const std::string& message = {})
		{
			std::lock_guard lock(s_mutex);
			s_status.state = state;
			s_status.message = message;
		}

		void Fail(const std::string& message)
		{
			ps5log::Line("[es-de] {}", message);
			Set(Status::State::Failed, message);
		}

		int Progress(void*, curl_off_t total, curl_off_t received, curl_off_t, curl_off_t)
		{
			{
				std::lock_guard lock(s_mutex);
				if (s_status.state == Status::State::Downloading)
				{
					s_status.total = total > 0 ? (uint64_t)total : s_status.total;
					s_status.received = (uint64_t)received;
				}
			}
			return s_stop ? 1 : 0;
		}

		CURL* Session(curl_slist* resolve, const std::string& url)
		{
			CURL* curl = curl_easy_init();
			if (!curl)
				return nullptr;
			ps5curl::Setup(curl);
			static const std::string ca = ps5paths::Assets() + "/cacert.pem";
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			if (resolve)
				curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "Playtendoh/" PS5CEMU_VERSION);
			curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
			curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
			curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
			return curl;
		}

		size_t Collect(char* data, size_t size, size_t count, void* out)
		{
			auto* body = static_cast<std::string*>(out);
			if (body->size() > 1024 * 1024)
				return 0;
			body->append(data, size * count);
			return size * count;
		}

		struct File
		{
			std::ofstream out;
			uint64_t written = 0;
		};

		size_t Write(char* data, size_t size, size_t count, void* user)
		{
			auto* file = static_cast<File*>(user);
			const size_t bytes = size * count;
			if (file->written + bytes > kMostBytes)
				return 0;
			file->out.write(data, (std::streamsize)bytes);
			file->written += bytes;
			return file->out ? bytes : 0;
		}

		// the app's own folder, where the update puts its files (updates.cpp's AppFolder): empty when the
		// app runs from an image, whose files can't be added to
		fs::path AppFolder()
		{
			bool image = false;
			const std::string source = ps5paths::MountSource(&image);
			if (!source.empty())
				return source;
			if (image)
				return {};
			std::error_code ec;
			for (const std::string& folder : ps5paths::InstallFolders())
				if (fs::exists(folder + "/eboot.bin", ec) && ps5paths::RunsFrom(folder))
					return folder;
			return fs::exists(ps5paths::kMountedEboot, ec) ? fs::path() : fs::path(ps5paths::AppDir());
		}

		// Where the release's asset is, its size and its SHA-256: GitHub's API, each name of the repository.
		// missing: GitHub answered, and has no such release or asset (not the network's fault)
		bool Find(curl_slist* resolve, std::string& url, uint64_t& size, std::string& sha, bool& missing)
		{
			missing = false;
			for (const char* repository : kRepositories)
			{
				std::string body;
				CURL* curl = Session(resolve, std::string("https://api.github.com/repos/") + repository + "/releases/tags/" + kTag);
				if (!curl)
					return false;
				curl_slist* headers = ps5curl::Headers(curl_slist_append(nullptr, "Accept: application/vnd.github+json"));
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
				curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
				const CURLcode result = curl_easy_perform(curl);
				long http = 0;
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
				curl_easy_cleanup(curl);
				curl_slist_free_all(headers);
				if (result != CURLE_OK || http != 200)
				{
					ps5log::Line("[es-de] {}'s {}: {} (HTTP {})", repository, kTag, curl_easy_strerror(result), http);
					missing |= result == CURLE_OK && http == 404;
					continue;
				}
				rapidjson::Document json;
				json.Parse(body.c_str());
				if (json.HasParseError() || !json.IsObject() || !json.HasMember("assets") || !json["assets"].IsArray())
					continue;
				for (const auto& asset : json["assets"].GetArray())
				{
					if (!asset.IsObject() || !asset.HasMember("name") || !asset["name"].IsString() || std::string(asset["name"].GetString()) != kAsset)
						continue;
					if (!asset.HasMember("browser_download_url") || !asset["browser_download_url"].IsString())
						continue;
					url = asset["browser_download_url"].GetString();
					size = asset.HasMember("size") && asset["size"].IsUint64() ? asset["size"].GetUint64() : 0;
					// "sha256:<hex>", as GitHub gives every asset's
					const std::string digest = asset.HasMember("digest") && asset["digest"].IsString() ? asset["digest"].GetString() : "";
					sha = digest.rfind("sha256:", 0) == 0 ? digest.substr(7) : "";
					return !sha.empty();
				}
				ps5log::Line("[es-de] {}'s {} has no {}", repository, kTag, kAsset);
				missing = true;
			}
			return false;
		}

		void Install(curl_slist* resolve)
		{
			std::string url, sha;
			uint64_t size = 0;
			bool missing = false;
			if (!Find(resolve, url, size, sha, missing))
			{
				Fail(s_stop	   ? ps5lang::Tr("Stopped for a game")
					 : missing ? ps5lang::Tr("EmulationStation isn't on GitHub for this version yet. Try again after an update.")
							   : ps5lang::Tr("EmulationStation could not be downloaded. Is the PS5 online?"));
				return;
			}
			const fs::path app = AppFolder();
			if (app.empty())
			{
				Fail(ps5lang::Tr("Playtendoh runs from an image that can't be changed. Copy the es-de folder into it yourself."));
				return;
			}
			std::error_code ec;
			if (const fs::space_info space = fs::space(app, ec); !ec && size > 0 && space.available < size * 2 + (256ull << 20))
			{
				Fail(ps5lang::TrF("EmulationStation needs {0} MB free on the drive Playtendoh is on", (size * 2 + (256ull << 20)) >> 20));
				return;
			}
			ps5log::Line("[es-de] downloading {} ({} MB)", url, size >> 20);
			{
				std::lock_guard lock(s_mutex);
				s_status.state = Status::State::Downloading;
				s_status.received = 0;
				s_status.total = size;
			}
			const fs::path archive = fs::path(ps5paths::Cache()) / "es-de.zip";
			fs::create_directories(archive.parent_path(), ec);
			File file;
			file.out.open(archive, std::ios::binary | std::ios::trunc);
			CURL* curl = Session(resolve, url);
			if (!curl || !file.out)
			{
				if (curl)
					curl_easy_cleanup(curl);
				Fail(ps5lang::Tr("The download could not start"));
				return;
			}
			curl_slist* headers = ps5curl::Headers();
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
			const CURLcode result = curl_easy_perform(curl);
			long http = 0;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
			curl_easy_cleanup(curl);
			curl_slist_free_all(headers);
			file.out.close();
			if (result != CURLE_OK || http != 200 || file.written == 0)
			{
				fs::remove(archive, ec);
				ps5log::Line("[es-de] the download failed ({}, HTTP {})", s_stop ? "stopped" : curl_easy_strerror(result), http);
				Fail(s_stop ? ps5lang::Tr("Stopped") : ps5lang::Tr("The download did not finish: is the PS5 online?"));
				return;
			}
			Set(Status::State::Verifying);
			const std::string actual = ps5update::Sha256Of(archive);
			if (actual != sha)
			{
				fs::remove(archive, ec);
				Fail(ps5lang::TrF("The download is damaged (SHA-256 {0}, not {1})", actual.substr(0, 12), sha.substr(0, 12)));
				return;
			}
			// unpacked beside the app (the same drive), then moved in as one folder
			Set(Status::State::Installing);
			const fs::path staging = app.string() + ".es-de";
			ps5update::RemoveAll(staging);
			std::vector<fs::path> files;
			std::string error;
			if (!ps5update::Unpack(archive, staging, files, error, s_stop, ps5esde::kProgram))
			{
				ps5update::RemoveAll(staging);
				fs::remove(archive, ec);
				Fail(ps5lang::TrF("It could not be unpacked: {0}", error));
				return;
			}
			fs::remove(archive, ec);
			const fs::path target = app / "es-de";
			ps5update::RemoveAll(target);
			fs::rename(staging / "es-de", target, ec);
			ps5update::RemoveAll(staging);
			if (ec || !ps5esde::Available())
			{
				Fail(ps5lang::TrF("EmulationStation could not be installed ({0})", ec ? ec.message() : std::string("es-de.bin")));
				return;
			}
			ps5log::Line("[es-de] installed in {} ({} files)", target.string(), files.size());
			Set(Status::State::Done);
		}

		void Run()
		{
			struct Running
			{
				~Running() { s_running = false; }
			} running;
			sceNetInit();
			curl_global_init(CURL_GLOBAL_DEFAULT);
			curl_slist* resolve = nullptr;
			for (const char* host : kHosts)
				if (!s_stop)
					resolve = ps5net::AddResolve(resolve, host, 443);
			if (!resolve)
			{
				Fail(ps5lang::Tr("GitHub could not be found: is the PS5 online?"));
				return;
			}
			Install(resolve);
			curl_slist_free_all(resolve);
		}
	}

	void Start()
	{
		{
			std::lock_guard lock(s_mutex);
			if (s_running)
				return;
			s_status = {};
			s_status.state = Status::State::Finding;
		}
		if (s_thread.joinable())
			s_thread.join();
		s_stop = false;
		s_running = true;
		s_thread = std::thread(Run);
	}

	Status GetStatus()
	{
		std::lock_guard lock(s_mutex);
		return s_status;
	}

	bool Busy() { return s_running; }

	bool Cancel()
	{
		{
			std::lock_guard lock(s_mutex);
			if (!s_running || s_status.state == Status::State::Installing)
				return false;
		}
		s_stop = true;
		if (s_thread.joinable())
			s_thread.join();
		Set(Status::State::Idle);
		return true;
	}

	void Stop()
	{
		s_stop = true;
		if (s_thread.joinable())
			s_thread.join();
	}

	bool Remove(std::string& error)
	{
		if (s_running)
		{
			error = ps5lang::Tr("EmulationStation is downloading");
			return false;
		}
		const fs::path app = AppFolder();
		if (app.empty())
		{
			error = ps5lang::Tr("Playtendoh runs from an image that can't be changed");
			return false;
		}
		ps5update::RemoveAll(app / "es-de");
		if (ps5esde::Available())
		{
			error = ps5lang::Tr("Its folder could not be removed");
			return false;
		}
		Set(Status::State::Idle);
		return true;
	}
}
