// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a libretro core's game on the console (game.h). As the DS side's (port/melonds/core.cpp):
//
//  - the emulation, and the picture on VideoOut (the DS side's screens, their one picture), on a thread
//    of its own: as many of the core's frames for each frame shown as its rate over VideoOut's (60.1
//    for the NES on a 59.94 Hz TV: one a frame, now and then two; 50 for a PAL game: now and then
//    none), VideoOut's flips pacing it. Every call into the core (session.h) is made from this thread.
//  - the sound on another: the core's, at its own rate, made 48 kHz and fed to AudioOut in 256-frame
//    grains, its buffer kept about half full by stretching it a little either way.
//  - the game's loop on the main thread: the DualSense as each player's RetroPad (the side's controls,
//    A and B where the menu puts them, the left stick as the D-pad too), player 1's as the special
//    controller a game has plugged in (devices.h: the paddle, a light gun, a mouse, a mat) and its motion
//    as a cartridge's tilt sensor, the port's shortcuts (ps5/pad.h), and what the in-game menu changes, as
//    the DS side's loop does.
#include "game.h"
#include "core.h"
#include "devices.h"
#include "session.h"
#include "../app/achievements.h"
#include "../app/boxart.h"
#include "../app/cheats.h"
#include "../app/game_archives.h"
#include "../app/ingame3ds.h"
#include "../app/lang.h"
#include "../app/saves.h"
#include "../app/shots.h"
#include "../azahar/controls.h"
#include "../melonds/screens.h"
#include "../ps5/display.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/notify.h"
#include "../ps5/pad.h"
#include "../retro/library.h"
#include "../retro/registry.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iterator>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <vector>

extern "C"
{
	int sceAudioOutInit(void);
	int sceAudioOutOpen(int userId, int type, int index, uint32_t length, uint32_t frequency, uint32_t format);
	int sceAudioOutClose(int handle);
	int sceAudioOutOutput(int handle, const void* samples);
	int sceAudioOutSetVolume(int handle, int flags, const int* volumes);
	void* scePthreadSelf(void);
	int scePthreadGetprio(void* thread, int* priority);
	int scePthreadSetprio(void* thread, int priority);
}

namespace ps5libretro
{
	namespace
	{
		namespace screens = ps5melonds::screens;
		using ps5azahar::Button;
		using ps5emu::PadInput;

		constexpr int kStateSlots = ps5ingame3ds::kStateSlots;
		constexpr int kResumeSlot = ps5saves::kResumeSlot;

		int s_side = -1;
		std::string s_key; // the side's ("snes")
		std::string s_name;
		uint64_t s_titleId = 0;
		bool s_gameOnly = false; // the game has settings of its own (the menu's Settings for this game only)
		ps5settings::N3ds s_settings; // the game's loop's: as the menu leaves them
		ps5settings::N3ds s_launched; // what the game started with, or what the menu last saved
		bool s_coreTouched = false;
		std::atomic<bool> s_cheevos{false}; // RetroAchievements: this game's set looked for (app/achievements.h)
		// its cheats (app/cheats.h), when its core takes them: the menu's list, from its .cht
		bool s_cheats = false;
		ps5cheats::Game s_cheatGame;

		// RetroAchievements' id for the side's console (rc_consoles.h); 0 for one it has no sets for
		uint32_t CheevosConsole(std::string_view key)
		{
			if (key == "nes") return 7;
			if (key == "fds") return 81;
			if (key == "snes" || key == "satellaview") return 3;
			if (key == "gb" || key == "sgb") return 4;
			if (key == "gbc") return 6;
			if (key == "gba") return 5;
			if (key == "n64" || key == "ique") return 2;
			if (key == "vb") return 28;
			if (key == "pokemini") return 24;
			return 0;
		}

