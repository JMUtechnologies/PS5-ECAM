// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: what GameTDB (https://www.gametdb.com) knows of a game, for its details page and the
// in-game menus: its description, developer, publisher, release date, genre, players and rating, by
// the ID on its box, as the box art is found (boxart.h). The app carries GameTDB's English Wii U,
// 3DS, DS, Switch and Wii (with the GameCube's discs) databases, cut down to those fields (tools/render-gametdb.py: assets/gametdb), so
// it needs no network for them; each is read the first time a game of its system is looked up. A
// Switch game's files have no box ID (GameTDB's is its product code's last part, HAC-P-AHNXA, and its
// database has no title IDs): the game is found by its name (FindByTitle).

#pragma once

#include "boxart.h"

#include <string>
#include <vector>

namespace ps5gameinfo
{
	struct Info
	{
		std::string id;		   // ALZE01, AJRE, AHNXA
		std::string title;	   // GameTDB's English title
		std::string region;	   // NTSC-U, PAL, NTSC-J...
		std::string synopsis;  // paragraphs separated by blank lines
		std::string developer, publisher;
		std::string released;  // YYYY, YYYY-MM or YYYY-MM-DD
		std::string genre;	   // GameTDB's list, "action,adventure"
		std::string rating;	   // "ESRB E10+"
		int players = 0;	   // 0: not known
	};

	// GameTDB's entry for a game's box ID; false when it has none (or the ID is empty).
	bool Find(ps5boxart::System system, const std::string& id, Info& info);
	// The box ID of the game GameTDB has by this title (English, as its database has them), their
	// letters and digits compared, case and accents aside; empty when it has none by that title.
	std::string FindByTitle(ps5boxart::System system, const std::string& title);

	// For people: "March 3, 2017" (or "March 2017", "2017"), and "Action, Adventure, Role-playing".
	std::string ReleaseDate(const std::string& released);
	std::string Year(const std::string& released);
	std::string Genres(const std::string& genre, int most = 0);

	// The libretro sides' games, which GameTDB has none of: the LaunchBox Games Database's year and publisher
	// for a game file's name (No-Intro's), and the picture of its cartridge from the region the name gives (a
	// Japanese NES game's Famicom cartridge), else one shaped the same (never a Famicom cartridge for an
	// American NES game); ps5boxart::FetchCarts fetches it. From tools/render-carts.py's table,
	// launchbox-carts.tsv.gz beside GameTDB's (a Game & Watch game's "cartridge" its own unit). system: nes,
	// snes, gb, gbc, gba, n64, vb, mini or gw; false when the game is not in it.
	struct Cart
	{
		int year = 0;
		std::string publisher;
		std::string picture; // its name at the LaunchBox Games Database (empty: none in a region shaped as it is)
	};
	bool FindCart(const std::string& system, const std::string& fileName, Cart& cart);
	// A game's manual as Nintendo keeps it online (tools/render-manuals.py's table, manuals.tsv.gz: the NES and
	// Super NES Classic Editions' and the Wii U's electronic manuals), by any of its names (its file's, its own,
	// GameTDB's), matched as FindCart's are: the PDF's address, for a QR code; empty when there is none.
	// system: nes, snes or wiiu.
	std::string FindManual(const std::string& system, const std::vector<std::string>& names);
	// Where the databases are (default: the app's assets/gametdb); the launcher's preview on a PC
	// gives its own.
	void SetFolder(const std::string& folder);
}
