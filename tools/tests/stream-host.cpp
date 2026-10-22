// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a host harness for the remote touch screen's server (port/stream), driven by
// test-stream.py. The JPEGs are stand-ins (a frame header with the picture's size), so it needs no
// TurboJPEG; the log goes to stderr. With STREAM_UPLOADS set, the files page sends to that folder
// (id "games") and to a folder that cannot be made (id "nowhere"), for test-upload.py.
#include "stream.h"
#include "jpeg.h"

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ps5log
{
	void Write(std::string_view line)
	{
		std::cerr << line << std::endl;
	}
}

namespace ps5stream
{
	std::vector<uint8_t> EncodeJpeg(const uint8_t* rgb, int width, int height)
	{
		if (!rgb || width <= 0 || height <= 0)
			return {};
		std::vector<uint8_t> jpeg = {0xff, 0xd8, 0xff, 0xc0, 0x00, 0x11, 0x08, uint8_t(height >> 8), uint8_t(height), uint8_t(width >> 8), uint8_t(width), 3,
			1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1};
		jpeg.resize(jpeg.size() + 20000, rgb[0]); // a picture's worth of bytes
		jpeg.push_back(0xff);
		jpeg.push_back(0xd9);
		return jpeg;
	}
}

int main()
{
	const int width = 854, height = 480;
	std::vector<uint8_t> rgb((size_t)width * height * 3, 128);
	if (const char* uploads = std::getenv("STREAM_UPLOADS"))
	{
		const std::string folder = uploads;
		ps5stream::SetUploadFolders([folder] {
			return std::vector<ps5stream::Folder>{{"games", "Games", "Any file.", folder}, {"nowhere", "Nowhere", "", "/proc/none"}};
		});
	}
	ps5stream::Configure(true);
	std::cout << ps5stream::Current().address << std::endl;
	std::atomic<bool> alive{true};
	std::thread render([&] {
		while (alive)
		{
			if (ps5stream::WantFrame())
				ps5stream::SubmitFrame(rgb.data(), width, height);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	});
	std::string command;
	while (std::getline(std::cin, command))
	{
		if (command == "touch")
		{
			float x = 0, y = 0;
			const bool down = ps5stream::Touch(x, y);
			std::cout << down << " " << x << " " << y << std::endl;
		}
		else if (command == "state")
			std::cout << ps5stream::Describe(ps5stream::Current().kind) << std::endl;
		else if (command == "disable")
		{
			ps5stream::Configure(false);
			std::cout << ps5stream::Describe(ps5stream::Current().kind) << std::endl;
		}
		else if (command == "enable")
		{
			ps5stream::Configure(true);
			std::cout << ps5stream::Current().address << std::endl;
		}
		else
			break;
	}
	alive = false;
	render.join();
	ps5stream::Configure(false);
}
