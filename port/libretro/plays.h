// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: which sides play on a libretro core in the app (port/libretro/game.h), for the launcher's
// files, which need nothing else of the frontend (no libretro.h).
#pragma once

namespace ps5libretro
{
	// A side whose core (ps5retro::kSystems' core) this build links: its games play in the app, on the
	// core (every side but the GameCube's and the Wii's, which are Porpoise X's)
	bool Plays(int side);
	// Its core's name as players read it ("Mesen", "ParaLLEl-N64"); empty for a side with none
	const char* CoreTitle(int side);
}
