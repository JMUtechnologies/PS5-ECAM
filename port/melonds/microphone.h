// SPDX-License-Identifier: GPL-3.0-or-later
// Optional recorded input; melonDS Platform.h requests signed mono PCM at 47.6 kHz.
#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace ps5melonds::microphone
{
	class Source
	{
	public:
		bool Configure(int mode, const std::string& file, std::string& error)
		{
			m_mode = 1; m_pcm.clear(); m_phase = 0; m_rate = 0;
			if (mode < 0 || mode > 2) { error = "Unsupported DS microphone source."; return false; }
			if (mode != 2) { m_mode = mode; error.clear(); return true; }
			try
			{
				namespace fs = std::filesystem;
				for (auto at = fs::absolute(file).lexically_normal(); !at.empty(); at = at.parent_path())
				{
					if (fs::is_symlink(fs::symlink_status(at))) { error = "Microphone WAV paths must not contain symbolic links."; return false; }
					if (at == at.root_path()) break;
				}
				const auto size = fs::file_size(file);
				if (size < 44 || size > (16u << 20)) { error = "Microphone WAV must be between 44 bytes and 16 MiB."; return false; }
				std::vector<unsigned char> bytes((size_t)size);
				std::ifstream input(file, std::ios::binary);
				if (!input.read((char*)bytes.data(), bytes.size()) || input.peek() != EOF) { error = "Microphone WAV could not be read."; return false; }
				auto u16 = [&](size_t at) { return unsigned(bytes[at]) | unsigned(bytes[at + 1]) << 8; };
				auto u32 = [&](size_t at) { return uint32_t(u16(at)) | uint32_t(u16(at + 2)) << 16; };
				auto tag = [&](size_t at, const char* value) { return std::equal(bytes.begin() + at, bytes.begin() + at + 4, value); };
				if (!tag(0, "RIFF") || !tag(8, "WAVE") || uint64_t(u32(4)) + 8 != bytes.size())
				{ error = "Microphone input must be a complete RIFF WAVE file."; return false; }
				bool format = false, data = false; size_t start = 0, length = 0;
				for (size_t at = 12; at < bytes.size();)
				{
					if (bytes.size() - at < 8) { error = "Truncated microphone WAV chunk."; return false; }
					const size_t count = u32(at + 4), body = at + 8;
					if (count > bytes.size() - body || count + (count & 1) > bytes.size() - body)
					{ error = "Truncated microphone WAV data."; return false; }
					if (tag(at, "fmt "))
					{
						if (format || count < 16 || u16(body) != 1 || u16(body + 2) != 1 || u16(body + 14) != 16 || u16(body + 12) != 2)
						{ error = "Microphone WAV must be uncompressed 16-bit mono PCM."; return false; }
						m_rate = u32(body + 4);
						if (m_rate < 8000 || m_rate > 48000 || u32(body + 8) != m_rate * 2)
						{ error = "Microphone WAV sample rate must be 8000 to 48000 Hz."; return false; }
						format = true;
					}
					else if (tag(at, "data"))
					{
						if (data || !count || count % 2) { error = "Microphone WAV needs one nonempty PCM data chunk."; return false; }
						data = true; start = body; length = count;
					}
					at = body + count + (count & 1);
				}
				if (!format || !data) { error = "Microphone WAV is missing its format or data."; return false; }
				m_pcm.reserve(length / 2);
				for (size_t at = start; at < start + length; at += 2) m_pcm.push_back((int16_t)u16(at));
				m_mode = mode; error.clear(); return true;
			}
			catch (const std::exception& failure) { error = failure.what(); return false; }
		}
		// Called by the emulation thread only, after Configure and before shutdown. No I/O/allocation.
		int Read(int16_t* output, int count, bool held)
		{
			if (!output || count <= 0) return 0;
			if (!held) m_phase = 0;
			for (int i = 0; i < count; i++)
			{
				if (!held || m_mode == 1) output[i] = 0;
				else if (m_mode == 0) output[i] = (int16_t)((int)(m_noise() % 0x6000) - 0x3000);
				else
				{
					output[i] = m_pcm[m_phase / 47600];
					m_phase = (m_phase + m_rate) % (uint64_t(m_pcm.size()) * 47600);
				}
			}
			return count;
		}
	private:
		int m_mode = 0;
		uint32_t m_rate = 0;
		uint64_t m_phase = 0;
		std::vector<int16_t> m_pcm;
		std::minstd_rand m_noise{0x5D5};
	};
	inline Source source;
}
