// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarders.h"
#include "lang.h"
#include "paths.h"
#include "../ps5/log.h"
#include "../ui/images.h"

#include "psfwd.h"

// stb's PNG writer, static to this file, for the tile's icon0.png
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma clang diagnostic pop

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace ps5forwarders
{
	namespace
	{
		constexpr const char* kRoot = PSFWD_DEFAULT_ROOT; // /data/homebrew
		constexpr int kIconSize = 512;					  // the format's icon0.png

		std::mutex s_mutex;
		Status s_status; // under s_mutex
		std::thread s_thread;

		bool Exists(const std::string& path)
		{
			struct stat info{};
			return stat(path.c_str(), &info) == 0;
		}

		// The tile made for this game before: a forwarder in /data/homebrew that starts PS5CEMU-HAR
		// with the same --rom
		std::string Earlier(const std::string& rom)
		{
			DIR* dir = opendir(kRoot);
			if (!dir)
				return {};
			std::string found;
			while (const dirent* entry = readdir(dir))
			{
				if (!psfwd_valid_title_id(entry->d_name))
					continue;
				static psfwd_info info; // 5 KB: off the stack of a thread of the app's
				std::memset(&info, 0, sizeof(info));
				if (!psfwd_read((std::string(kRoot) + "/" + entry->d_name).c_str(), &info) || std::strcmp(info.target, ps5paths::kTitleId) != 0)
					continue;
				for (int i = 0; i + 1 < info.argc; i++)
					if (std::strcmp(info.argv[i], "--rom") == 0 && rom == info.argv[i + 1])
						found = info.title_id;
				if (!found.empty())
					break;
			}
			closedir(dir);
			return found;
		}

		// A free ID from the format's range that no app on the console has: PS5CEMU-HAR's own is in
		// that range, and an app installed on a drive has no folder in /data/homebrew
		std::string NewTitleId()
		{
			for (int attempt = 0; attempt < 64; attempt++)
			{
				char id[PSFWD_TITLE_ID_SIZE] = {};
				if (!psfwd_new_title_id(kRoot, id))
					return {};
				if (std::strcmp(id, ps5paths::kTitleId) != 0 && !Exists(std::string("/user/app/") + id) &&
					!Exists(std::string("/system_ex/app/") + id))
					return id;
			}
			return {};
		}

		void Append(void* context, void* data, int size)
		{
			auto* out = static_cast<std::vector<uint8_t>*>(context);
			out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
		}

		// The tile's picture: the cover made square as a PNG, else the app's own icon
		std::vector<uint8_t> Icon(const std::string& cover)
		{
			std::vector<uint8_t> png;
			std::vector<uint8_t> rgba;
			if (!cover.empty() && ui::SquarePicture(cover, kIconSize, rgba))
			{
				std::vector<uint8_t> rgb((size_t)kIconSize * kIconSize * 3);
				for (size_t i = 0, o = 0; i < rgba.size(); i += 4, o += 3)
				{
					rgb[o] = rgba[i];
					rgb[o + 1] = rgba[i + 1];
					rgb[o + 2] = rgba[i + 2];
				}
				if (stbi_write_png_to_func(Append, &png, kIconSize, kIconSize, 3, rgb.data(), kIconSize * 3) && !png.empty())
					return png;
				png.clear();
			}
			if (FILE* file = std::fopen((ps5paths::AppDir() + "/sce_sys/icon0.png").c_str(), "rb"))
			{
				uint8_t buffer[64 * 1024];
				for (size_t read; (read = std::fread(buffer, 1, sizeof(buffer), file)) > 0;)
					png.insert(png.end(), buffer, buffer + read);
				std::fclose(file);
			}
			return png;
		}

		void Finish(Status::State state, const std::string& titleId, bool replaced, const std::string& message)
		{
			std::lock_guard lock(s_mutex);
			s_status.state = state;
			s_status.titleId = titleId;
			s_status.folder = titleId.empty() ? std::string() : std::string(kRoot) + "/" + titleId;
			s_status.replaced = replaced;
			s_status.message = message;
			s_status.finished++;
		}

		void Run(Request request)
		{
			const std::vector<uint8_t> icon = Icon(request.cover);
			if (icon.empty())
			{
				Finish(Status::State::Failed, {}, false, ps5lang::Tr("The game has no picture for its tile"));
				return;
			}
			std::string titleId = Earlier(request.rom);
			const bool replaced = !titleId.empty();
			if (titleId.empty())
				titleId = NewTitleId();
			if (titleId.empty())
			{
				Finish(Status::State::Failed, {}, false, ps5lang::Tr("No free title ID for a tile"));
				return;
			}
			const char* argv[] = {"--rom", request.rom.c_str(), "--exit-after-game"};
			psfwd_spec spec{};
			spec.title_id = titleId.c_str();
			spec.name = request.name.c_str();
			spec.target = ps5paths::kTitleId;
			spec.argc = 3;
			spec.argv = argv;
			spec.creator = "PS5CEMU-HAR " PS5CEMU_VERSION;
			spec.icon0_png = {icon.data(), icon.size()};
			char error[256] = {};
			const std::string templateDir = ps5paths::Assets() + "/forwarder";
			if (!psfwd_write(kRoot, templateDir.c_str(), &spec, error, sizeof(error)))
			{
				ps5log::Line("[tiles] {}'s tile not made: {}", request.name, error);
				Finish(Status::State::Failed, titleId, replaced, error);
				return;
			}
			ps5log::Line("[tiles] {}/{}: {} ({}; --rom {}, a {} KB picture)", kRoot, titleId, request.name, replaced ? "made again" : "new",
				request.rom, icon.size() >> 10);
			Finish(Status::State::Done, titleId, replaced, {});
		}
	}

	void Start(const Request& request)
	{
		{
			std::lock_guard lock(s_mutex);
			if (s_status.state == Status::State::Making)
				return;
			const uint32_t finished = s_status.finished;
			s_status = {};
			s_status.finished = finished;
			s_status.state = Status::State::Making;
		}
		if (s_thread.joinable())
			s_thread.join();
		s_thread = std::thread(Run, request);
	}

	Status GetStatus()
	{
		std::lock_guard lock(s_mutex);
		return s_status;
	}

	void Stop()
	{
		if (s_thread.joinable())
			s_thread.join();
	}
}
