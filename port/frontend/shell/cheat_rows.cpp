// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the launcher's cheats (app/cheats.h): Fetch cheats automatically, every game in the library looked
// for as the launcher starts; a game's Cheats page in its settings, Get cheats first, then each cheat on or off
// (the libretro sides', the 3DS's and the DS's: the Switch's and Porpoise's own pages have their lists, Get
// cheats added to them). The in-game menu's Cheats turns the same cheats on and off while playing.
#include "shell_internal.h"
#include "../../eden/library.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace ps5shell
{
	namespace
	{
		const char* Source(ps5cheats::Kind kind)
		{
			switch (kind)
			{
			case ps5cheats::Kind::Retro:
			case ps5cheats::Kind::Nds: return Tr("From libretro-database, by the game file's name.");
			case ps5cheats::Kind::N3ds: return Tr("From Sharkive, by the game's title ID.");
			case ps5cheats::Kind::Switch: return Tr("From switch-cheats-db, for every version of the game.");
			case ps5cheats::Kind::Porpoise: return Tr("From codes.rc24.xyz, by the disc's ID.");
			case ps5cheats::Kind::None: break;
			}
			return "";
		}
	}

	Row Shell::CheatsAutoRow(const ps5settings::Launcher& settings)
	{
		Row row;
		row.kind = Row::Kind::Toggle;
		row.id = "cheatsauto";
		row.label = Tr("Fetch cheats automatically");
		row.on = settings.cheats;
		row.value = row.on ? Tr("On") : Tr("Off");
		row.description = Tr("Cheats for every game in your library, each off until you turn it on.");
		row.help = Tr("Downloads cheats for every game on the systems that have them: NES, SNES, the Game Boys, N64, DS, 3DS, Switch, GameCube and Wii. "
					  "Each cheat is off until you turn it on in the game's settings or the in-game menu. Cheats can crash a game or spoil a save: save first.");
		return row;
	}

	void Shell::FetchCheats()
	{
		std::vector<ps5cheats::Game> games;
		for (int side = 0; side < ps5launcher::kSideCount; side++)
		{
			if (!ps5cheats::Supported(side))
				continue;
			for (const ps5catalog::Entry& entry : ps5catalog::Games((ps5catalog::System)side))
			{
				const ps5emu::Game& game = entry.game;
				// a DS game the 3DS side listed before the DS had a side; a Switch game with no title ID (no keys)
				if ((side == (int)System::N3ds && game.nds) || (side == (int)System::Switch && ps5eden::IsMadeId(game.titleId)))
					continue;
				games.push_back({side, game.titleId, game.path.string()});
			}
		}
		ps5cheats::Fetch(games);
	}

	void Shell::CheatsRefresh()
	{
		if (ps5cheats::Arrivals() == m_cheatArrivals)
			return;
		m_cheatArrivals = ps5cheats::Arrivals();
		// the Switch's and Porpoise's lists, read again with the new files
		m_switchExtrasKey.clear();
		m_porpoiseGame.file.clear();
	}

	Row Shell::CheatsGetRow()
	{
		const ps5cheats::Game game{m_gameRef.side, m_gameRef.titleId, m_gameRef.file};
		const int looked = ps5cheats::Looked(game);
		const size_t count = ps5cheats::Count(game);
		Row row;
		row.kind = Row::Kind::Action;
		row.id = "cheats:get";
		row.label = Tr("Get cheats");
		row.value = count ? TrP((long long)count, "{0} cheat", "{0} cheats") :
			looked == -1 && ps5cheats::Pending() ? std::string(Tr("Looking…")) :
			looked == 0							 ? std::string(Tr("None found")) :
			looked == -2						 ? std::string(Tr("Not reached: is the PS5 online?")) :
												   std::string(Tr("Cross to download"));
		row.description = Source(ps5cheats::KindOf(m_gameRef.side));
		row.help = std::string(Tr("Downloads this game's cheats, adding any new ones to what is here. Each is off until you turn it on. "
								  "Cheats can crash a game or spoil a save: save first.")) + " " + row.description;
		return row;
	}

	std::vector<Row> Shell::CheatRows()
	{
		CheatsRefresh();
		const ps5cheats::Game game{m_gameRef.side, m_gameRef.titleId, m_gameRef.file};
		std::vector<Row> rows{CheatsGetRow()};
		const std::vector<ps5cheats::Cheat> cheats = ps5cheats::List(game);
		constexpr size_t kMost = 80; // the most a page lists; every one is in the in-game menu
		for (size_t i = 0; i < cheats.size() && i < kMost; i++)
		{
			Row row;
			row.kind = Row::Kind::Toggle;
			row.id = "cheats:" + std::to_string(i);
			row.label = cheats[i].name.empty() ? std::string(Tr("Cheat")) : cheats[i].name;
			row.on = cheats[i].on;
			row.value = row.on ? Tr("On") : Tr("Off");
			row.description = Tr("Applies when the game starts. Turn it on or off while playing in the in-game menu's Cheats.");
			row.help = row.description;
			rows.push_back(std::move(row));
		}
		if (cheats.size() > kMost)
		{
			Row more;
			more.id = "cheats:more";
			more.label = TrP((long long)(cheats.size() - kMost), "{0} more in the in-game menu", "{0} more in the in-game menu");
			more.description = Tr("Every cheat is listed in the in-game menu's Cheats.");
			more.help = more.description;
			more.dimmed = true;
			rows.push_back(std::move(more));
		}
		if (cheats.empty())
		{
			Row none;
			none.id = "cheats:none";
			none.label = Tr("No cheats yet");
			none.description = Tr("Get cheats downloads them. Your own files work too.");
			// tr: {0} is where the game's cheats file goes
			none.help = TrF("Get cheats downloads them. Or put your own file at {0}.", ps5cheats::FileFor(game));
			none.dimmed = true;
			rows.push_back(std::move(none));
		}
		return rows;
	}

	void Shell::ChangeCheats(const Row& row, int step, bool cross)
	{
		const ps5cheats::Game game{m_gameRef.side, m_gameRef.titleId, m_gameRef.file};
		const std::string what = row.id.substr(std::strlen("cheats:"));
		if (what == "get")
		{
			if (!cross)
				return;
			if (ps5cheats::FileFor(game).empty())
			{
				m_feedback.Play(ui::Cue::Denied);
				Toast(Tr("This game has no ID to look its cheats up by"));
				return;
			}
			ps5cheats::Fetch({game}, true);
			m_feedback.Play(ui::Cue::Select);
			Toast(Tr("Looking for cheats…"));
			return;
		}
		if (what.empty() || !std::isdigit((unsigned char)what[0]))
		{
			if (cross)
				m_feedback.Play(ui::Cue::Denied);
			return;
		}
		if (!cross && step != 0 && ((step > 0) == row.on))
		{
			m_feedback.Play(ui::Cue::Edge);
			return;
		}
		std::string error;
		if (!ps5cheats::SetOn(game, (size_t)std::atoi(what.c_str()), !row.on, error))
		{
			m_feedback.Play(ui::Cue::Denied);
			Toast(TrF("The cheat could not be saved: {0}", error));
			return;
		}
		m_feedback.Play(ui::Cue::Toggle);
	}
}
