// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: ProsperoEden's rooms on the Switch side, in the app's own Settings > Multiplayer, never in
// ProsperoEden's launcher. A room is a server where Switch games played in Eden find each other for
// their local wireless play, as on desktop Eden. The public ones are listed by the same directory
// ProsperoEden's Multiplayer page reads (its headless/multiplayer_directory.cpp: the project's own
// featured rooms first, then the compatible ones, the fullest first), downloaded here; a private
// one is reached by its address, port and password. The room chosen is kept in the Switch side's
// settings (ps5settings::N3ds's nxRoom...) and handed to Eden's program as a game starts
// (ps5eden::GameArguments), which joins it before the game loads and says in the in-game menu who is
// in it (port/eden/ingame.h). A room that cannot be joined brings the player back to the library
// with why.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5rooms
{
	// Eden's rooms' protocol version (its network/room.h network_version): a room with another one
	// cannot be joined
	constexpr unsigned kProtocol = 1;
	constexpr uint16_t kDefaultPort = 24872;

	struct Room
	{
		std::string name, game, address;
		uint16_t port = 0;
		unsigned players = 0, capacity = 0;
		bool password = false, compatible = false;
		bool featured = false; // one of ProsperoEden's own rooms
	};

	struct Status
	{
		bool busy = false;	  // the list is being downloaded
		bool fetched = false; // it was, once at least
		std::string error;	  // why the last download failed, in the player's language
		std::vector<Room> rooms;
	};

	// The public list downloaded again, on a thread of its own; nothing when one is under way
	void Refresh();
	// Whether the list was asked for since the app started (Settings > Multiplayer asks as it opens)
	bool Asked();
	Status Get();

	// A nickname a room takes: 4 to 20 characters, as Eden checks them
	bool ValidNickname(const std::string& name);
}
