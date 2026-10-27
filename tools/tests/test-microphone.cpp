// SPDX-License-Identifier: GPL-3.0-or-later
#include "melonds/microphone.h"
#include "melonds/touch_calibration.h"
#include <limits>
#include <array>
#include <iostream>
namespace fs = std::filesystem;
using ps5melonds::microphone::Source;
int failures = 0;
void Check(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
std::string Wav(uint32_t rate = 47600)
{
	std::string s = "RIFF"; s.resize(44, 0); s.replace(8, 8, "WAVEfmt "); s.replace(36, 4, "data");
	auto put = [&](size_t at, uint32_t n, int count) { for (int i = 0; i < count; i++) s[at + i] = char(n >> (8 * i)); };
	put(4, 40, 4); put(16, 16, 4); put(20, 1, 2); put(22, 1, 2); put(24, rate, 4); put(28, rate * 2, 4); put(32, 2, 2); put(34, 16, 2); put(40, 4, 4);
	s += std::string("\x00\x40\x00\xC0", 4); return s;
}
void Put(const fs::path& file, const std::string& data) { std::ofstream(file, std::ios::binary) << data; }
int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	fs::create_directories(argv[1]); const auto root = fs::canonical(argv[1]); const fs::path file = root / "mic.wav";
	Source source; std::string error; std::array<int16_t, 8> out{};
	Check(source.Configure(0, "missing", error) && source.Read(out.data(), 8, true) == 8 && std::any_of(out.begin(), out.end(), [](auto s) { return s != 0; }), "default noise needs no WAV");
	source.Read(out.data(), 8, false); Check(std::all_of(out.begin(), out.end(), [](auto s) { return !s; }), "release sends silence");
	Check(source.Configure(1, "missing", error), "silence source needs no file"); source.Read(out.data(), 8, true);
	Check(std::all_of(out.begin(), out.end(), [](auto s) { return !s; }), "silence ignores held input");
	Put(file, Wav()); Check(source.Configure(2, file.string(), error), "bounded mono PCM WAV accepted");
	source.Read(out.data(), 3, true); Check(out[0] == 16384 && out[1] == -16384 && out[2] == 16384, "signed PCM loops at the core sample rate");
	source.Read(out.data(), 1, false); source.Read(out.data(), 1, true); Check(out[0] == 16384, "release restarts recorded input");
	Put(file, Wav(23800)); Check(source.Configure(2, file.string(), error), "lower rate WAV accepted"); source.Read(out.data(), 4, true);
	Check(out[0] == 16384 && out[1] == 16384 && out[2] == -16384 && out[3] == -16384, "sample-rate conversion keeps deterministic phase");
	for (const auto& bad : {std::string("bad"), Wav().substr(0, 47)}) { Put(file, bad); Check(!source.Configure(2, file.string(), error), "truncated file refused"); }
	auto bad = Wav(); bad[22] = 2; Put(file, bad); Check(!source.Configure(2, file.string(), error), "stereo refused");
	bad = Wav(); bad[20] = 3; Put(file, bad); Check(!source.Configure(2, file.string(), error), "float/compressed encoding refused");
	bad = Wav(); bad[40] = char(255); Put(file, bad); Check(!source.Configure(2, file.string(), error), "oversized chunk cannot read beyond buffer");
	bad = Wav(); bad[34] = 8; Put(file, bad); Check(!source.Configure(2, file.string(), error), "8-bit PCM refused");
	bad = Wav(); bad[28] = 0; Put(file, bad); Check(!source.Configure(2, file.string(), error), "inconsistent byte rate refused");
	Put(file, Wav(96000)); Check(!source.Configure(2, file.string(), error), "unsupported sample rate refused");
	Put(file, Wav()); fs::create_symlink(file, root / "linked.wav"); Check(!source.Configure(2, (root / "linked.wav").string(), error), "symlinked WAV refused");
	Check(source.Read(nullptr, 4, true) == 0 && source.Read(out.data(), -1, true) == 0, "invalid callback buffers refused");
	Check(!source.Configure(3, file.string(), error), "invalid source mode refused");
	using ps5melonds::input::Calibrate;
	Check(Calibrate(0, 0, 100) == 0 && Calibrate(0.5f, 0, 100) == 0.5f && Calibrate(1, 0, 100) == 1, "default calibration preserves edges and center");
	Check(std::abs(Calibrate(0.5f, 10, 100) - 0.6f) < 0.00001f, "offset moves visible cursor in display space");
	Check(Calibrate(0, 0, 50) == 0.25f && Calibrate(1, 0, 50) == 0.75f, "scale changes reach around center");
	Check(Calibrate(0, -20, 150) == 0 && Calibrate(1, 20, 150) == 1, "calibration clamps screen bounds");
	Check(Calibrate(std::numeric_limits<float>::quiet_NaN(), 0, 100) == 0.5f, "invalid coordinates cannot propagate NaN");
	return failures ? 1 : 0;
}
