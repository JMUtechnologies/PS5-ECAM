// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded, single-producer frame-interval capture. No disk writes or allocations in Record().
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ps5capture
{
	class Frames
	{
	public:
		struct Sample { uint64_t elapsedUs, intervalUs; };
		explicit Frames(size_t capacity = 12000) : m_capacity(capacity) {}
		void Begin() { m_samples.clear(); m_samples.reserve(m_capacity); m_first = m_previous = 0; m_hasPrevious = false; m_active = true; m_complete = false; }
		void Stop() { m_active = false; }
		// Stop at the first pause/rewind rather than inventing frames across a discontinuity.
		void Discontinuity() { m_active = false; }
		void Record(uint64_t nowUs)
		{
			if (!m_active) return;
			if (!m_hasPrevious) { m_first = m_previous = nowUs; m_hasPrevious = true; return; }
			if (nowUs <= m_previous) { m_active = false; return; }
			if (m_samples.size() == m_capacity) { m_complete = true; m_active = false; return; }
			m_samples.push_back({nowUs - m_first, nowUs - m_previous}); m_previous = nowUs;
		}
		const std::vector<Sample>& Samples() const { return m_samples; }
		bool Full() const { return m_complete; }
		bool WriteCsv(const std::string& file) const
		{
			FILE* out = std::fopen(file.c_str(), "wx");
			if (!out) return false;
			bool ok = std::fprintf(out, "elapsed_s,frame_ms\n") > 0;
			for (const auto& sample : m_samples)
				ok = std::fprintf(out, "%llu.%06llu,%llu.%03llu\n", (unsigned long long)(sample.elapsedUs / 1000000),
					(unsigned long long)(sample.elapsedUs % 1000000), (unsigned long long)(sample.intervalUs / 1000),
					(unsigned long long)(sample.intervalUs % 1000)) > 0 && ok;
			return std::fclose(out) == 0 && ok;
		}
	private:
		std::vector<Sample> m_samples;
		size_t m_capacity = 0;
		uint64_t m_first = 0, m_previous = 0;
		bool m_active = false, m_complete = false;
		bool m_hasPrevious = false;
	};
}
