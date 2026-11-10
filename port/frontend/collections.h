// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../app/catalog.h"
#include <algorithm>
#include <string_view>

namespace ps5collections
{
	inline bool Matches(const ps5catalog::Entry& entry, std::string_view name)
	{
		return name.empty() || std::find(entry.collections.begin(), entry.collections.end(), name) != entry.collections.end();
	}

	// The frontend may have enriched the game's ID or artwork metadata after its scan.
	inline void SyncMembership(ps5catalog::Entry& frontend, const ps5catalog::Entry& stored)
	{
		frontend.collections = stored.collections;
	}
}
