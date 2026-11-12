// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: box art for the launcher's game details, from GameTDB (art.gametdb.com), which has
// the Wii U's, the 3DS's, the DS's and the Switch's covers by the ID printed on each game's box (ALZE01
// for a Wii U game, AREE for a 3DS one, AMCE for a DS one, AHNXA for a Switch one, found by its name:
// gameinfo.h). They are fetched in the background over HTTP, as the Wii's homebrew loaders fetch
// theirs, decoded, scaled to the launcher's cover and kept as TGAs it shows:
// covers/boxart/<wiiu|3ds|ds|switch>/<ID>.tga. An ID GameTDB answers "not found" for in every region
// is remembered (<ID>.none) and asked for again after two weeks, or when Settings > Online asks for
// the missing covers. No network, or GameTDB refusing or busy, is tried again a little later: it never
// counts as "no cover".

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5boxart
{
	enum class System
	{
		WiiU,
		N3ds,
		Nds, // a DS game, on the 3DS side
		Switch,
		GameCubeWii, // the GameCube's and the Wii's: GameTDB keeps both as "wii"
	};

	// The largest a cover is kept: the launcher's cover area at 4K
	constexpr int kMaxWidth = 576, kMaxHeight = 704;

	// A GameCube or Wii game's loading picture: its title screen, from libretro's thumbnails by the game file's
	// name, made full-screen (banner_compose.h) and kept in covers/boxart/titles. Empty until it has come.
	std::string TitleArtPath(const std::string& name);
	void FetchTitleArt(const std::vector<std::pair<bool, std::string>>& wiiAndNames);
	// SteamGridDB's wide art is asked for first, with the player's key (kept beside the covers), else the one
	// this build was given (PS5ECAM_STEAMGRIDDB_KEY, never in the source). A game with none has libretro's.
	bool HasSteamGridKey();
	bool PlayerSteamGridKey();
	std::string PlayerSteamGridKeyText(); // what the player entered, to check and change it
	void SetSteamGridKey(const std::string& key); // empty: the player's is forgotten

	// The libretro sides' covers (NES, SNES, the Game Boys, N64, Virtual Boy...): libretro's thumbnails, by its
	// folder for the console ("Nintendo - Game Boy") and the game file's name, as its No-Intro set names it.
	// Kept in covers/boxart/retro/<folder>/<name>.tga; one with none is asked for again after two weeks, or
	// at once with again (Settings > Online).
	std::string RetroPath(const std::string& system, const std::string& name);
	void FetchRetro(const std::string& system, const std::vector<std::string>& names, bool again = false);
	size_t RetroPending();
	// Of names in a No-Intro set (libretro's lists), the one that fits a game file's name best, as its cover is
	// found: the same title, then a subtitle or a word more, an American or worldwide release first; empty: none
	std::string BestName(const std::vector<std::string>& theirs, const std::string& name);

	// The Timeline's real media (6.0): a game's disc (the GameCube's, the Wii's, the Wii U's) or its card (the
	// DS's, the 3DS's, the Switch's), GameTDB's scan of it by the cover's ID, clear around its own shape. Kept
	// beside the covers, covers/boxart/<folder>/media/<ID>.tga, on the same queue, after the covers asked for.
	std::string MediaPath(System system, const std::string& id);
	void FetchMedia(System system, const std::vector<std::string>& ids);
	uint32_t MediaArrivals(); // how many have arrived since the start

	// And the libretro sides' cartridges, the LaunchBox Games Database's pictures of them by the names
	// ps5gameinfo::FindCart gives: kept as covers/boxart/carts/<picture>.tga, fetched one at a time on a thread
	// of their own (from images.launchbox-app.com); MediaArrivals counts them too.
	std::string CartPath(const std::string& picture);
	void FetchCarts(const std::vector<std::string>& pictures);

	// The cover's TGA when it has been fetched, else empty.
	std::string Path(System system, const std::string& id);
	// Fetches the covers not fetched yet, one at a time, on a thread of its own.
	void Fetch(System system, const std::vector<std::string>& ids);
	// Settings > Online: the covers of these IDs asked for again now, those GameTDB had none for too;
	// replace: the ones already here downloaded again as well.
	void Refetch(System system, const std::vector<std::string>& ids, bool replace);
	// How many covers are waiting to be fetched (or being fetched)
	size_t Pending();
	// The covers that arrived since the last call, as "<wiiu|3ds|ds|switch>/<ID>" (a replaced one included)
	std::vector<std::string> TakeArrived();
	// How many covers have arrived since the start: the launcher shows a new one when it changes.
	uint32_t Arrivals();
	// Whether GameTDB answered in this session: 1 it did, 0 it did not (no network), -1 not asked yet
	// (the Setup check's).
	int Answered();
	// The launcher's setting: when off, nothing more is fetched.
	void SetEnabled(bool enabled);
	// 3D box art (Settings > Launcher): each GameTDB cover's spine fetched too, from its whole cover
	void SetSpines(bool spines);
	// Where a GameTDB cover's spine is kept (<ID>.spine.tga beside it), whether or not it is there yet
	std::string SpinePath(const std::string& cover);
	// Before a game starts: nothing more is fetched in this session (the app starts over after the
	// game), and a cover being fetched is waited for, a few seconds at most.
	void Stop();

	// A TGA's size (its header), for laying it out.
	bool ImageSize(const std::string& path, int& width, int& height);
}
