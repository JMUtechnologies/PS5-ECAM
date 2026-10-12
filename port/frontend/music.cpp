// SPDX-License-Identifier: GPL-3.0-or-later
#include "music.h"
#include "../ps5/log.h"

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace ps5music
{
	namespace
	{
		constexpr size_t kChunk = 4096; // frames read from a source at a time

		uint32_t Read16(const uint8_t* at) { return at[0] | at[1] << 8; }
		uint32_t Read32(const uint8_t* at) { return Read16(at) | Read16(at + 2) << 16; }

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return text;
		}

		class FramesSource : public Source
		{
		public:
			FramesSource(std::vector<int16_t> stereo, uint32_t frameRate) : m_frames(std::move(stereo)) { rate = frameRate; }
			size_t Read(int16_t* out, size_t frames) override
			{
				const size_t count = std::min(frames, m_frames.size() / 2 - m_at);
				std::memcpy(out, m_frames.data() + m_at * 2, count * 2 * sizeof(int16_t));
				m_at += count;
				return count;
			}
			bool Rewind() override
			{
				m_at = 0;
				return !m_frames.empty();
			}

		private:
			std::vector<int16_t> m_frames;
			size_t m_at = 0;
		};

		// the first two channels of interleaved frames, as stereo (a mono one on both sides)
		void ToStereo(const int16_t* in, uint32_t channels, size_t frames, int16_t* out)
		{
			for (size_t frame = 0; frame < frames; frame++)
			{
				out[frame * 2] = in[frame * channels];
				out[frame * 2 + 1] = in[frame * channels + (channels > 1 ? 1 : 0)];
			}
		}

		// An MP3, decoded as it plays
		class Mp3Source : public Source
		{
		public:
			bool Open(const std::string& path)
			{
				m_open = drmp3_init_file(&m_mp3, path.c_str(), nullptr);
				if (!m_open || m_mp3.channels < 1 || m_mp3.sampleRate < 8000 || m_mp3.sampleRate > 192000)
					return false;
				rate = m_mp3.sampleRate;
				return true;
			}
			~Mp3Source() override
			{
				if (m_open)
					drmp3_uninit(&m_mp3);
			}
			size_t Read(int16_t* out, size_t frames) override
			{
				m_buffer.resize(frames * m_mp3.channels);
				const size_t count = (size_t)drmp3_read_pcm_frames_s16(&m_mp3, frames, m_buffer.data());
				ToStereo(m_buffer.data(), m_mp3.channels, count, out);
				return count;
			}
			bool Rewind() override { return drmp3_seek_to_pcm_frame(&m_mp3, 0); }

		private:
			drmp3 m_mp3{};
			bool m_open = false;
			std::vector<int16_t> m_buffer;
		};

		// A WAV of 16-bit PCM, read as it plays
		class WavSource : public Source
		{
		public:
			bool Open(const std::string& path)
			{
				m_file.open(path, std::ios::binary);
				uint8_t header[12];
				if (!m_file.read((char*)header, 12) || std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
					return false;
				uint32_t format = 0, bits = 0;
				for (;;)
				{
					uint8_t chunk[8];
					if (!m_file.read((char*)chunk, 8))
						return false;
					const uint32_t size = Read32(chunk + 4);
					if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16)
					{
						uint8_t fmt[16];
						if (!m_file.read((char*)fmt, 16))
							return false;
						format = Read16(fmt);
						m_channels = Read16(fmt + 2);
						rate = Read32(fmt + 4);
						bits = Read16(fmt + 14);
						m_file.seekg(size - 16 + (size & 1), std::ios::cur);
					}
					else if (std::memcmp(chunk, "data", 4) == 0)
					{
						m_start = m_file.tellg();
						m_frames = m_channels ? size / (2 * m_channels) : 0;
						break;
					}
					else
						m_file.seekg(size + (size & 1), std::ios::cur);
				}
				// PCM, or WAVE_FORMAT_EXTENSIBLE holding it
				return (format == 1 || format == 0xfffe) && bits == 16 && m_channels >= 1 && m_channels <= 8 && rate >= 8000 && rate <= 192000 &&
					m_frames > 0;
			}
			size_t Read(int16_t* out, size_t frames) override
			{
				const size_t count = std::min<size_t>(frames, m_frames - m_at);
				m_buffer.resize(count * m_channels);
				if (count && !m_file.read((char*)m_buffer.data(), (std::streamsize)(count * m_channels * 2)))
					return 0;
				ToStereo(m_buffer.data(), m_channels, count, out);
				m_at += count;
				return count;
			}
			bool Rewind() override
			{
				m_file.clear();
				m_file.seekg(m_start);
				m_at = 0;
				return (bool)m_file;
			}

		private:
			std::ifstream m_file;
			std::streampos m_start;
			uint32_t m_channels = 0;
			size_t m_frames = 0, m_at = 0;
			std::vector<int16_t> m_buffer;
		};
	}

	std::unique_ptr<Source> FromFrames(std::vector<int16_t> stereo, uint32_t rate)
	{
		return std::make_unique<FramesSource>(std::move(stereo), rate);
	}

	std::unique_ptr<Source> OpenFile(const std::string& path)
	{
		const std::string extension = Lower(std::filesystem::path(path).extension().string());
		if (extension == ".mp3")
		{
			auto mp3 = std::make_unique<Mp3Source>();
			if (mp3->Open(path))
				return mp3;
		}
		else if (extension == ".wav")
		{
			auto wav = std::make_unique<WavSource>();
			if (wav->Open(path))
				return wav;
		}
		return nullptr;
	}

	std::vector<std::string> Files(const std::string& folder)
	{
		std::vector<std::string> files;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
		{
			const std::string extension = Lower(entry.path().extension().string());
			if ((extension == ".mp3" || extension == ".wav") && entry.is_regular_file(ec))
				files.push_back(entry.path().filename().string());
		}
		std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
		return files;
	}

	Player::Player(std::string folder, std::function<std::unique_ptr<Source>()> setup)
		: m_folder(std::move(folder)), m_setup(std::move(setup)),
		  m_random((uint32_t)std::chrono::steady_clock::now().time_since_epoch().count()), m_chunk(kChunk * 2)
	{
	}

	bool Player::OpenNextShuffled()
	{
		for (size_t tries = 0; tries < m_order.size(); tries++)
		{
			if (m_next >= m_order.size())
			{
				std::shuffle(m_order.begin(), m_order.end(), m_random);
				m_next = 0;
			}
			const std::string& name = m_order[m_next++];
			if (auto file = OpenFile(m_folder + "/" + name))
			{
				m_source = std::move(file);
				m_current = name;
				return true;
			}
			ps5log::Line("[music] {}/{} cannot be played: an MP3, or a WAV of 16-bit PCM, is needed", m_folder, name);
		}
		return false;
	}

	void Player::Choose(const std::string& music)
	{
		m_music = music;
		m_source.reset();
		m_current.clear();
		m_chunkFrames = m_chunkAt = 0;
		m_between = 0;
		m_before = m_after = {};
		if (music.rfind("file:", 0) == 0)
		{
			const std::string name = music.substr(5);
			if (!name.empty() && name.find('/') == std::string::npos)
			{
				m_source = OpenFile(m_folder + "/" + name);
				if (m_source)
					m_current = name;
				else
					ps5log::Line("[music] {}/{} cannot be played (an MP3, or a WAV of 16-bit PCM, is needed): the launcher's own instead", m_folder, name);
			}
		}
		else if (music == "shuffle")
		{
			m_order = Files(m_folder);
			m_next = m_order.size(); // shuffled as the first is opened
			OpenNextShuffled();
		}
		// the launcher's own: chosen, or in place of a file that cannot be played
		if (!m_source && !music.empty() && music != "off" && m_setup)
			m_source = m_setup();
		if (m_source && !Frame(m_after))
			m_source.reset();
	}

	bool Player::Frame(std::array<int16_t, 2>& frame)
	{
		if (m_chunkAt == m_chunkFrames)
		{
			m_chunkAt = 0;
			m_chunkFrames = m_source->Read(m_chunk.data(), kChunk);
			if (m_chunkFrames == 0)
			{
				// its end: shuffle goes to its next file, anything else starts again
				const bool next = m_music == "shuffle" && !m_order.empty() ? OpenNextShuffled() : m_source->Rewind();
				if (next)
					m_chunkFrames = m_source->Read(m_chunk.data(), kChunk);
				if (m_chunkFrames == 0)
					return false;
			}
		}
		frame = {m_chunk[m_chunkAt * 2], m_chunk[m_chunkAt * 2 + 1]};
		m_chunkAt++;
		return true;
	}

	bool Player::Next(float& left, float& right)
	{
		left = right = 0;
		if (!m_source)
			return false;
		// between the source's two frames either side, in a straight line
		const float t = (float)m_between;
		left = m_before[0] + (m_after[0] - m_before[0]) * t;
		right = m_before[1] + (m_after[1] - m_before[1]) * t;
		for (m_between += (double)m_source->rate / kRate; m_between >= 1; m_between -= 1)
		{
			m_before = m_after;
			if (!Frame(m_after))
			{
				m_source.reset(); // it stopped reading: nothing more plays
				break;
			}
		}
		return true;
	}
}
