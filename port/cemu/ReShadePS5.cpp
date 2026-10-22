// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReShadePS5.h"
#include "ReShadeEffects.h"
#include "ReShadeRuntime.h"

#include <fmt/format.h>
#include <pthread.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// the port's (app/emulator.cpp): the setting and the boot log
uint32_t PS5Cemu_ReShade();
void PS5Cemu_LogLine(std::string_view line);

namespace ps5reshade
{
	namespace
	{
		constexpr uint64_t kRetireFrames = 8;			   // frames a replaced runtime is kept for, past the GPU's last use of it
		constexpr size_t kCompileStack = 16 * 1024 * 1024; // ReShade's parser recurses through an effect's expressions

		// what a runtime is compiled for
		struct Key
		{
			VkFormat format = VK_FORMAT_UNDEFINED;
			uint32_t width = 0, height = 0;
			uint64_t titleId = 0;
			bool operator==(const Key&) const = default;
		};

		struct Job
		{
			Key key;
			VkDevice device;
			VkPhysicalDevice physical;
		};

		struct State
		{
			std::mutex mutex;				// over ready, readyKey, building, tried
			std::unique_ptr<Runtime> ready; // compiled, not yet drawn with
			Key readyKey;
			bool building = false;
			bool triedAny = false;
			Key tried; // the last one compiled, whether anything runs or not: not compiled again
			// the renderer's thread's own
			std::unique_ptr<Runtime> current;
			Key currentKey;
			std::vector<std::pair<std::unique_ptr<Runtime>, uint64_t>> retired; // and the frame each was replaced at
			uint64_t frame = 0;
		};
		State s;

		// reshade::api::format for a swapchain's format, and its bits a colour
		void Describe(VkFormat format, Target& target)
		{
			switch (format)
			{
			case VK_FORMAT_R8G8B8A8_UNORM: target.colorFormat = 28; break;
			case VK_FORMAT_R8G8B8A8_SRGB: target.colorFormat = 29; break;
			case VK_FORMAT_B8G8R8A8_SRGB: target.colorFormat = 91; break;
			case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
				target.colorFormat = 24;
				target.colorBitDepth = 10;
				break;
			default: target.colorFormat = 87; break; // b8g8r8a8_unorm, VideoOut's
			}
		}

		// The preset compiled and its GPU objects made, on a thread of its own
		void* Compile(void* argument)
		{
			std::unique_ptr<Job> job(static_cast<Job*>(argument));
			const auto start = std::chrono::steady_clock::now();
			Target target;
			target.width = job->key.width;
			target.height = job->key.height;
			Describe(job->key.format, target);
			Chain chain;
			Build(Root(), job->key.titleId, target, chain);
			for (const std::string& message : chain.messages)
				PS5Cemu_LogLine("[reshade] " + message);
			const std::string summary = chain.Summary();
			const std::string preset = chain.preset.string();
			std::unique_ptr<Runtime> runtime;
			std::string error;
			if (!chain.techniques.empty())
				runtime = Runtime::Create(job->device, job->physical, std::move(chain), job->key.format, {job->key.width, job->key.height}, error);
			const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			if (runtime)
				PS5Cemu_LogLine(fmt::format("[reshade] {} from {}, on {}x{} pictures (compiled in {} ms)", runtime->GetChain().Summary(), preset,
					job->key.width, job->key.height, ms));
			else if (!error.empty())
				PS5Cemu_LogLine(fmt::format("[reshade] nothing runs: {} ({})", error, summary));
			else
				PS5Cemu_LogLine(fmt::format("[reshade] nothing runs: {} (in {})", summary, Root().string()));
			std::lock_guard lock(s.mutex);
			s.ready = std::move(runtime); // one compiled for an earlier picture was never drawn with
			s.readyKey = job->key;
			s.building = false;
			return nullptr;
		}

		void StartCompile(const Key& key, const Picture& picture)
		{
			s.building = true;
			s.triedAny = true;
			s.tried = key;
			auto* job = new Job{key, picture.device, picture.physical};
			pthread_attr_t attributes;
			pthread_attr_init(&attributes);
			pthread_attr_setstacksize(&attributes, kCompileStack);
			pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
			pthread_t thread;
			if (const int error = pthread_create(&thread, &attributes, Compile, job); error != 0)
			{
				delete job;
				s.building = false;
				PS5Cemu_LogLine(fmt::format("[reshade] the preset's compile could not start ({})", error));
			}
			pthread_attr_destroy(&attributes);
		}
	}

	bool Wanted()
	{
		return PS5Cemu_ReShade() != 0;
	}

	void Apply(const Picture& picture)
	{
		s.frame++;
		// replaced runtimes, once the GPU is past their last frame
		std::erase_if(s.retired, [](const auto& retired) { return s.frame - retired.second > kRetireFrames; });
		const Key key{picture.format, picture.extent.width, picture.extent.height, picture.titleId};
		{
			std::lock_guard lock(s.mutex);
			if (s.ready && s.readyKey == key)
			{
				if (s.current)
					s.retired.emplace_back(std::move(s.current), s.frame);
				s.current = std::move(s.ready);
				s.currentKey = key;
			}
			const bool have = s.current && s.currentKey == key;
			if (!have && !s.building && !(s.triedAny && s.tried == key))
				StartCompile(key, picture);
		}
		// the frames before it is compiled are shown as they are
		if (s.current && s.currentKey == key)
			s.current->Record(picture.commands, picture.image, picture.layout);
	}
}
