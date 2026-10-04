// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: Cemu's audio output on the PS5's AudioOut: the TV's sound on the console's output, and
// the GamePad's, when the launcher's setting asks for it, on player 1's DualSense speaker.

#pragma once

#include "IAudioAPI.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

class PS5AudioAPI : public IAudioAPI
{
public:
	class PS5DeviceDescription : public DeviceDescription
	{
	public:
		PS5DeviceDescription(const wchar_t* name, const wchar_t* id) : DeviceDescription(name), m_id(id) {}
		std::wstring GetIdentifier() const override { return m_id; }

	private:
		const wchar_t* m_id;
	};

	// the devices: the console's main audio output, and player 1's DualSense speaker
	static constexpr const wchar_t* kDeviceId = L"ps5-audioout";
	static constexpr const wchar_t* kPadSpeakerId = L"ps5-padspeaker";

	PS5AudioAPI(bool padSpeaker, uint32 samplerate, uint32 channels, uint32 samples_per_block, uint32 bits_per_sample);
	~PS5AudioAPI() override;

	AudioAPI GetType() const override { return PS5AudioOut; }
	bool NeedAdditionalBlocks() const override;
	bool FeedBlock(sint16* data) override;
	bool Play() override;
	bool Stop() override;

	static std::vector<DeviceDescriptionPtr> GetDevices();
	static bool InitializeStatic();

private:
	void OutputThread();

	int m_port = -1;
	bool m_padSpeaker = false; // one channel, on the DualSense
	std::thread m_thread;
	bool m_quit = false;

	mutable std::mutex m_mutex;
	std::condition_variable m_wake;
	std::vector<sint16> m_queue; // interleaved samples at the stream's channel count
	size_t m_queueCapacity = 0;
};
