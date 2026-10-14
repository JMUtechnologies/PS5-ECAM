// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: a game's own tile on the PS5's home screen, in the open
// PS5 Forwarder Format (github.com/heydemoura/ps5-forwarder-format, its psfwd library, GPL-3.0): a
// folder in /data/homebrew holding the format's tile program, whose forwarder.json asks the format's
// launcher to start PS5CEMU-HAR (PPSA99360) with "--rom <the game's path> --exit-after-game"
// (main_ps5.cpp). Its picture is the game's box art, or its icon, made square. ShadowMount Plus puts
// the folder on the home screen as it does any homebrew; Forwarder Manager can edit it. Without a HEN
// the app may not make folders in /data/homebrew (root's): the tile is then made in
// /data/ps5cemu/tiles/pending and moved in by tile-installer.elf (port/payloads), which elfldr runs as
// root, as the tiles' own launcher needs elfldr anyway; a tile is taken away the same way, asked for in
// /data/ps5cemu/tiles/remove.

#pragma once

#include <cstdint>
#include <string>

namespace ps5forwarders
{
	struct Request
	{
		std::string name;  // under the tile
		std::string rom;   // the game's path, as --rom takes it
		std::string cover; // its box art, else its icon (empty: the app's own picture)
	};

	struct Status
	{
		enum class State
		{
			Idle,
			Making,
			Done,	// titleId made (replaced: the game's tile from before, made again)
			Failed, // message says why
		} state = State::Idle;
		std::string titleId;
		std::string folder; // the tile's: /data/homebrew/<titleId>
		bool replaced = false;
		bool removed = false; // the request took the tile away (StartRemove)
		std::string message;
		uint32_t finished = 0; // the requests that ended, so the launcher answers each once
	};

	// Makes the game's tile, or makes it again, on a thread of its own
	void Start(const Request& request);
	// Whether the game (its path, as --rom has it) has a tile of PS5CEMU-HAR's in /data/homebrew
	bool HasTile(const std::string& rom);
	// Takes the game's tile away (its folder in /data/homebrew), on a thread of its own
	void StartRemove(const Request& request);
	Status GetStatus();
	// Before a game: a tile being made is finished first
	void Stop();
}
