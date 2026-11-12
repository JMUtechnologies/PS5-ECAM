// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the special controllers' choices (devices.h), by the device numbers Mesen's and Mesen-S's
// libretro.cpp give them
#include "devices.h"

#include <libretro.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ps5libretro
{
	namespace
	{
		constexpr unsigned Sub(unsigned base, unsigned id) { return RETRO_DEVICE_SUBCLASS(base, id); }
		// Mesen's (Libretro/libretro.cpp's DEVICE_*), Mesen-S's the same where they share one
		constexpr unsigned kAuto = RETRO_DEVICE_JOYPAD, kGamepad = Sub(RETRO_DEVICE_JOYPAD, 0), kPowerPad = Sub(RETRO_DEVICE_JOYPAD, 1),
						   kFamilyTrainer = Sub(RETRO_DEVICE_JOYPAD, 2), kPartyTap = Sub(RETRO_DEVICE_JOYPAD, 3),
						   kPachinko = Sub(RETRO_DEVICE_JOYPAD, 4), kExcitingBoxing = Sub(RETRO_DEVICE_JOYPAD, 5),
						   kKonamiHyperShot = Sub(RETRO_DEVICE_JOYPAD, 6), kZapper = Sub(RETRO_DEVICE_POINTER, 0),
						   kOekaKids = Sub(RETRO_DEVICE_POINTER, 1), kBandaiHyperShot = Sub(RETRO_DEVICE_POINTER, 2),
						   kArkanoid = Sub(RETRO_DEVICE_MOUSE, 0), kHoriTrack = Sub(RETRO_DEVICE_MOUSE, 1), kSnesMouse = Sub(RETRO_DEVICE_MOUSE, 2);
		// Mesen-S's: the Super Scope in the second port, the mouse in either
		constexpr unsigned kSuperScope = Sub(RETRO_DEVICE_POINTER, 0);

		// the in-game Buttons page's Controller row (its words translated where it shows them, ingame3ds.cpp)
		const std::vector<Choice> kMesen = {
			{"Auto", {{0, kAuto}, {1, kAuto}, {4, kAuto}}},
			{"Standard", {{0, kGamepad}, {1, kGamepad}, {4, RETRO_DEVICE_NONE}}},
			{"Arkanoid paddle", {{0, kGamepad}, {1, kArkanoid}, {4, RETRO_DEVICE_NONE}}},
			{"Zapper", {{0, kGamepad}, {1, kZapper}, {4, RETRO_DEVICE_NONE}}},
			{"Power Pad", {{0, kGamepad}, {1, kPowerPad}, {4, RETRO_DEVICE_NONE}}},
		};
		const std::vector<Choice> kMesenS = {
			{"Auto", {}},
			{"Standard", {{0, kGamepad}, {1, kGamepad}}},
			{"SNES Mouse", {{0, kSnesMouse}, {1, kGamepad}}},
			{"Super Scope", {{0, kGamepad}, {1, kSuperScope}}},
		};
		const std::vector<Choice> kNone;

		// letters and digits only, in capitals: "Mario & Wario (Japan)" is MARIOWARIOJAPAN
		std::string Folded(std::string_view text)
		{
			std::string out;
			for (const char c : text)
				if (std::isalnum((unsigned char)c))
					out += (char)std::toupper((unsigned char)c);
			return out;
		}
	}

	const std::vector<Choice>& Choices(std::string_view core)
	{
		return core == "mesen" ? kMesen : core == "mesens" ? kMesenS : kNone;
	}

	std::vector<std::pair<unsigned, unsigned>> Ports(std::string_view core, int choice, const std::string& path)
	{
		const std::vector<Choice>& choices = Choices(core);
		if (choices.empty())
			return {};
		choice = std::clamp(choice, 0, (int)choices.size() - 1);
		if (choice == 0 && core == "mesens")
		{
			// the header's title, wherever it is in the first 64 KiB and a copier's 512 bytes
			std::vector<uint8_t> rom;
			if (std::ifstream file(path, std::ios::binary); file)
			{
				rom.resize(0x10200);
				file.read((char*)rom.data(), (std::streamsize)rom.size());
				rom.resize((size_t)file.gcount());
				std::error_code error;
				const uintmax_t size = std::filesystem::file_size(path, error);
				if (!error && size % 1024 == 512 && rom.size() > 512)
					rom.erase(rom.begin(), rom.begin() + 512);
			}
			const Device device = SnesDevice(SnesTitle(rom), std::filesystem::path(path).filename().string());
			choice = device == Device::Mouse ? 2 : device == Device::LightGun ? 3 : 1;
		}
		return choices[(size_t)choice].ports;
	}

	Device DeviceOf(std::string_view core, unsigned device)
	{
		const unsigned base = device & RETRO_DEVICE_MASK;
		if (base == RETRO_DEVICE_MOUSE)
			return core == "mesen" && device == kArkanoid ? Device::Paddle : Device::Mouse;
		if (base == RETRO_DEVICE_POINTER || base == RETRO_DEVICE_LIGHTGUN)
			return Device::LightGun;
		if (core == "mesen" && device >= kPowerPad && device <= kKonamiHyperShot && base == RETRO_DEVICE_JOYPAD)
			return Device::Mat;
		return Device::Pad;
	}

	const char* DeviceName(std::string_view core, unsigned device)
	{
		if (core == "mesens")
			return device == kSnesMouse ? "SNES Mouse" : device == kSuperScope ? "Super Scope" : "";
		if (core != "mesen")
			return "";
		switch (device)
		{
		case kArkanoid: return "Arkanoid paddle";
		case kHoriTrack: return "Hori Track";
		case kSnesMouse: return "SNES Mouse";
		case kZapper: return "Zapper";
		case kOekaKids: return "Oeka Kids";
		case kBandaiHyperShot:
		case kKonamiHyperShot: return "Hyper Shot";
		case kPowerPad: return "Power Pad";
		case kFamilyTrainer: return "Family Trainer";
		case kPartyTap: return "Partytap";
		case kPachinko: return "Pachinko";
		case kExcitingBoxing: return "Exciting Boxing";
		default: return "";
		}
	}

	std::string SnesTitle(const std::vector<uint8_t>& rom)
	{
		// LoROM's header, then HiROM's: the one whose checksum and its complement add up, else the first
		// whose title is all printable
		std::string printable;
		for (const size_t at : {size_t(0x7fc0), size_t(0xffc0)})
		{
			if (rom.size() < at + 0x20)
				continue;
			const uint8_t* h = rom.data() + at;
			std::string title(h, h + 21);
			const bool readable = std::all_of(title.begin(), title.end(), [](char c) { return c >= 0x20 && c < 0x7f; });
			const unsigned complement = h[0x1c] | h[0x1d] << 8, checksum = h[0x1e] | h[0x1f] << 8;
			while (!title.empty() && title.back() == ' ')
				title.pop_back();
			if (readable && !title.empty() && (complement ^ checksum) == 0xffff)
				return title;
			if (readable && !title.empty() && printable.empty())
				printable = title;
		}
		return printable;
	}

	Device SnesDevice(const std::string& title, const std::string& fileName)
	{
		// the games that need one, their headers' titles and No-Intro's names (the start of either); the
		// ones that play with the pad too are left to it (Lemmings 2, T2: The Arcade Game, Tin Star)
		static constexpr const char* kMouse[] = {"MARIOPAINT", "MARIOWARIO"};
		static constexpr const char* kScope[] = {"SUPERSCOPE6", "NINTENDOSCOPE6", "BATTLECLASH", "SPACEBAZOOKA", "METALCOMBAT",
			"YOSHISSAFARI", "YOSHISAFARI", "YOSHINOROADHUNTING", "YOSSYNOROADHUNTING", "XZONE", "BAZOOKABLITZKRIEG"};
		const std::string names[] = {Folded(title), Folded(std::filesystem::path(fileName).stem().string())};
		for (const std::string& name : names)
		{
			if (name.empty())
				continue;
			for (const char* key : kMouse)
				if (name.rfind(key, 0) == 0)
					return Device::Mouse;
			for (const char* key : kScope)
				if (name.rfind(key, 0) == 0)
					return Device::LightGun;
		}
		return Device::Pad;
	}
}
