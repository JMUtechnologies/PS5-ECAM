// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Azahar's sound on the PS5's AudioOut (AudioCore::CreatePS5Sink, which the PS5's
// entry in Azahar's sink list names). As Cemu's PS5AudioAPI: a thread feeds AudioOut 256-frame
// stereo grains at 48 kHz, and sceAudioOutOutput, blocking until the previous grain plays, paces
// it.
//
// Azahar's sound is the 3DS's rate, 32728 Hz (AudioCore::native_sample_rate), as every sink of
// Azahar's takes it: its time stretcher keeps the speed, not the rate, and leaves resampling to the
// sink (cubeb, SDL and OpenAL resample in their libraries). AudioOut plays 48 kHz only, so the
// sink resamples here; asked for 48 kHz frames, the stretcher played the 3DS's sound half again too
// high and warbled filling the gap.

#include "audio_core/audio_types.h"
#include "audio_core/sink.h"
#include "../ps5/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

extern "C"
{
	int sceAudioOutInit(void);
	int sceAudioOutOpen(int userId, int type, int index, uint32_t length, uint32_t frequency, uint32_t format);
	int sceAudioOutClose(int handle);
	int sceAudioOutOutput(int handle, const void* samples);
	int sceAudioOutSetVolume(int handle, int flags, const int* volumes);
	void* scePthreadSelf();
	int scePthreadGetprio(void* thread, int* priority);
	int scePthreadSetprio(void* thread, int priority);
}

namespace AudioCore
{
	namespace
	{
		constexpr int kUserSystem = 0xff; // the port belongs to the system, not to one user
		constexpr int kPortMain = 0;
		constexpr uint32_t kGrain = 256; // frames per sceAudioOutOutput
		constexpr uint32_t kRate = 48000;
		constexpr uint32_t kFormatStereoS16 = 1;
		constexpr int kVolumeFlagsLeftRight = 3;
		constexpr int kVolume0dB = 32768;
		// 3DS frames per AudioOut frame
		constexpr double kStep = (double)native_sample_rate / kRate;
		// The output thread's priority (the console's: 256 is the highest, 767 the lowest, threads
		// start at 700): above the emulation's, so a busy CPU (shaders compiling on every core)
		// does not leave AudioOut without its next grain. It wakes every 5 ms and works briefly.
		constexpr int kAudioPriority = 384;

		class PS5Sink final : public Sink
		{
		public:
			PS5Sink()
			{
				sceAudioOutInit(); // an error when the launcher or Cemu has already: the port says
				m_port = sceAudioOutOpen(kUserSystem, kPortMain, 0, kGrain, kRate, kFormatStereoS16);
				if (m_port < 0)
				{
					ps5log::Line("[azahar] AudioOut did not open ({:#x}): no sound", (uint32_t)m_port);
					return;
				}
				const std::array<int, 8> volumes{kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB};
				sceAudioOutSetVolume(m_port, kVolumeFlagsLeftRight, volumes.data());
				m_thread = std::thread(&PS5Sink::Output, this);
			}

			~PS5Sink() override
			{
				m_quit = true;
				if (m_thread.joinable())
					m_thread.join();
				if (m_port >= 0)
				{
					sceAudioOutOutput(m_port, nullptr); // wait for the last grain
					sceAudioOutClose(m_port);
				}
			}

			unsigned int GetNativeSampleRate() const override
			{
				return native_sample_rate;
			}

			void SetCallback(std::function<void(s16*, std::size_t)> callback) override
			{
				std::lock_guard lock(m_mutex);
				m_callback = std::move(callback);
			}

		private:
			static void RaisePriority()
			{
				void* self = scePthreadSelf();
				int was = 0;
				scePthreadGetprio(self, &was);
				if (was > 0 && was <= kAudioPriority)
					return;
				const int result = scePthreadSetprio(self, kAudioPriority);
				ps5log::Line("[azahar] sound thread priority {} -> {}{}", was, kAudioPriority,
					result == 0 ? std::string() : fmt::format(" refused ({:#x}): it stays", (uint32_t)result));
			}

			static s16 Clamp(float value)
			{
				return (s16)std::clamp(value, -32768.0f, 32767.0f);
			}

			// The next grain at 48 kHz from Azahar's frames at 32728 Hz: Catmull-Rom between the four
			// frames around each point. m_in keeps a frame before the position for the curve.
			void Resample(std::array<s16, kGrain * 2>& grain)
			{
				const double last = m_position + (kGrain - 1) * kStep;
				const size_t needed = (size_t)last + 3; // through the frame two past the last point
				const size_t have = m_in.size() / 2;
				if (needed > have)
				{
					m_in.resize(needed * 2);
					std::lock_guard lock(m_mutex);
					if (m_callback)
						m_callback(m_in.data() + have * 2, needed - have);
					else
						std::fill(m_in.begin() + have * 2, m_in.end(), (s16)0);
				}
				for (uint32_t frame = 0; frame < kGrain; frame++)
				{
					const double at = m_position + frame * kStep;
					const size_t k = (size_t)at;
					const float t = (float)(at - k);
					for (int channel = 0; channel < 2; channel++)
					{
						const float y0 = m_in[(k - 1) * 2 + channel], y1 = m_in[k * 2 + channel];
						const float y2 = m_in[(k + 1) * 2 + channel], y3 = m_in[(k + 2) * 2 + channel];
						const float value = y1 + 0.5f * t * ((y2 - y0) + t * ((2 * y0 - 5 * y1 + 4 * y2 - y3) + t * (3 * (y1 - y2) + y3 - y0)));
						grain[frame * 2 + channel] = Clamp(value);
					}
				}
				// the frames behind the next grain's first point (all but the one before it) are spent
				m_position += kGrain * kStep;
				const size_t spent = (size_t)m_position - 1;
				m_in.erase(m_in.begin(), m_in.begin() + spent * 2);
				m_position -= (double)spent;
			}

			void Output()
			{
				RaisePriority();
				std::array<s16, kGrain * 2> grain{};
				m_in.assign(2, 0); // a silent frame before the first, for the curve
				m_position = 1.0;
				while (!m_quit)
				{
					Resample(grain);
					if (sceAudioOutOutput(m_port, grain.data()) < 0)
					{
						ps5log::Line("[azahar] AudioOut output failed: the sound stops");
						return;
					}
				}
			}

			int m_port = -1;
			std::thread m_thread;
			std::atomic_bool m_quit = false;
			std::mutex m_mutex;
			std::function<void(s16*, std::size_t)> m_callback;
			// the output thread's: Azahar's frames not yet played past, and where the next grain starts in them
			std::vector<s16> m_in;
			double m_position = 1.0;
		};
	}

	std::unique_ptr<Sink> CreatePS5Sink(std::string_view)
	{
		return std::make_unique<PS5Sink>();
	}
}