		// The menu's Achievements, as the DS side shows them (melonds/core.cpp)
		void ShowAchievements()
		{
			std::vector<ps5cheevos::Item> items;
			std::string summary;
			ps5cheevos::List(items, summary);
			std::vector<ps5ingame3ds::Achievement> shown;
			for (const ps5cheevos::Item& item : items)
			{
				ps5ingame3ds::Achievement a{item.title, item.description, item.points, item.unlocked, {}};
				const std::time_t when = (std::time_t)item.unlockedAt;
				std::tm local{};
				if (item.unlocked && when > 0 && localtime_r(&when, &local))
					a.when = ps5lang::ShortDateTime(local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
				shown.push_back(std::move(a));
			}
			ps5ingame3ds::SetAchievements(true, summary, shown);
		}
		// The menu's Cheats from the game's file, and the codes of those that are on
		std::vector<std::string> ShowCheats()
		{
			std::vector<std::pair<std::string, bool>> listed;
			std::vector<std::string> codes;
			for (const ps5cheats::Cheat& cheat : ps5cheats::List(s_cheatGame))
			{
				listed.emplace_back(cheat.name, cheat.on);
				if (cheat.on)
					codes.push_back(cheat.code);
			}
			ps5ingame3ds::SetExtras({}, listed);
			return codes;
		}

		bool s_resume = false;
		std::thread s_emulation;
		std::atomic_bool s_stop = false;
		std::atomic_bool s_running = false;
		std::atomic<uint32_t> s_framesShown{0}, s_framesRun{0};

		// What the emulation reads before each frame; the game's loop changes it
		struct View
		{
			int filter = 0;
			int speedLimit = 100; // percent of the core's own rate; 0: none
			int size = 0, shape = 0; // the picture's on the TV (ps5ingame3ds::kPictureSizes, kPictureShapes)
		};
		// what the menu asked of the core, done between two frames on the emulation's thread: an option's new
		// value, a reset (under s_mutex)
		std::vector<std::pair<std::string, std::string>> s_optionChanges;
		bool s_resetPending = false;
		std::vector<std::string> s_cheatCodes; // the cheats on, when they changed (s_cheatsChanged)
		bool s_cheatsChanged = false;
		std::mutex s_mutex; // s_view, s_pads and the special controller's below
		View s_view;
		Pad s_pads[kPlayers];

		// The special controller plugged in (devices.h): the Controller row's choice, what it gives the core's
		// ports (done between two frames), and the first port with a controller other than a pad, as the core
		// has it after
		int s_choice = 0;
		std::string s_path; // the game's file, as the core has it (a SNES game's header for Auto)
		std::vector<std::pair<unsigned, unsigned>> s_portChanges;
		struct Special
		{
			int port = -1;
			unsigned device = 0; // libretro's
			Device kind = Device::Pad;
		};
		Special s_special;
		std::atomic<int> s_plugs{0}; // how many times the ports were given controllers (Plug)
		// the paddle's knob where a finger on the touchpad puts it, 0 to 1 (-1: no finger), and the mouse's
		// movement the touchpad made since the last frames, for the emulation to give the core
		float s_paddleTarget = -1;
		float s_mouseOwed[2] = {};

		// the game's loop's requests, for the emulation to make between two frames: a save state to slot
		// n (n), a load from it (-n), the last load undone (kUndoRequest)
		constexpr int kUndoRequest = -1000;
		std::atomic<int> s_stateRequest{0};
		std::atomic<bool> s_quitAfterState{false};
		// the emulation's answers, for the game's loop to give the menu (it is not the emulation's to)
		std::mutex s_answerMutex;
		std::string s_stateMessage;
		bool s_stateAnswered = false;
		bool s_undoAvailable = false;

		// -- the sound ---------------------------------------------------------------------------------

		constexpr int kUserSystem = 0xff;
		constexpr int kPortMain = 0;
		constexpr uint32_t kGrain = 256;
		constexpr uint32_t kRate = 48000;
		constexpr uint32_t kFormatStereoS16 = 1;
		constexpr int kVolumeFlagsLeftRight = 3;
		constexpr int kVolume0dB = 32768;
		constexpr int kAudioPriority = 384;
		constexpr size_t kTargetFill = 2400; // frames waiting for AudioOut: 50 ms

		int s_port = -1;
		std::thread s_soundThread;
		std::atomic_bool s_soundStop = false;
		std::mutex s_soundMutex;
		std::deque<int16_t> s_sound; // 48 kHz, left and right
		double s_position = 0;	 // where the next 48 kHz frame falls between the core's last two
		int16_t s_previous[2] = {};

		void FeedSound()
		{
			void* self = scePthreadSelf();
			int was = 0;
			scePthreadGetprio(self, &was);
			if (was <= 0 || was > kAudioPriority)
				scePthreadSetprio(self, kAudioPriority);
			std::vector<int16_t> grain(kGrain * 2);
			while (!s_soundStop)
			{
				{
					std::lock_guard lock(s_soundMutex);
					const size_t have = std::min(s_sound.size(), grain.size());
					std::copy_n(s_sound.begin(), have, grain.begin());
					std::fill(grain.begin() + have, grain.end(), 0);
					s_sound.erase(s_sound.begin(), s_sound.begin() + have);
				}
				if (sceAudioOutOutput(s_port, grain.data()) < 0)
					sceKernelUsleep(5000);
			}
		}

		void SetVolume(int percent)
		{
			if (s_port < 0)
				return;
			const int volume = kVolume0dB * std::clamp(percent, 0, 100) / 100;
			const int volumes[2] = {volume, volume};
			sceAudioOutSetVolume(s_port, kVolumeFlagsLeftRight, volumes);
		}

		void StartSound()
		{
			sceAudioOutInit(); // an error when the launcher has already: the port says
			s_port = sceAudioOutOpen(kUserSystem, kPortMain, 0, kGrain, kRate, kFormatStereoS16);
			if (s_port < 0)
			{
				ps5log::Line("[retro] AudioOut did not open ({:#x}): no sound", (uint32_t)s_port);
				return;
			}
			SetVolume(s_settings.volume);
			s_soundStop = false;
			s_soundThread = std::thread(FeedSound);
		}

		void StopSound()
		{
			s_soundStop = true;
			if (s_soundThread.joinable())
				s_soundThread.join();
			if (s_port >= 0)
			{
				sceAudioOutOutput(s_port, nullptr); // the last grain played
				sceAudioOutClose(s_port);
			}
			s_port = -1;
			std::lock_guard lock(s_soundMutex);
			s_sound.clear();
		}

		// The core's sound of one frame, made 48 kHz by its two nearest samples, a little faster or
		// slower as AudioOut's buffer is fuller or emptier than it should be
		void QueueSound(const std::vector<int16_t>& sound, double rate)
		{
			if (s_port < 0 || sound.size() < 2)
				return;
			size_t fill;
			{
				std::lock_guard lock(s_soundMutex);
				fill = s_sound.size() / 2;
			}
			const double skew = std::clamp(1.0 + (double(fill) - double(kTargetFill)) / double(kTargetFill) * 0.005, 0.995, 1.005);
			const double step = rate / kRate * skew; // the core's samples a 48 kHz frame moves on by
			std::vector<int16_t> out;
			out.reserve(size_t(sound.size() / step) + 4);
			const size_t frames = sound.size() / 2;
			for (; s_position < double(frames); s_position += step)
			{
				const size_t i = size_t(s_position);
				const double t = s_position - double(i);
				for (int c = 0; c < 2; c++)
				{
					const double a = i == 0 ? s_previous[c] : sound[(i - 1) * 2 + c];
					const double b = sound[i * 2 + c];
					out.push_back((int16_t)std::lround(a + (b - a) * t));
				}
			}
			s_position -= double(frames);
			s_previous[0] = sound[(frames - 1) * 2];
			s_previous[1] = sound[(frames - 1) * 2 + 1];
			std::lock_guard lock(s_soundMutex);
			// a backlog past a fifth of a second (the game held for the menu): dropped, not played late
			if (s_sound.size() / 2 > kRate / 5)
				s_sound.clear();
			s_sound.insert(s_sound.end(), out.begin(), out.end());
		}

		// -- the DualSense as the RetroPad ---------------------------------------------------------------

		// the RetroPad's buttons, and the handhelds' buttons they are as the side's controls map them
		constexpr std::pair<unsigned, Button> kButtons[] = {
			{RETRO_DEVICE_ID_JOYPAD_A, Button::A}, {RETRO_DEVICE_ID_JOYPAD_B, Button::B}, {RETRO_DEVICE_ID_JOYPAD_X, Button::X},
			{RETRO_DEVICE_ID_JOYPAD_Y, Button::Y}, {RETRO_DEVICE_ID_JOYPAD_L, Button::L}, {RETRO_DEVICE_ID_JOYPAD_R, Button::R},
			{RETRO_DEVICE_ID_JOYPAD_L2, Button::ZL}, {RETRO_DEVICE_ID_JOYPAD_R2, Button::ZR}, {RETRO_DEVICE_ID_JOYPAD_START, Button::Start},
			{RETRO_DEVICE_ID_JOYPAD_SELECT, Button::Select}, {RETRO_DEVICE_ID_JOYPAD_UP, Button::Up}, {RETRO_DEVICE_ID_JOYPAD_DOWN, Button::Down},
			{RETRO_DEVICE_ID_JOYPAD_LEFT, Button::Left}, {RETRO_DEVICE_ID_JOYPAD_RIGHT, Button::Right},
		};
		std::array<PadInput, std::size(kButtons)> s_mapping{};

		float Axis(uint8_t value) { return std::clamp((int(value) - 128) / 127.0f, -1.0f, 1.0f); }

		bool Held(PadInput input, const ps5pad::Data& data, uint32_t buttons)
		{
			constexpr float kStick = 0.5f;
			switch (input)
			{
			case PadInput::None: return false;
			case PadInput::Cross: return buttons & ps5pad::kCross;
			case PadInput::Circle: return buttons & ps5pad::kCircle;
			case PadInput::Square: return buttons & ps5pad::kSquare;
			case PadInput::Triangle: return buttons & ps5pad::kTriangle;
			case PadInput::L1: return buttons & ps5pad::kL1;
			case PadInput::R1: return buttons & ps5pad::kR1;
			case PadInput::L2: return buttons & ps5pad::kL2;
			case PadInput::R2: return buttons & ps5pad::kR2;
			case PadInput::L3: return buttons & ps5pad::kL3;
			case PadInput::R3: return buttons & ps5pad::kR3;
			case PadInput::Create: return buttons & ps5pad::kCreate;
			case PadInput::Options: return buttons & ps5pad::kOptions;
			case PadInput::Up: return buttons & ps5pad::kUp;
			case PadInput::Down: return buttons & ps5pad::kDown;
			case PadInput::Left: return buttons & ps5pad::kLeft;
			case PadInput::Right: return buttons & ps5pad::kRight;
			case PadInput::LeftStickUp: return Axis(data.leftY) < -kStick;
			case PadInput::LeftStickDown: return Axis(data.leftY) > kStick;
			case PadInput::LeftStickLeft: return Axis(data.leftX) < -kStick;
			case PadInput::LeftStickRight: return Axis(data.leftX) > kStick;
			case PadInput::RightStickUp: return Axis(data.rightY) < -kStick;
			case PadInput::RightStickDown: return Axis(data.rightY) > kStick;
			case PadInput::RightStickLeft: return Axis(data.rightX) < -kStick;
			case PadInput::RightStickRight: return Axis(data.rightX) > kStick;
			case PadInput::TouchPad: return buttons & ps5pad::kTouchPad;
			}
			return false;
		}

		void Configure(const ps5settings::N3ds& settings)
		{
			for (size_t i = 0; i < std::size(kButtons); i++)
				s_mapping[i] = ps5azahar::MappedInput(settings, kButtons[i].second);
		}

		// -- player 1's DualSense as the special controller, and its motion ------------------------------

		// The Arkanoid paddle's knob as Mesen counts it (ArkanoidController.h): 0x54 to 0xF4, a mouse's move a step
		constexpr float kKnobLeft = 0x54, kKnobRight = 0xf4;
		// the left stick all the way, or the D-pad: the knob's steps a frame (across in half a second), and a
		// mouse's; the right stick all the way: the aim across the picture in a second
		constexpr float kPaddleSpeed = 5.5f, kMouseSpeed = 6.0f, kAimSpeed = 1.0f;
		// the touchpad's width as the mouse's counts, and the turn (radians) that aims across the picture, and down it
		// (as the Wii U side's pointer, cemu/PS5PadController.cpp)
		constexpr float kTouchpadMouse = 320.0f, kAimAcross = 0.70f, kAimDown = 0.42f;

		float s_aim[2] = {0.5f, 0.5f}; // the light gun's, 0 to 1 across the picture
		bool s_fingerWas = false;
		float s_finger[2] = {};
		uint64_t s_motionAt = 0;
		bool s_centerWas = false;
		// the DualSense's gravity as the game started or was last levelled (the Buttons page), libretro's axes:
		// a tilt is the controller's from there
		float s_level[3] = {0, 0, 1};
		std::atomic<bool> s_levelPending{true};

		// The DualSense's axes (SDL's: x right, y up out of its face, z towards the player) as libretro's
		// sensors' for a handheld held in front of the player (x right, y to its screen's top, z out of it)
		void Handheld(const float (&in)[3], float (&out)[3])
		{
			out[0] = in[0];
			out[1] = -in[2];
			out[2] = in[1];
		}

		// v turned as the turn that takes from to (0, 0, 1) would (Rodrigues'), both of length 1
		void Level(const float (&from)[3], float (&v)[3])
		{
			const float axis[3] = {from[1], -from[0], 0}; // from x (0, 0, 1)
			const float sine = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1]), cosine = from[2];
			if (sine < 1e-4f)
				return;
			const float k[3] = {axis[0] / sine, axis[1] / sine, 0};
			const float cross[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
			const float dot = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
			for (int i = 0; i < 3; i++)
				v[i] = v[i] * cosine + cross[i] * sine + k[i] * dot * (1 - cosine);
		}

