// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: what a Switch game file (.nsp or .xci) is, read with the player's own keys (prod.keys and
// title.keys in Eden's keys/) as Eden reads it: its title ID, whether it is a game, an update or DLC,
// its own name and its icon, for the Switch side's library (#64, #67). Without this the launcher had
// only the file's name to go by, so an update or DLC named like a game was listed as one, a game named
// like an update was left out, and no game had a picture until Eden had read it.
//
// The formats are the Switch's own (switchbrew.org: NCA, PFS0, HFS0, XCI, Ticket, RomFS, NACP): an
// NSP is a PFS0 of NCAs and tickets, an XCI a gamecard image whose "secure" HFS0 partition holds the
// NCAs. An NCA's header is AES-128-XTS with the header key (sector numbers big-endian in the tweak);
// its sections are AES-128-CTR, with the key in its key area (key_area_key_application_XX), or for one
// with a rights ID, its ticket's title key (titlekek_XX). The control NCA's RomFS holds control.nacp
// (the names) and icon_<language>.dat (JPEG).

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ps5eden::nx
{
	enum class Kind
	{
		Unknown,
		Game,	// an application (its title ID ends in 000)
		Update, // a patch (800)
		Dlc,	// add-on content
	};

	using Key = std::array<uint8_t, 16>;

	struct Keys
	{
		std::array<uint8_t, 32> header{};
		bool hasHeader = false;
		std::map<int, Key> keyArea[3]; // key_area_key_application_XX, _ocean_XX, _system_XX, by XX
		std::map<int, Key> titlekek;   // titlekek_XX
		std::map<std::string, Key> titleKeys; // title.keys: a rights ID (lower-case hex) -> its encrypted title key
	};
	// prod.keys and title.keys in a keys folder. False when there is no header key (nothing can be read)
	bool LoadKeys(const std::string& folder, Keys& keys);

	struct Info
	{
		uint64_t titleId = 0;
		Kind kind = Kind::Unknown;
		std::string name;		   // the game's own, from its control data; empty when not read
		std::vector<uint8_t> icon; // its icon (JPEG), when asked for and read
	};
	// What a file is: its title ID and kind from its NCAs' headers, and its name (and, with icon, its
	// icon) from its control NCA. False, with why, when it cannot be read as a package at all; a package
	// whose control NCA cannot be read (no key for it) is still true, without a name.
	bool Read(const std::string& path, const Keys& keys, bool icon, Info& info, std::string& why);

	// An icon (JPEG) as the cover TGA Eden keeps and the launcher draws (32-bit, top-down), written
	// whole or not at all
	bool WriteIconTga(const std::vector<uint8_t>& jpeg, const std::string& path);
}
