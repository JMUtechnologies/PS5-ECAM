// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_capture.h"
#include "app/capture_session.h"
#include <filesystem>
#include <fstream>
#include <iostream>
static int failures = 0;
static void Check(bool ok, const char* name) { std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok; }
int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	ps5capture::Frames frames(3);
	frames.Record(100);
	Check(frames.Samples().empty(), "disabled capture records nothing");
	frames.Begin(); frames.Record(1000); frames.Record(17667); frames.Record(34334);
	Check(frames.Samples().size() == 2 && frames.Samples()[1].elapsedUs == 33334 && frames.Samples()[1].intervalUs == 16667, "consecutive intervals use exact integer timestamps");
	frames.Discontinuity(); frames.Record(999999);
	Check(std::string(frames.Reason()) == "discontinuity", "capture records pause/rewind termination");
	Check(frames.Samples().size() == 2, "pause ends capture instead of inventing a long interval");
	frames.Begin(); frames.Record(1000); frames.Record(2000); frames.Record(3000); frames.Record(4000); frames.Record(5000);
	Check(frames.Samples().size() == 3 && frames.Full(), "fixed capacity bounds memory and stops capture");
	Check(std::string(frames.Reason()) == "capacity", "capacity termination is explicit");
	const std::string file = argv[1];
	const auto metadata = ps5capture::Session(frames, "ds", "test\"title", "test", "pinned-core", {{"jit_active", 1}});
	Check(ps5capture::WriteSession(file + ".session.json", metadata) && !ps5capture::WriteSession(file + ".session.json", metadata), "session metadata is written exclusively");
	Check(frames.WriteCsv(file), "capture exports CSV");
	std::ifstream in(file); const std::string text{std::istreambuf_iterator<char>(in), {}};
	Check(text == "elapsed_s,frame_ms\n0.001000,1.000\n0.002000,1.000\n0.003000,1.000\n", "CSV schema and unit conversion match comparison tool");
	Check(!frames.WriteCsv(file), "existing capture is never overwritten");
	frames.Begin(); frames.Record(1000); frames.Record(900); frames.Record(2000);
	Check(frames.Samples().empty() && std::string(frames.Reason()) == "clock", "nonmonotonic timestamps stop capture");
	frames.Begin(); frames.Record(0); frames.Record(1000);
	Check(frames.Samples().size() == 1 && frames.Samples()[0].intervalUs == 1000, "zero is a valid first timestamp");
	return failures ? 1 : 0;
}
