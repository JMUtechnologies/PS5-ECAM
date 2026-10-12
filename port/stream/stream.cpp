// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the remote touch screen's server (stream.h), from dolphin78's prototype (#38).
#include "stream.h"
#include "browser.h"
#include "jpeg.h"
#include "websocket.h"
#include "../ps5/log.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ps5stream
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr int kFirstPort = 8080;
		constexpr auto kFrameGap = std::chrono::milliseconds(33); // 30 pictures a second at most
		constexpr auto kTouchLife = std::chrono::seconds(1);	  // a browser that stops sending lets go
		constexpr size_t kClients = 12;							  // viewers and their touch sockets

		std::mutex s_configure; // one Configure at a time (the launcher's, a menu's)
		std::atomic<bool> s_enabled{false};	 // the setting
		std::atomic<bool> s_running{false}, s_demand{false}; // the server
		std::atomic<int> s_viewers{0};
		std::atomic<int64_t> s_lastCapture{0}; // Clock's ticks
		std::atomic<uint64_t> s_nextOwner{0};
		std::thread s_server, s_encoder;

		// what the menus show: under s_stateMutex
		std::mutex s_stateMutex;
		State::Kind s_kind = State::Kind::Off;
		int s_port = 0;
		std::string s_address;
		Clock::time_point s_addressChecked{};

		// the newest picture to encode, and the newest JPEG: under s_frameMutex
		std::mutex s_frameMutex;
		std::condition_variable s_frameChanged;
		struct RawFrame
		{
			std::vector<uint8_t> pixels;
			int width, height;
		};
		std::shared_ptr<const RawFrame> s_raw;
		std::shared_ptr<const std::vector<uint8_t>> s_jpeg;
		uint64_t s_serial = 0;

		// the browser's touch: under s_touchMutex
		std::mutex s_touchMutex;
		struct Pointer
		{
			bool down = false;
			float x = 0, y = 0;
		};
		Pointer s_pointer;
		std::deque<Pointer> s_edges; // presses and releases not read yet, so a quick tap is not lost
		uint64_t s_touchOwner = 0;	 // the /input socket that touches: the newest
		Clock::time_point s_lastTouch{};

		bool Alive()
		{
			return s_running.load();
		}

		// The PS5's IPv4 address on a network (not loopback); empty when it has none
		std::string LocalAddress()
		{
			std::string host;
			ifaddrs* interfaces = nullptr;
			if (getifaddrs(&interfaces) != 0)
				return host;
			for (ifaddrs* i = interfaces; i; i = i->ifa_next)
			{
				if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET)
					continue;
				const auto* address = reinterpret_cast<const sockaddr_in*>(i->ifa_addr);
				const uint32_t ip = ntohl(address->sin_addr.s_addr);
				if ((ip >> 24) == 127 || ip == 0)
					continue;
				char text[INET_ADDRSTRLEN];
				if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)))
				{
					host = text;
					break;
				}
			}
			freeifaddrs(interfaces);
			return host;
		}

		bool Reply(int fd, int code, const char* type, const std::vector<uint8_t>& body)
		{
			const std::string header = "HTTP/1.1 " + std::to_string(code) +
				" Response\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Type: " + type +
				"\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
			return SendBytes(fd, header.data(), header.size(), Alive) && SendBytes(fd, body.data(), body.size(), Alive);
		}

		void EncodeFrames()
		{
			while (s_running)
			{
				std::shared_ptr<const RawFrame> source;
				{
					std::unique_lock lock(s_frameMutex);
					s_frameChanged.wait(lock, [] { return !s_running || s_raw; });
					if (!s_running)
						break;
					source = std::move(s_raw);
				}
				std::vector<uint8_t> encoded = EncodeJpeg(source->pixels.data(), source->width, source->height);
				if (encoded.empty())
					continue;
				auto frame = std::make_shared<const std::vector<uint8_t>>(std::move(encoded));
				{
					std::lock_guard lock(s_frameMutex);
					s_jpeg = std::move(frame);
					++s_serial;
				}
				s_frameChanged.notify_all();
			}
		}

		// "1,0.25,0.5" from /input, or "1&x=0.25&y=0.5" from /touch?down=; false when it is neither
		bool UpdateTouch(const std::string& values, const char* format, uint64_t owner)
		{
			int pressed = 0, used = 0;
			float x = 0, y = 0;
			if (std::sscanf(values.c_str(), format, &pressed, &x, &y, &used) != 3 || used != (int)values.size() ||
				(pressed != 0 && pressed != 1) || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
				return false;
			std::lock_guard lock(s_touchMutex);
			if (owner && owner != s_touchOwner)
				return true; // an older socket, since replaced: ignored
			const Pointer next{pressed != 0, x, y};
			// presses and releases are kept for the game to read in turn; only movement is merged
			if (next.down != s_pointer.down)
			{
				if (s_edges.size() >= 32)
					s_edges.clear();
				s_edges.push_back(next);
			}
			s_pointer = next;
			s_lastTouch = Clock::now();
			return true;
		}

		void InputStream(WebSocket& socket)
		{
			const uint64_t owner = ++s_nextOwner;
			{
				std::lock_guard lock(s_touchMutex);
				s_touchOwner = owner;
				s_pointer.down = false;
				s_edges.clear();
			}
			while (s_running)
			{
				std::string message;
				const WebSocket::Result result = socket.Receive(message, 100);
				if (result == WebSocket::Result::Idle)
					continue;
				if (result != WebSocket::Result::Text || !UpdateTouch(message, "%d,%f,%f%n", owner))
					break;
			}
			std::lock_guard lock(s_touchMutex);
			if (s_touchOwner == owner)
			{
				s_pointer.down = false;
				s_edges.clear();
				s_touchOwner = 0;
			}
		}

		void VideoStream(WebSocket& socket)
		{
			if (s_viewers++ == 0)
				ps5log::Line("[stream] a browser is showing the screen");
			bool ready = false;
			uint64_t sentSerial = 0;
			Clock::time_point lastReady = Clock::now();
			while (s_running)
			{
				std::string message;
				const WebSocket::Result result = socket.Receive(message, ready ? 1 : 50);
				if (result == WebSocket::Result::Text && message == "ready")
				{
					ready = true;
					lastReady = Clock::now();
				}
				else if (result != WebSocket::Result::Idle)
					break;
				if (!ready)
				{
					if (Clock::now() - lastReady > std::chrono::seconds(5))
						break; // the browser stopped asking
					continue;
				}
				std::shared_ptr<const std::vector<uint8_t>> frame;
				uint64_t serial;
				{
					std::unique_lock lock(s_frameMutex);
					s_frameChanged.wait_for(lock, std::chrono::milliseconds(10), [&] { return !s_running || s_serial != sentSerial; });
					serial = s_serial;
					if (serial != sentSerial)
						frame = s_jpeg;
				}
				if (!frame)
					continue;
				// one picture in flight: a slow browser gets the newest when it is ready, not a queue
				if (!socket.Send(2, frame->data(), frame->size()))
					break;
				sentSerial = serial;
				ready = false;
				lastReady = Clock::now();
			}
			if (--s_viewers == 0)
				ps5log::Line("[stream] no browser is showing the screen");
		}

		void HandleClient(int fd)
		{
			ConfigureSocket(fd);
			std::string request;
			const auto deadline = Clock::now() + std::chrono::milliseconds(500);
			while (s_running && Clock::now() < deadline && request.size() < 4096 && request.find("\r\n\r\n") == std::string::npos)
			{
				pollfd wait{fd, POLLIN, 0};
				if (poll(&wait, 1, 20) <= 0)
					continue;
				char buffer[1024];
				const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
				if (n == 0)
					return;
				if (n < 0)
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
						continue;
					return;
				}
				request.append(buffer, (size_t)n);
			}
			if (request.find("\r\n\r\n") == std::string::npos)
				return;
			const size_t end = request.find(" HTTP/1.1\r\n");
			const std::string path = end != std::string::npos && request.rfind("GET ", 0) == 0 ? request.substr(4, end - 4) : "";
			if (path == "/")
			{
				const std::string page = kBrowser;
				Reply(fd, 200, "text/html; charset=utf-8", {page.begin(), page.end()});
			}
			else if (path == "/input" || path == "/video")
			{
				WebSocket socket(fd, Alive);
				if (!socket.Upgrade(request))
				{
					Reply(fd, 400, "text/plain", {});
					return;
				}
				if (path == "/input")
					InputStream(socket);
				else
					VideoStream(socket);
			}
			else if (path == "/frame")
			{
				// one picture, for a browser or a tool without WebSockets: the next one is asked for
				std::shared_ptr<const std::vector<uint8_t>> frame;
				{
					std::lock_guard lock(s_frameMutex);
					frame = s_jpeg;
				}
				s_demand = true;
				Reply(fd, frame ? 200 : 204, "image/jpeg", frame ? *frame : std::vector<uint8_t>{});
			}
			else if (path.rfind("/touch?down=", 0) == 0)
				Reply(fd, UpdateTouch(path.substr(12), "%d&x=%f&y=%f%n", 0) ? 204 : 400, "text/plain", {});
			else
				Reply(fd, 404, "text/plain", {});
		}

		void Serve(int listener)
		{
			struct Client
			{
				std::thread thread;
				std::shared_ptr<std::atomic<bool>> done;
			};
			std::vector<Client> clients;
			while (s_running)
			{
				for (auto i = clients.begin(); i != clients.end();)
				{
					if (*i->done)
					{
						i->thread.join();
						i = clients.erase(i);
					}
					else
						++i;
				}
				pollfd wait{listener, POLLIN, 0};
				if (poll(&wait, 1, 100) <= 0)
					continue;
				const int fd = accept(listener, nullptr, nullptr);
				if (fd < 0)
					continue;
				if (clients.size() >= kClients)
				{
					close(fd);
					continue;
				}
				auto done = std::make_shared<std::atomic<bool>>(false);
				clients.push_back({std::thread([fd, done] {
					HandleClient(fd);
					close(fd);
					*done = true;
				}),
					done});
			}
			close(listener);
			for (Client& client : clients)
				client.thread.join();
		}

		void Stop()
		{
			if (!s_running.exchange(false))
				return;
			s_frameChanged.notify_all();
			if (s_server.joinable())
				s_server.join();
			if (s_encoder.joinable())
				s_encoder.join();
		}

		void Reset(State::Kind kind)
		{
			{
				std::lock_guard lock(s_touchMutex);
				s_pointer = {};
				s_edges.clear();
				s_touchOwner = 0;
			}
			{
				std::lock_guard lock(s_frameMutex);
				s_raw.reset();
				s_jpeg.reset();
				s_serial = 0;
			}
			s_demand = false;
			std::lock_guard lock(s_stateMutex);
			s_kind = kind;
			s_port = 0;
			s_address.clear();
		}

		struct Cleanup
		{
			~Cleanup() { Stop(); }
		} s_cleanup;
	}

	void Configure(bool enabled)
	{
		std::lock_guard configuring(s_configure);
		if (enabled == s_enabled.load())
			return;
		s_enabled = enabled;
		Stop();
		Reset(State::Kind::Off);
		if (!enabled)
		{
			ps5log::Line("[stream] the remote touch screen is off");
			return;
		}
		// the first free port from 8080
		const int listener = socket(AF_INET, SOCK_STREAM, 0);
		int port = kFirstPort;
		bool bound = false;
		if (listener >= 0)
		{
			int yes = 1;
			setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_ANY);
			for (; port <= 65535; port++)
			{
				address.sin_port = htons((uint16_t)port);
				if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0)
				{
					bound = true;
					break;
				}
				if (errno != EADDRINUSE)
					break;
			}
		}
		if (!bound || listen(listener, 8) < 0)
		{
			ps5log::Line("[stream] no port could be opened for the remote touch screen (errno {})", errno);
			if (listener >= 0)
				close(listener);
			std::lock_guard lock(s_stateMutex);
			s_kind = State::Kind::NoPort;
			return;
		}
		const std::string host = LocalAddress();
		{
			std::lock_guard lock(s_stateMutex);
			s_port = port;
			s_address = host.empty() ? "" : "http://" + host + ":" + std::to_string(port) + "/";
			s_kind = host.empty() ? State::Kind::NoNetwork : State::Kind::Waiting;
			s_addressChecked = Clock::now();
		}
		ps5log::Line("[stream] the remote touch screen is on, port {}{}", port, host.empty() ? ", no network address yet" : ", at http://" + host + ":" + std::to_string(port) + "/");
		s_running = true;
		s_encoder = std::thread(EncodeFrames);
		s_server = std::thread(Serve, listener);
	}

	bool Enabled()
	{
		return s_enabled.load();
	}

	State Current()
	{
		std::lock_guard lock(s_stateMutex);
		// no network at start: looked for again, every 2 seconds while a menu asks
		if (s_kind == State::Kind::NoNetwork && Clock::now() - s_addressChecked > std::chrono::seconds(2))
		{
			s_addressChecked = Clock::now();
			const std::string host = LocalAddress();
			if (!host.empty())
			{
				s_address = "http://" + host + ":" + std::to_string(s_port) + "/";
				s_kind = State::Kind::Waiting;
				ps5log::Line("[stream] the remote touch screen is at {}", s_address);
			}
		}
		State state;
		state.kind = s_kind;
		state.address = s_address;
		if (state.kind == State::Kind::Waiting && s_viewers > 0)
			state.kind = State::Kind::Showing;
		return state;
	}

	const char* Describe(State::Kind kind)
	{
		switch (kind)
		{
		case State::Kind::Off: return "Off";
		case State::Kind::Waiting: return "Waiting for a browser";
		case State::Kind::Showing: return "A browser is showing it";
		case State::Kind::NoNetwork: return "No network: connect the PS5 to Wi-Fi or a cable";
		case State::Kind::NoPort: return "No free port for the server";
		}
		return "";
	}

	bool WantFrame()
	{
		if (!s_running.load(std::memory_order_relaxed) || (!s_viewers.load(std::memory_order_relaxed) && !s_demand.load(std::memory_order_relaxed)))
			return false;
		const int64_t now = Clock::now().time_since_epoch().count();
		int64_t last = s_lastCapture.load();
		if (now - last < std::chrono::duration_cast<Clock::duration>(kFrameGap).count() || !s_lastCapture.compare_exchange_strong(last, now))
			return false;
		s_demand = false;
		return true;
	}

	void SubmitFrame(const uint8_t* rgb, int width, int height)
	{
		if (!s_running || !rgb || width <= 0 || height <= 0 || width > 4096 || height > 4096)
			return;
		// copied here, outside the locks: the touches never wait for a picture
		auto frame = std::make_shared<const RawFrame>(RawFrame{std::vector<uint8_t>(rgb, rgb + (size_t)width * height * 3), width, height});
		{
			std::lock_guard lock(s_frameMutex);
			s_raw = std::move(frame);
		}
		s_frameChanged.notify_all();
	}

	bool Touch(float& x, float& y)
	{
		if (!s_running.load(std::memory_order_relaxed))
			return false;
		std::lock_guard lock(s_touchMutex);
		if (Clock::now() - s_lastTouch >= kTouchLife)
		{
			s_pointer.down = false;
			s_edges.clear();
		}
		Pointer state = s_pointer;
		if (!s_edges.empty())
		{
			state = s_edges.front();
			s_edges.pop_front();
		}
		x = state.x;
		y = state.y;
		return state.down;
	}
}

// What the emulators' patches call, by these names (Cemu's 0033, Azahar's 0013): they declare them
// rather than include the port's headers, as their other hooks do.
bool PS5Cemu_StreamWantFrame()
{
	return ps5stream::WantFrame();
}

void PS5Cemu_StreamFrame(const uint8_t* rgb, int width, int height)
{
	ps5stream::SubmitFrame(rgb, width, height);
}

bool AzaharPS5_StreamWantFrame()
{
	return ps5stream::WantFrame();
}

void AzaharPS5_StreamFrame(const uint8_t* rgb, int width, int height)
{
	ps5stream::SubmitFrame(rgb, width, height);
}