		// The motion sensors, for every player: as libretro's, levelled from how the DualSense was held
		void ReadMotion(const ps5pad::Data& data, Pad& pad)
		{
			float accel[3];
			Handheld(data.acceleration, accel);
			Handheld(data.angularVelocity, pad.gyro);
			const float length = std::sqrt(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
			if (s_levelPending.exchange(false) && length > 0.5f)
				for (int i = 0; i < 3; i++)
					s_level[i] = accel[i] / length;
			Level(s_level, accel);
			Level(s_level, pad.gyro);
			std::copy(std::begin(accel), std::end(accel), pad.accel);
		}

		// Player 1's DualSense as the special controller: the paddle, the light gun, the mouse (devices.h)
		void ReadSpecial(Device kind, const ps5pad::Data& data, uint32_t buttons, float deadzone, Pad& pad)
		{
			const bool finger = data.touchCount > 0;
			float fingerAt[2] = {};
			if (finger)
			{
				float width = 1, height = 1;
				ps5pad::TouchResolution(0, width, height);
				fingerAt[0] = std::clamp(data.touch[0].x / std::max(width, 1.0f), 0.0f, 1.0f);
				fingerAt[1] = std::clamp(data.touch[0].y / std::max(height, 1.0f), 0.0f, 1.0f);
			}
			const float seconds = s_motionAt && data.timestampUs > s_motionAt ? std::min((data.timestampUs - s_motionAt) / 1e6f, 0.1f) : 0.0f;
			s_motionAt = data.timestampUs;
			auto stick = [&](uint8_t value) {
				const float v = Axis(value);
				return std::fabs(v) < std::max(deadzone, 0.1f) ? 0.0f : v;
			};
			const float dpadX = (buttons & ps5pad::kRight ? 1.0f : 0.0f) - (buttons & ps5pad::kLeft ? 1.0f : 0.0f);
			auto press = [&](unsigned button, bool held) {
				if (held)
					pad.mouseButtons |= uint8_t(1u << button);
			};
			float paddle = -1, owed[2] = {};
			switch (kind)
			{
			case Device::Paddle:
				// a finger across the touchpad is the knob turned as far; the stick and the D-pad turn it
				if (finger)
					paddle = fingerAt[0];
				pad.mouse[0] = (int16_t)std::lround(std::clamp(stick(data.leftX) + dpadX, -1.0f, 1.0f) * kPaddleSpeed);
				press(RETRO_DEVICE_ID_MOUSE_LEFT, buttons & (ps5pad::kCross | ps5pad::kR2));
				break;
			case Device::Mouse:
				// a finger moved on the touchpad moves it as far, the left stick as long as it is pushed
				if (finger && s_fingerWas)
					for (int i = 0; i < 2; i++)
						owed[i] = (fingerAt[i] - s_finger[i]) * kTouchpadMouse;
				pad.mouse[0] = (int16_t)std::lround(stick(data.leftX) * kMouseSpeed);
				pad.mouse[1] = (int16_t)std::lround(stick(data.leftY) * kMouseSpeed);
				press(RETRO_DEVICE_ID_MOUSE_LEFT, buttons & ps5pad::kCross);
				press(RETRO_DEVICE_ID_MOUSE_RIGHT, buttons & ps5pad::kCircle);
				break;
			case Device::LightGun:
			{
				// a finger aims where it is, turning the DualSense aims from there (the Wii U side's pointer),
				// and the right stick; R3 aims at the middle
				const bool center = buttons & ps5pad::kR3;
				if (center && !s_centerWas)
					s_aim[0] = s_aim[1] = 0.5f;
				s_centerWas = center;
				if (finger)
					std::copy(std::begin(fingerAt), std::end(fingerAt), s_aim);
				else
				{
					s_aim[0] -= data.angularVelocity[1] * seconds / kAimAcross;
					s_aim[1] -= data.angularVelocity[0] * seconds / kAimDown;
				}
				s_aim[0] += stick(data.rightX) * kAimSpeed * seconds;
				s_aim[1] += stick(data.rightY) * kAimSpeed * seconds;
				for (float& a : s_aim)
					a = std::clamp(a, 0.0f, 1.0f);
				pad.aim[0] = (int16_t)std::lround(s_aim[0] * 65534.0f - 32767.0f);
				pad.aim[1] = (int16_t)std::lround(s_aim[1] * 65534.0f - 32767.0f);
				// the Zapper's trigger and its shot away from the screen; the Super Scope's fire, cursor and turbo
				press(RETRO_DEVICE_ID_MOUSE_LEFT, buttons & (ps5pad::kCross | ps5pad::kR2));
				press(RETRO_DEVICE_ID_MOUSE_RIGHT, buttons & (ps5pad::kCircle | ps5pad::kL2));
				press(RETRO_DEVICE_ID_MOUSE_MIDDLE, buttons & ps5pad::kSquare);
				pad.offscreen = pad.mouseButtons & (1u << RETRO_DEVICE_ID_MOUSE_RIGHT);
				break;
			}
			default:
				break;
			}
			s_fingerWas = finger;
			std::copy(std::begin(fingerAt), std::end(fingerAt), s_finger);
			std::lock_guard lock(s_mutex);
			s_paddleTarget = paddle;
			s_mouseOwed[0] += owed[0];
			s_mouseOwed[1] += owed[1];
		}

		// Each player's controller, a few hundred times a second; while the menu is up, let go
		void ReadPads(bool blocked)
		{
			Pad pads[kPlayers];
			const float deadzone = std::clamp(s_settings.deadzone, 0, 90) / 100.0f;
			Device special;
			{
				std::lock_guard lock(s_mutex);
				special = s_special.kind;
			}
			for (int player = 0; player < kPlayers; player++)
			{
				ps5pad::Data data{};
				if (!ps5pad::Read(player, data))
					continue;
				const uint32_t buttons = ps5pad::ApplyTurbo(ps5pad::FilterShortcuts(player, data.buttons).buttons);
				if (blocked || (data.buttons & ps5pad::kIntercepted))
					continue;
				Pad& pad = pads[player];
				for (size_t i = 0; i < std::size(kButtons); i++)
					if (Held(s_mapping[i], data, buttons))
						pad.buttons |= uint16_t(1u << kButtons[i].first);
				if (buttons & ps5pad::kL3)
					pad.buttons |= 1u << RETRO_DEVICE_ID_JOYPAD_L3;
				if (buttons & ps5pad::kR3)
					pad.buttons |= 1u << RETRO_DEVICE_ID_JOYPAD_R3;
				// the left stick as the D-pad too, past the deadzone, as the DS side's circle pad
				const float x = Axis(data.leftX), y = Axis(data.leftY);
				if (std::max(std::fabs(x), std::fabs(y)) > std::max(deadzone, 0.3f))
				{
					if (std::fabs(x) > std::fabs(y) * 0.5f)
						pad.buttons |= 1u << (x < 0 ? RETRO_DEVICE_ID_JOYPAD_LEFT : RETRO_DEVICE_ID_JOYPAD_RIGHT);
					if (std::fabs(y) > std::fabs(x) * 0.5f)
						pad.buttons |= 1u << (y < 0 ? RETRO_DEVICE_ID_JOYPAD_UP : RETRO_DEVICE_ID_JOYPAD_DOWN);
				}
				auto stick = [&](uint8_t value) {
					const float v = Axis(value);
					return int16_t(std::fabs(v) < deadzone ? 0 : std::lround(v * 32767.0f));
				};
				pad.left[0] = stick(data.leftX);
				pad.left[1] = stick(data.leftY);
				pad.right[0] = stick(data.rightX);
				pad.right[1] = stick(data.rightY);
				ReadMotion(data, pad);
				if (player == 0 && special != Device::Pad && special != Device::Mat)
					ReadSpecial(special, data, buttons, deadzone, pad);
			}
			std::lock_guard lock(s_mutex);
			std::copy(std::begin(pads), std::end(pads), s_pads);
		}

		// -- the menu ------------------------------------------------------------------------------------

		// The Buttons page's Controller row and what the DualSense does for the special controller plugged in
		// (ps5ingame3ds::Controllers), as the core has it now
		void PublishControllers()
		{
			const std::string_view core = ps5retro::Of(s_side).core;
			Special special;
			{
				std::lock_guard lock(s_mutex);
				special = s_special;
			}
			ps5ingame3ds::Controllers controllers;
			for (const Choice& choice : Choices(core))
				controllers.choices.push_back(choice.name);
			if (special.port >= 0)
				controllers.found = DeviceName(core, special.device);
			auto input = [](Button button) { return ps5azahar::Mapping(s_settings, button, true).input; };
			auto& rows = controllers.mapping;
			switch (special.kind)
			{
			case Device::Paddle:
				rows = {{"Turn the knob", "Touchpad, left stick or D-pad"}, {"Fire", "Cross or R2"}};
				break;
			case Device::LightGun:
				rows = {{"Aim", "Touchpad, motion or right stick"}, {"Fire", "Cross or R2"}, {"Aim at the middle", "R3"}};
				if (core == "mesens")
					rows.insert(rows.end(), {{"Cursor", "Circle or L2"}, {"Turbo", "Square"}, {"Pause", input(Button::Start)}});
				else
					rows.push_back({"Shoot away from the screen", "Circle or L2"});
				break;
			case Device::Mouse:
				rows = {{"Move", "Touchpad or left stick"}, {"Left button", "Cross"}, {"Right button", "Circle"}};
				break;
			case Device::Mat:
				// the Power Pad's twelve, as Mesen reads them from a pad's buttons (its libretro.cpp)
				if (controllers.found == "Power Pad" || controllers.found == "Family Trainer")
				{
					static constexpr Button kMat[] = {Button::B, Button::A, Button::Y, Button::X, Button::L, Button::R, Button::Left,
						Button::Right, Button::Up, Button::Down, Button::Select, Button::Start};
					for (size_t i = 0; i < std::size(kMat); i++)
						rows.push_back({"Mat " + std::to_string(i + 1), input(kMat[i])});
				}
				break;
			default:
				break;
			}
			controllers.motion = MotionUsed();
			if (controllers.motion)
				rows.push_back({"Tilt", "Tilt or turn the DualSense"});
			ps5ingame3ds::SetControllers(controllers);
		}

		ps5ingame3ds::Settings MenuSettings()
		{
			ps5ingame3ds::Settings menu;
			menu.volume = s_settings.volume;
			menu.performance = s_settings.performance;
			menu.deadzone = s_settings.deadzone;
			menu.aOnCircle = ps5azahar::MappedInput(s_settings, Button::A) == PadInput::Circle;
			menu.buttons = s_settings.buttons;
			menu.border = s_settings.border;
			menu.screenFilter = s_settings.dsFilter;
			menu.pictureSize = s_settings.pictureSize;
			menu.pictureShape = s_settings.pictureShape;
			menu.controller = s_choice;
			menu.gameOnly = s_gameOnly;
			std::lock_guard lock(s_mutex);
			menu.speedLimit = s_view.speedLimit;
			return menu;
		}

		// The picture's place on the TV: the whole height, in the core's shape
		screens::Rect PictureRect(float aspect, int size = 0, int shape = 0, unsigned pixelsWide = 0, unsigned pixelsHigh = 0)
		{
			const float width = (float)screens::Width(), height = (float)screens::Height();
			// its shape: the core's, a 4:3 TV's, each pixel square, a 16:9 TV's
			if (shape == 1)
				aspect = 4.0f / 3.0f;
			else if (shape == 2 && pixelsWide && pixelsHigh)
				aspect = (float)pixelsWide / pixelsHigh;
			else if (shape == 3)
				aspect = 16.0f / 9.0f;
			if (size == 2)
				return {0, 0, width, height}; // the whole screen
			float w = height * aspect, h = height;
			if (w > width)
			{
				w = width;
				h = width / aspect;
			}
			// whole pixels: the largest whole multiple of its height (on a 4K screen, a 240-line game 9 times)
			if (size == 1 && pixelsHigh)
			{
				const float times = std::max(1.0f, std::floor(h / pixelsHigh));
				h = times * pixelsHigh;
				w = h * aspect;
			}
			return {(width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2};
		}

		void PublishScreens(float aspect)
		{
			const screens::Rect r = PictureRect(aspect);
			ps5ingame3ds::SetScreens({{r.left, r.top, r.right, r.bottom}}, (float)screens::Width(), (float)screens::Height());
		}

		void ShowStateSlots()
		{
			std::vector<std::string> times(kStateSlots);
			for (int slot = 1; slot <= kStateSlots; slot++)
			{
				struct stat info{};
				if (stat(StatePath(slot).c_str(), &info) != 0)
					continue;
				const std::time_t time = info.st_mtime;
				char text[32] = "saved";
				if (const std::tm* local = std::localtime(&time))
					std::strftime(text, sizeof(text), "%b %d, %H:%M", local);
				times[slot - 1] = text;
			}
			ps5ingame3ds::SetStateSlots(times);
		}

		// What the menu changed, while the game runs; kept for the next games in the side's settings, or
		// the game's own when it has some
		void ApplyMenu(const ps5ingame3ds::Settings& menu)
		{
			// the menu's Buttons, the A and B row's swap already in them (app/buttons_menu.h)
			s_settings.buttons = menu.buttons;
			const bool aOnCircle = ps5azahar::MappedInput(s_settings, Button::A) == PadInput::Circle;
			s_settings.volume = menu.volume;
			s_settings.performance = menu.performance;
			s_settings.deadzone = menu.deadzone;
			s_settings.dsFilter = menu.screenFilter;
			s_settings.pictureSize = menu.pictureSize;
			s_settings.pictureShape = menu.pictureShape;
			if (menu.controller != s_choice)
			{
				// another controller plugged in, between two frames; kept for this game only
				s_choice = s_settings.retroController = menu.controller;
				std::lock_guard lock(s_mutex);
				s_portChanges = Ports(ps5retro::Of(s_side).core, s_choice, s_path);
			}
			if (menu.border != s_settings.border)
			{
				s_settings.border = menu.border;
				ps5ingame3ds::LoadBorder(menu.border);
			}
			if (menu.aOnCircle != aOnCircle)
			{
				ps5azahar::SetMapping(s_settings, (size_t)Button::A, menu.aOnCircle ? PadInput::Circle : PadInput::Cross, true);
				ps5azahar::SetMapping(s_settings, (size_t)Button::B, menu.aOnCircle ? PadInput::Cross : PadInput::Circle, true);
				ps5azahar::SetMapping(s_settings, (size_t)Button::X, menu.aOnCircle ? PadInput::Triangle : PadInput::Square, true);
				ps5azahar::SetMapping(s_settings, (size_t)Button::Y, menu.aOnCircle ? PadInput::Square : PadInput::Triangle, true);
			}
			Configure(s_settings);
			{
				std::lock_guard lock(s_mutex);
				s_view.filter = menu.screenFilter;
				s_view.speedLimit = menu.speedLimit;
				s_view.size = menu.pictureSize;
				s_view.shape = menu.pictureShape;
			}
			SetVolume(menu.volume);
			ps5settings::Launcher before, after;
			before.retro[ps5retro::Slot(s_side)] = s_launched;
			after.retro[ps5retro::Slot(s_side)] = s_settings;
			if (menu.gameOnly != s_gameOnly)
			{
				s_gameOnly = menu.gameOnly;
				ps5settings::SetGameOwn(s_key, s_titleId, s_gameOnly, after);
			}
			ps5settings::SaveChanges(s_key, s_titleId, before, after);
			s_launched = s_settings;
		}

		// -- the emulation -------------------------------------------------------------------------------

		// The Controller row's choice given to the core's ports, then which special controller it has: on the
		// emulation's thread, or before it starts
		void Plug(const std::vector<std::pair<unsigned, unsigned>>& ports)
		{
			const std::string_view core = ps5retro::Of(s_side).core;
			for (const auto& [port, device] : ports)
				SetDevice(port, device);
			Special special;
			for (unsigned port = 0; port < kPorts && special.port < 0; port++)
				if (const unsigned device = PortDevice(port); DeviceOf(core, device) != Device::Pad)
					special = {(int)port, device, DeviceOf(core, device)};
			if (special.port >= 0)
				ps5log::Line("[retro] port {}: {} (0x{:x})", special.port, DeviceName(core, special.device), special.device);
			{
				std::lock_guard lock(s_mutex);
				s_special = special;
			}
			s_plugs++;
		}

		// The knob where Mesen has it, as the frames before moved it (a state loaded, it is found again at
		// either end), and the mouse's movement still owed to the core
		float s_knob = (kKnobRight - kKnobLeft) / 2;

		// Player 1's special controller for one frame: the paddle's knob to where a finger puts it, the
		// touchpad's mouse movement, and a special controller in another port (the Power Pad, the Super
		// Scope's pause) pressed by player 1's buttons too
		void SpecialFrame(Pad (&pads)[kPlayers], const Special& special, float target, int (&mouse)[2])
		{
			if (special.port > 0 && special.port < kPlayers)
				pads[special.port].buttons |= pads[0].buttons;
			if (special.kind == Device::Paddle)
			{
				float move = pads[0].mouse[0];
				if (target >= 0)
				{
					move = kKnobLeft + target * (kKnobRight - kKnobLeft) - s_knob;
					// at either end, past it: Mesen stops the knob there, and the two agree again
					if (target < 0.02f || target > 0.98f)
						move += target < 0.5f ? -kKnobRight : kKnobRight;
				}
				const int steps = (int)std::lround(std::clamp(move, -255.0f, 255.0f));
				s_knob = std::clamp(s_knob + steps, kKnobLeft, kKnobRight);
				pads[0].mouse[0] = (int16_t)steps;
			}
			else if (special.kind == Device::Mouse)
				for (int i = 0; i < 2; i++)
				{
					pads[0].mouse[i] = (int16_t)std::clamp(pads[0].mouse[i] + mouse[i], -127, 127);
					mouse[i] = 0; // the touchpad's, once
				}
		}

		void Answer(const std::string& message)
		{
			std::lock_guard lock(s_answerMutex);
			s_stateMessage = message;
			s_stateAnswered = true;
			s_undoAvailable = UndoAvailable();
		}

		// A save state saved or loaded, between two frames, and what came of it in the menu, in the DS
		// side's words; why one did not work, in the boot log
		void MakeState(int request)
		{
			std::string error;
			const int slot = std::abs(request);
			bool done;
			std::string message;
			if (request == kUndoRequest)
			{
				done = UndoLoad(error);
				message = done ? ps5lang::Tr("Load undone") : ps5lang::Tr("It did not work");
			}
			else if (request > 0)
			{
				done = SaveState(slot, error);
				if (done)
				{
					const Picture& picture = LastPicture();
					ps5shots::SaveStatePicture(StatePath(slot), picture.pixels.data(), picture.width, picture.height, picture.width);
				}
				message = done ? ps5lang::TrF("Saved slot {0}", slot) : std::string(ps5lang::Tr("It did not work"));
			}
			else
			{
				done = LoadState(slot, error);
				message = !done ? std::string(ps5lang::Tr("It did not work")) :
					slot == kResumeSlot ? std::string(ps5lang::Tr("Back where you left off")) : ps5lang::TrF("Loaded slot {0}", slot);
			}
			if (done && request <= 0 && s_cheevos)
				ps5cheevos::Reset(); // the set's conditions start again from the state's memory
			if (!done)
				ps5log::Line("[retro] save state {}: {}", request, error);
			if (request == kResumeSlot)
			{
				// the quit's own: the menu is gone, the game goes
				if (s_quitAfterState)
					s_stop = true;
				return;
			}
			if (request != -kResumeSlot || done)
				Answer(message);
		}

		void Emulate()
		{
			const double refresh = ps5display::OutputRefresh() ? ps5display::OutputRefresh() / 1000.0 : 59.94;
			double owed = 0; // the core's frames due, at its rate over VideoOut's
			std::vector<uint32_t> held; // the picture shown while the menu is up
			while (!s_stop)
			{
				if (const int request = s_stateRequest.exchange(0))
					MakeState(request);
				{
					// the menu's emulator options and Reset, between two frames
					std::vector<std::pair<std::string, std::string>> changes;
					std::vector<std::pair<unsigned, unsigned>> ports;
					std::vector<std::string> cheats;
					bool reset = false, cheatsChanged = false;
					{
						std::lock_guard lock(s_mutex);
						changes.swap(s_optionChanges);
						ports.swap(s_portChanges);
						reset = std::exchange(s_resetPending, false);
						cheatsChanged = std::exchange(s_cheatsChanged, false);
						cheats.swap(s_cheatCodes);
					}
					if (!ports.empty())
						Plug(ports);
					if (cheatsChanged)
						SetCheats(cheats);
					for (const auto& [key, value] : changes)
						if (!SetOption(key, value))
							ps5log::Line("[retro] the core has no {} value {}", key, value);
					if (reset)
					{
						ps5libretro::Reset();
						if (s_cheevos)
							ps5cheevos::Reset();
						ps5log::Line("[retro] reset");
					}
				}
				View view;
				Pad pads[kPlayers];
				Special special;
				float paddle;
				int mouse[2];
				{
					std::lock_guard lock(s_mutex);
					view = s_view;
					std::copy(std::begin(s_pads), std::end(s_pads), pads);
					special = s_special;
					paddle = s_paddleTarget;
					for (int i = 0; i < 2; i++)
					{
						// whole counts; the rest stays for the next frames
						mouse[i] = (int)s_mouseOwed[i];
						s_mouseOwed[i] -= (float)mouse[i];
					}
				}
				const bool paused = ps5ingame3ds::MenuOpen();
				if (paused && s_cheevos)
					ps5cheevos::Idle(); // what the server answered is still dealt with
				if (!paused)
				{
					const double rate = FrameRate();
					owed += view.speedLimit > 0 ? rate / refresh * view.speedLimit / 100.0 : 4.0;
					const int frames = std::min(8, (int)owed);
					owed -= frames;
					for (int i = 0; i < frames && !s_stop; i++)
					{
						Pad frame[kPlayers];
						std::copy(std::begin(pads), std::end(pads), frame);
						SpecialFrame(frame, special, paddle, mouse);
						Run(frame);
						s_framesRun++;
						if (s_cheevos)
							ps5cheevos::DoFrame();
						// sound at the core's own speed only: faster or slower, it would be played as such
						if (view.speedLimit == 100)
							QueueSound(Sound(), SampleRate());
					}
					for (int player = 0; player < kPlayers; player++)
					{
						uint16_t strong, weak;
						Rumble(player, strong, weak);
						ps5pad::SetVibration(player, uint8_t(strong >> 8), uint8_t(weak >> 8));
					}
				}
				const Picture& picture = LastPicture();
				screens::Frame frame;
				frame.filter = view.filter;
				if (picture.width && picture.height && picture.width <= screens::kPictureMax && picture.height <= screens::kPictureMax)
				{
					frame.picture = picture.pixels.data();
					frame.pictureWidth = picture.width;
					frame.pictureHeight = picture.height;
					frame.pictureRect = PictureRect(AspectRatio(), view.size, view.shape, picture.width, picture.height);
					// a light gun's crosshair, where it aims
					if (special.kind == Device::LightGun && !paused)
					{
						frame.cursor = frame.cursorOnPicture = true;
						frame.cursorX = (pads[0].aim[0] + 32767) / 65534.0f;
						frame.cursorY = (pads[0].aim[1] + 32767) / 65534.0f;
					}
				}
				if (!screens::Present(frame))
				{
					ps5log::Line("[retro] the picture stopped: the GPU's device was lost");
					// tr: a notification as a game on a libretro core stops because the PS5's GPU stopped drawing it
					ps5notify::Send(ps5lang::Tr("The game's graphics stopped working."));
					break;
				}
				s_framesShown++;
			}
			for (int player = 0; player < kPlayers; player++)
				ps5pad::SetVibration(player, 0, 0);
			s_running = false;
		}

		// Whether a file's name has one of the core's types (its valid_extensions: "sfc|smc|...")
		bool CoreReads(const retro_system_info& info, const std::string& name)
		{
			std::string extension = std::filesystem::path(name).extension().string(), list = info.valid_extensions ? info.valid_extensions : "";
			for (std::string* text : {&extension, &list})
				for (char& c : *text)
					c = (char)std::tolower((unsigned char)c);
			return extension.size() > 1 && ("|" + list + "|").find("|" + extension.substr(1) + "|") != std::string::npos;
		}

		// A game in a .zip or .7z (the NES's and the Game Boy Advance's libraries list them): its first file
		// of the core's types, a Mac's copies of them left out, unpacked once into folder (the side's
		// cache), from where the core reads it as any game file. A core that reads the archive itself has it.
		bool Unpack(const Core& core, const std::string& archivePath, const std::string& folder, std::string& unpacked, std::string& error)
		{
			retro_system_info info{};
			core.getSystemInfo(&info);
			unpacked = archivePath;
			if (CoreReads(info, archivePath))
				return true;
			namespace fs = std::filesystem;
			std::unique_ptr<archive, int (*)(archive*)> reader(archive_read_new(), archive_read_free);
			archive_read_support_filter_all(reader.get());
			archive_read_support_format_all(reader.get());
			archive_entry* entry = nullptr;
			if (archive_read_open_filename(reader.get(), archivePath.c_str(), 1 << 16) == ARCHIVE_OK)
				while (archive_read_next_header(reader.get(), &entry) == ARCHIVE_OK)
				{
					const char* name = archive_entry_pathname(entry);
					const int64_t size = archive_entry_size(entry);
					if (!name || archive_entry_filetype(entry) != AE_IFREG || std::strstr(name, "__MACOSX/") ||
						fs::path(name).filename().string().rfind("._", 0) == 0 || !CoreReads(info, name) || size <= 0 || size > (int64_t)512 << 20)
						continue;
					std::error_code ec;
					fs::create_directories(folder, ec);
					const std::string target = folder + "/" + fs::path(name).filename().string();
					// a copy of its size there already is this game's, from its last start
					if (const uintmax_t had = fs::file_size(target, ec); !ec && had == (uintmax_t)size)
					{
						unpacked = target;
						return true;
					}
					std::vector<char> data((size_t)size);
					size_t done = 0;
					for (la_ssize_t got = 1; done < data.size() && got > 0; done += got > 0 ? (size_t)got : 0)
						got = archive_read_data(reader.get(), data.data() + done, data.size() - done);
					std::ofstream out(target + ".tmp", std::ios::binary | std::ios::trunc);
					out.write(data.data(), (std::streamsize)data.size());
					out.close();
					if (done == data.size() && out)
						fs::rename(target + ".tmp", target, ec);
					if (done != data.size() || !out || ec)
						break;
					ps5log::Line("[retro] {} unpacked from {} ({} KiB)", fs::path(target).filename().string(), archivePath, data.size() >> 10);
					unpacked = target;
					return true;
				}
			// tr: why a game in a .zip or .7z archive did not start: no file in it the system's emulator reads
			error = ps5lang::Tr("There is no game for this system in this archive, or the archive could not be read.");
			return false;
		}
	}

	namespace
	{
		// the launcher's language, as libretro numbers them
		unsigned RetroLanguage()
		{
			const std::string language = ps5lang::Current();
			return language == "ja" ? RETRO_LANGUAGE_JAPANESE : language.rfind("fr", 0) == 0 ? RETRO_LANGUAGE_FRENCH :
				language.rfind("es", 0) == 0 ? RETRO_LANGUAGE_SPANISH : language == "de" ? RETRO_LANGUAGE_GERMAN :
				language == "it" ? RETRO_LANGUAGE_ITALIAN : language == "nl" ? RETRO_LANGUAGE_DUTCH :
				language == "pt-BR" ? RETRO_LANGUAGE_PORTUGUESE_BRAZIL : language == "pt-PT" ? RETRO_LANGUAGE_PORTUGUESE_PORTUGAL :
				language == "ru" ? RETRO_LANGUAGE_RUSSIAN : language == "ko" ? RETRO_LANGUAGE_KOREAN :
				language == "zh-Hant" ? RETRO_LANGUAGE_CHINESE_TRADITIONAL : language == "zh-Hans" ? RETRO_LANGUAGE_CHINESE_SIMPLIFIED :
				language == "pl" ? RETRO_LANGUAGE_POLISH : language == "tr" ? RETRO_LANGUAGE_TURKISH : RETRO_LANGUAGE_ENGLISH;
		}
	}

	const std::vector<CoreOption>& CoreOptions(int side)
	{
		// each side's, as last asked, and in which language
		static std::map<int, std::pair<std::string, std::vector<CoreOption>>> asked;
		static const std::vector<CoreOption> none;
		const Core* core = Plays(side) ? Find(ps5retro::Of(side).core) : nullptr;
		if (!core || Loaded())
			return none;
		auto& [language, options] = asked[side];
		if (language == ps5lang::Current() && !options.empty())
			return options;
		language = ps5lang::Current();
		options.clear();
		for (Option& option : Describe(*core, RetroLanguage()))
			options.push_back({std::move(option.key), std::move(option.label), std::move(option.info), std::move(option.category),
				std::move(option.values), std::move(option.defaultValue)});
		ps5log::Line("[retro] {}: {} options", core->name, options.size());
		return options;
	}

	bool CoreTouched() { return s_coreTouched; }
	void SetResume(bool resume) { s_resume = resume; }

	bool LaunchGame(int side, const ps5emu::Game& game, const ps5settings::N3ds& settings, std::string& error)
	{
		s_coreTouched = false;
		const Core* core = Plays(side) ? Find(ps5retro::Of(side).core) : nullptr;
		if (!core)
		{
			error = ps5lang::Tr("This system's emulator is not in this copy of Playtendoh.");
			return false;
		}
		const ps5retro::System& system = ps5retro::Of(side);
		s_side = side;
		s_key = system.key;
		s_name = game.name;
		s_titleId = game.titleId;
		s_settings = s_launched = settings;
		s_gameOnly = ps5settings::HasOwn(ps5settings::Load(), s_key, s_titleId);
		{
			std::lock_guard lock(s_mutex);
			s_view = View{settings.dsFilter, 100, settings.pictureSize, settings.pictureShape};
			std::fill(std::begin(s_pads), std::end(s_pads), Pad{});
		}
		Configure(settings);
		Setup setup;
		const std::string data = system.data;
		setup.system = data + "/system";
		setup.saves = data + "/saves";
		setup.states = data + "/states";
		// what the side always gives its core ("key=value;key=value": the Super Game Boy's model, ps5retro::kSystems)
		for (std::string_view rest = system.options; !rest.empty();)
		{
			const size_t end = rest.find(';');
			const std::string_view pair = rest.substr(0, end);
			if (const size_t equals = pair.find('='); equals != std::string_view::npos)
				setup.options[std::string(pair.substr(0, equals))] = std::string(pair.substr(equals + 1));
			rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
		}
		// the launcher's language, as libretro numbers them, for the cores that have their own words
		setup.language = RetroLanguage();
		// the app's values for some cores' options, over their defaults: the Virtual Boy's second D-pad on
		// the right stick (Red Alarm, Teleroboxer); the Nintendo 64 on the CPU as it is built
		// (tools/libretro/parallel-n64.mk): the cached interpreter, no dynarec being in it, and Angrylion's
		// renderer on six threads, the emulation keeping a core to itself
		const std::string coreName = system.core;
		if (coreName == "beetlevb")
			setup.options["vb_right_analog_to_digital"] = "enabled";
		else if (coreName == "mesen")
		{
			// Mesen's HD packs (texture packs): each in the side's system/HdPacks/<the game file's name>/, its
			// hires.txt there; the folder made so a player sees where they go
			std::error_code made;
			std::filesystem::create_directories(setup.system + "/HdPacks", made);
			setup.options["mesen_hdpacks"] = "enabled";
		}
		else if (coreName == "paralleln64")
			setup.options = {{"parallel-n64-cpucore", "cached_interpreter"}, {"parallel-n64-gfxplugin", "angrylion"},
				{"parallel-n64-angrylion-multithread", "6"}};
		// and the player's, from Settings > Emulator (ps5settings::N3ds::coreOptions), over both
		for (const auto& [key, value] : settings.coreOptions)
			setup.options[key] = value;
		SetLog([](int, const std::string& line) { ps5log::Line("[retro] {}", line); });
		// a game in an archive: its file unpacked into the side's cache, from where the core reads it
		std::string path = game.path.string();
		if (ps5archive::IsArchive(path) && !Unpack(*core, path, data + "/cache", path, error))
		{
			ps5log::Line("[retro] {} did not load: {}", game.name, error);
			return false;
		}
		if (!Load(*core, path, setup, error))
		{
			ps5log::Line("[retro] {} did not load: {}", game.name, error);
			return false;
		}
		// the controllers: the game's choice (Auto: what the core or devices.h finds for it), player 1's motion
		// levelled as it is held when the game starts
		s_path = path;
		s_choice = settings.retroController;
		s_special = {};
		s_paddleTarget = -1;
		s_mouseOwed[0] = s_mouseOwed[1] = 0;
		s_knob = (kKnobRight - kKnobLeft) / 2;
		s_aim[0] = s_aim[1] = 0.5f;
		s_fingerWas = s_centerWas = false;
		s_motionAt = 0;
		s_levelPending = true;
		s_portChanges.clear();
		Plug(Ports(system.core, s_choice, path));
		// the screens on VideoOut: from here, a failure needs a fresh process to show the launcher
		s_coreTouched = true;
		if (!screens::Start(error, "retro"))
		{
			Unload();
			return false;
		}
		StartSound();
		ps5ingame3ds::Start(s_name, s_titleId, MenuSettings(), ps5ingame3ds::Console::Retro, system.name, system.colour);
		// the menu's top: the core's name, and the box art the launcher shows (the folder's own picture, else
		// libretro's thumbnail by the game file's name)
		{
			std::string cover = ps5retro::CoverPath(side, game);
			if (cover.empty())
				cover = ps5boxart::RetroPath(ps5retro::Thumbnails(side), game.path.stem().string());
			ps5ingame3ds::SetGame(CoreName(), cover);
		}
		// RetroAchievements, signed in, for a console it has sets for: the game's file hashed as rcheevos
		// hashes that console's, its memory as rcheevos addresses it
		s_cheevos = false;
		if (const uint32_t console = CheevosConsole(system.key); console && ps5cheevos::Get().signedIn)
		{
			std::ifstream file(path, std::ios::binary);
			std::vector<uint8_t> data;
			if (file)
				data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			const std::string hash = data.size() <= (128u << 20) ? ps5cheevos::Hash(console, data.data(), data.size()) : std::string();
			if (!hash.empty() && CheevosMemory(console))
				s_cheevos = ps5cheevos::StartGame(CheevosRead, hash);
			else if (!hash.empty())
				ps5log::Line("[retro] RetroAchievements: {} gives no memory to check", CoreName());
		}
		ps5ingame3ds::SetAchievements(s_cheevos, {}, {});
		// its cheats, those on given to the core before its first frame (and kept by a state it resumes from)
		s_cheatGame = {side, game.titleId, game.path.string()};
		s_cheats = TakesCheats() && ps5cheats::KindOf(side) == ps5cheats::Kind::Retro;
		ps5ingame3ds::SetCheatsShown(s_cheats);
		if (s_cheats)
		{
			const std::vector<std::string> on = ShowCheats();
			SetCheats(on);
			if (!on.empty())
				ps5log::Line("[retro] {} cheats on", on.size());
		}
		{
			// the Emulator category: every option the core shows, as it is now
			std::vector<ps5ingame3ds::CoreOption> options;
			for (const Option& option : Options())
			{
				if (!option.visible || option.values.empty())
					continue;
				ps5ingame3ds::CoreOption row{option.key, option.label, option.info, {}, 0};
				for (size_t i = 0; i < option.values.size(); i++)
				{
					row.values.push_back(option.values[i].second);
					if (option.values[i].first == option.value)
						row.index = (int)i;
				}
				options.push_back(std::move(row));
			}
			ps5ingame3ds::SetCoreOptions(options);
		}
		ps5ingame3ds::LoadBorder(s_settings.border);
		PublishControllers();
		PublishScreens(AspectRatio());
		ShowStateSlots();
		ps5ingame3ds::SetUndoAvailable(false);
		s_stop = false;
		s_running = true;
		s_framesShown = s_framesRun = 0;
		s_emulation = std::thread(Emulate);
		ps5log::Line("[retro] {}: {} on {}", system.key, game.name, CoreName());
		return true;
	}

	void RunGame()
	{
		ps5notify::Send(ps5lang::Tr("Touchpad click + Options opens the in-game menu"));
		ps5pad::SetStateShortcuts(true);
		s_quitAfterState = false;
		if (std::exchange(s_resume, false))
			s_stateRequest = -kResumeSlot;
		uint64_t polls = 0, quitAskedAt = 0;
		std::pair<int, bool> shownControllers{s_plugs.load(), MotionUsed()};
		uint32_t lastRun = 0, lastShown = 0;
		uint64_t lastSecond = sceKernelGetProcessTime();
		while (s_running && !s_stop)
		{
			sceKernelUsleep(4000);
			ReadPads(ps5ingame3ds::MenuOpen());
			if (++polls % 500 == 0)
				ps5pad::Rescan();
			if (polls % 250 == 0)
			{
				const uint64_t now = sceKernelGetProcessTime();
				const double seconds = std::max(1e-3, (now - lastSecond) / 1e6);
				const uint32_t run = s_framesRun, shown = s_framesShown;
				const double fps = (shown - lastShown) / seconds;
				const double speed = (run - lastRun) / seconds / FrameRate() * 100.0;
				lastSecond = now;
				lastRun = run;
				lastShown = shown;
				ps5ingame3ds::SetPerformance(fps, speed, s_key + ": " + CoreName());
				if (polls % 2500 == 0)
					ps5log::Line("[perfretro] {:.0f} fps, speed {:.0f}%", fps, speed);
			}
			{
				std::lock_guard lock(s_answerMutex);
				if (std::exchange(s_stateAnswered, false))
				{
					ps5ingame3ds::SetStateMessage(s_stateMessage);
					ps5ingame3ds::SetUndoAvailable(s_undoAvailable);
					ShowStateSlots();
				}
			}
			// the menu's emulator options: given to the core between frames, kept for the side as Settings > Emulator keeps them
			for (std::string key; ;)
			{
				int index = 0;
				if (!ps5ingame3ds::TakeCoreOption(key, index))
					break;
				for (const Option& option : Options())
					if (option.key == key && index >= 0 && index < (int)option.values.size())
					{
						const std::string& value = option.values[index].first;
						{
							std::lock_guard lock(s_mutex);
							s_optionChanges.emplace_back(key, value);
						}
						if (value == option.defaultValue)
							s_settings.coreOptions.erase(key);
						else
							s_settings.coreOptions[key] = value;
						ps5settings::Launcher before, after;
						before.retro[ps5retro::Slot(s_side)] = s_launched;
						after.retro[ps5retro::Slot(s_side)] = s_settings;
						ps5settings::SaveChanges(s_key, s_titleId, before, after);
						s_launched = s_settings;
					}
			}
			// the special controller as the core has it after a change, and a cartridge's motion sensor once it is read
			if (polls % 125 == 0)
			{
				const std::pair<int, bool> now{s_plugs.load(), MotionUsed()};
				if (std::exchange(shownControllers, now) != now)
					PublishControllers();
			}
			if (ps5ingame3ds::TakeLevelRequest())
				s_levelPending = true;
			// a cheat turned on or off: kept in the game's .cht, given to the core between two frames
			if (ps5ingame3ds::ExtrasRequest extras; ps5ingame3ds::TakeExtrasRequest(extras) && extras.kind == ps5ingame3ds::ExtrasRequest::Cheat && s_cheats)
			{
				const std::vector<ps5cheats::Cheat> listed = ps5cheats::List(s_cheatGame);
				std::string error;
				if (extras.index >= 0 && extras.index < (int)listed.size())
				{
					const ps5cheats::Cheat& cheat = listed[extras.index];
					if (ps5cheats::SetOn(s_cheatGame, (size_t)extras.index, !cheat.on, error))
						// tr: a cheat ({0}) turned on or off
						ps5ingame3ds::SetExtrasMessage(!cheat.on ? ps5lang::TrF("{0}: on", cheat.name) : ps5lang::TrF("{0}: off", cheat.name));
					else
						ps5log::Line("[retro] cheat {} not changed: {}", cheat.name, error);
				}
				std::vector<std::string> codes = ShowCheats();
				std::lock_guard lock(s_mutex);
				s_cheatCodes = std::move(codes);
				s_cheatsChanged = true;
			}
			if (ps5ingame3ds::TakeResetRequest())
			{
				std::lock_guard lock(s_mutex);
				s_resetPending = true;
			}
			// RetroAchievements in the menu: read again about once a second while it is open
			if (s_cheevos && polls % 250 == 1 && ps5ingame3ds::MenuOpen())
				ShowAchievements();
			const ps5pad::Shortcut shortcut = ps5pad::TakeShortcut();
			if (shortcut == ps5pad::Shortcut::Menu)
				ps5ingame3ds::ToggleMenu();
			else if (shortcut == ps5pad::Shortcut::SaveState || shortcut == ps5pad::Shortcut::LoadState)
			{
				std::string why;
				if (!ps5ingame3ds::AskState(shortcut == ps5pad::Shortcut::LoadState, why))
					ps5notify::Send(why);
			}
			ps5ingame3ds::Settings menu;
			if (ps5ingame3ds::TakeChanges(menu))
				ApplyMenu(menu);
			bool load = false;
			int slot = 0;
			if (ps5ingame3ds::TakeStateRequest(load, slot))
				s_stateRequest = load ? -slot : slot;
			else if (ps5ingame3ds::TakeUndoRequest())
				s_stateRequest = kUndoRequest;
			if (quitAskedAt && sceKernelGetProcessTime() - quitAskedAt > 8000000 && !s_stop)
			{
				ps5log::Line("[retro] no state for the quit after 8 s: quitting without it");
				s_stop = true;
			}
			if (ps5ingame3ds::TakeLibraryRequest())
			{
				// a state to go on from first (Resume where you left off), made between two frames, then the quit
				ps5log::Line("[retro] back to the library");
				if (!s_quitAfterState)
				{
					s_quitAfterState = true;
					quitAskedAt = sceKernelGetProcessTime();
					s_stateRequest = kResumeSlot;
				}
				else
					s_stop = true;
			}
		}
		s_stop = true;
		if (s_emulation.joinable())
			s_emulation.join();
		std::string message;
		while (TakeMessage(message))
			ps5log::Line("[retro] the core said: {}", message);
		if (s_cheevos.exchange(false))
			ps5cheevos::EndGame(2000); // an unlock still on its way, a moment to go
		Unload(); // the save written, the core stopped
		StopSound();
		screens::Stop();
		ps5pad::SetStateShortcuts(false);
		ps5log::Line("[retro] {} ended", s_name);
	}
}
