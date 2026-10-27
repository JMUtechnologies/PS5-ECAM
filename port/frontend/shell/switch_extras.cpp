// SPDX-License-Identifier: GPL-3.0-or-later
#include "shell_internal.h"
#include "../../eden/library.h"
#include "../../ps5/pad.h"
#include <algorithm>
#include <filesystem>

namespace ps5shell
{
	ps5eden::extras::Context Shell::SwitchExtrasContext() const
	{
		ps5eden::extras::Context context;
		context.settings = ps5eden::DataFolder() + "/config/prosperoeden.json";
		context.user = ps5eden::DataFolder() + "/user";
		context.gameFiles = ps5eden::GameFilesFolder();
		context.ps5User = ps5pad::UserId(0);
		if (InGameSettings() && !ps5eden::IsMadeId(m_gameTitle))
		{
			context.title = m_gameTitle;
			const auto parent = std::filesystem::path(m_gameRef.file).parent_path().lexically_normal();
			if (parent != std::filesystem::path(ps5eden::ScanFolder(m_settings.nx.gamesFolder)).lexically_normal())
				context.gameFolder = parent.string();
		}
		return context;
	}

	std::vector<Row> Shell::SwitchExtrasRows(const std::string& page)
	{
		const auto context = SwitchExtrasContext();
		const std::string key = context.settings + "\n" + context.gameFolder + "\n" + std::to_string(context.title) + ":" + std::to_string(context.ps5User);
		if (m_switchExtrasKey != key)
		{
			m_switchExtras = ps5eden::extras::Read(context);
			m_switchExtrasKey = key;
		}
		const auto& view = m_switchExtras;
		std::vector<Row> rows;
		auto action = [&](const char* id, const std::string& name, const std::string& message) {
			Row row; row.id = id; row.label = name; row.description = message;
			row.kind = Row::Kind::Action; row.dimmed = true; rows.push_back(std::move(row));
		};
		if (!view.error.empty())
		{
			action("nxextra:error", Tr("Eden's settings could not be read"), view.error);
			return rows;
		}
		if ((page == "nxmods" || page == "nxcheats" || (page == "nxpost" && InGameSettings())) && !context.title)
		{
			action("nxextra:noid", Tr("This game has no title ID"), Tr("Mods and cheats go by a game's title ID, which Eden reads with the Switch's keys: add the keys, then look for games again."));
			return rows;
		}
		auto toggle = [&](const std::string& id, const std::string& name, bool on, const std::string& description) {
			Row row; row.id = id; row.kind = Row::Kind::Toggle; row.label = name;
			row.on = on; row.value = on ? Tr("On") : Tr("Off"); row.description = description;
			row.help = Tr("From the game's next start. The files stay where they are: only Eden's choice changes.");
			rows.push_back(std::move(row));
		};
		if (page == "nxmods" || page == "nxcheats")
		{
			const bool cheats = page == "nxcheats";
			toggle(cheats ? "nxextra:cheats" : "nxextra:mods", cheats ? Tr("Use cheats") : Tr("Use mods"),
				cheats ? view.cheatsOn : view.modsOn, Tr("Off, each one's choice below is kept for when it is on again."));
			const auto& items = cheats ? view.cheats : view.mods;
			for (size_t i = 0; i < items.size(); i++)
				toggle(std::string(cheats ? "nxextra:cheat:" : "nxextra:mod:") + std::to_string(i), items[i].name, items[i].on,
					cheats ? items[i].source : Tr("A mod: code patches, or the game's own files replaced."));
			if (items.empty()) action("nxextra:empty", cheats ? Tr("No cheats found") : Tr("No mods found"),
				Tr("Put them in the game's mods or cheats folder, then Look again."));
		}
		else if (page == "nxpost")
		{
			Row row; row.id = "nxextra:preset"; row.kind = Row::Kind::Choice; row.label = Tr("ReShade");
			if (context.title) row.options.push_back(Tr("Default"));
			for (const auto& preset : view.presets) row.options.push_back(preset.empty() ? Tr("Off") : preset);
			const auto found = std::find(view.presets.begin(), view.presets.end(), view.preset);
			row.index = context.title && !view.ownPreset ? 0 : (context.title ? 1 : 0) + (int)(found - view.presets.begin());
			row.value = row.options[std::clamp(row.index, 0, (int)row.options.size() - 1)];
			row.description = Tr("A ReShade look over the game's picture, Eden's own or one of yours. Applies to the next game.");
			row.help = Tr("Default is the side's look, Off none, for this game. Your own looks are .fxp files in the game's reshade folder or in Eden's user/post_presets.");
			rows.push_back(std::move(row));
		}
		else if (page == "nxprofiles")
		{
			if (view.profiles.empty()) action("nxextra:noprofiles", Tr("No players yet"), Tr("Make one below, or start a Switch game once."));
			else
			{
				Row row; row.id = "nxextra:profile"; row.kind = Row::Kind::Choice; row.label = Tr("Player");
				for (const auto& profile : view.profiles) row.options.push_back(profile.name + " · " + profile.id.substr(24));
				row.index = std::clamp(view.profile, 0, (int)row.options.size() - 1); row.value = row.options[row.index];
				row.description = Tr("Who plays the next Switch game.");
				row.help = Tr("Remembered for this PS5 user. Each player has saves of their own; none are moved or deleted.");
				rows.push_back(std::move(row));
			}
			Row create;
			create.id = "nxextra:createprofile"; create.kind = Row::Kind::Action; create.label = Tr("New player");
			create.description = Tr("A player with saves of their own.");
			create.dimmed = view.profiles.size() >= 8; rows.push_back(std::move(create));
			if (!view.profiles.empty())
			{
				Row rename;
				rename.id = "nxextra:renameprofile"; rename.kind = Row::Kind::Action; rename.label = Tr("Rename the player");
				rename.description = view.profiles[view.profile].name; rows.push_back(std::move(rename));
				Row remove;
				remove.id = "nxextra:removeprofile"; remove.kind = Row::Kind::Hold; remove.label = Tr("Remove the player");
				remove.value = Tr("Hold Cross"); remove.description = Tr("From the list only: their saves and settings are kept.");
				remove.help = Tr("The last player stays. The list from before is kept as profiles.dat.ecam-before.");
				remove.dimmed = view.profiles.size() <= 1; rows.push_back(std::move(remove));
			}
		}
		Row refresh;
		refresh.id = "nxextra:refresh"; refresh.kind = Row::Kind::Action;
		refresh.label = Tr("Look again");
		refresh.description = Tr("Reads the game's folders and Eden's settings again.");
		rows.push_back(std::move(refresh));
		return rows;
	}

	bool Shell::ChangeSwitchExtra(const Row& row, int step, bool cross)
	{
		if (row.id == "nxextra:refresh")
		{
			if (cross) { m_switchExtrasKey.clear(); m_feedback.Play(ui::Cue::Focus); }
			return cross;
		}
		if (row.id == "nxextra:createprofile" || row.id == "nxextra:renameprofile")
		{
			if (!cross || row.dimmed) return false;
			const bool create = row.id == "nxextra:createprofile";
			const auto context = SwitchExtrasContext();
			const auto view = ps5eden::extras::Read(context);
			if (!view.error.empty()) { Toast(view.error); return false; }
			if (!create && view.profiles.empty()) return false;
			const std::string id = create ? "" : view.profiles[view.profile].id;
			const std::string name = create ? "" : view.profiles[view.profile].name;
			OpenNameKeyboard(create ? Tr("A name for the new player") : Tr("The player's new name"), name, 31,
				[this, context, id, create](const std::string& value) {
					if (value.empty()) return;
					std::string error;
					const bool ok = create ? ps5eden::extras::CreateProfile(context, value, error) : ps5eden::extras::RenameProfile(context, id, value, error);
					if (!ok) Toast(error);
					else Toast(create ? Tr("New player made: choose them in Player to play as them") : Tr("Player renamed"));
					m_switchExtrasKey.clear();
				});
			return true;
		}
		if (row.dimmed || (row.kind != Row::Kind::Toggle && row.kind != Row::Kind::Choice)) return false;
		if (row.kind == Row::Kind::Toggle && !cross && step && ((step > 0) == row.on)) return false;
		const auto context = SwitchExtrasContext();
		const std::string& id = row.id;
		std::string error;
		bool saved = false;
		if (id == "nxextra:mods" || id == "nxextra:cheats") saved = ps5eden::extras::SetMaster(context, id == "nxextra:cheats", !row.on, error);
		else if (id.starts_with("nxextra:mod:") || id.starts_with("nxextra:cheat:"))
		{
			const bool cheat = id.starts_with("nxextra:cheat:");
			const auto& items = cheat ? m_switchExtras.cheats : m_switchExtras.mods;
			const auto index = (size_t)std::strtoul(id.c_str() + (cheat ? 14 : 12), nullptr, 10);
			if (index < items.size()) saved = cheat ? ps5eden::extras::SetCheat(context, items[index].source, items[index].name, !row.on, error) :
				ps5eden::extras::SetMod(context, items[index].source, !row.on, error);
		}
		else if (!row.options.empty())
		{
			const int next = (row.index + (step < 0 ? -1 : 1) + (int)row.options.size()) % (int)row.options.size();
			if (id == "nxextra:profile" && next < (int)m_switchExtras.profiles.size())
				saved = ps5eden::extras::SelectProfile(context, m_switchExtras.profiles[next].id, ps5pad::UserId(0), error);
			else if (id == "nxextra:preset")
			{
				const int preset = next - (context.title ? 1 : 0);
				if (preset < 0) saved = ps5eden::extras::SetPreset(context, "", true, error);
				else if (preset < (int)m_switchExtras.presets.size()) saved = ps5eden::extras::SetPreset(context, m_switchExtras.presets[preset], false, error);
			}
		}
		if (saved) m_feedback.Play(ui::Cue::Focus);
		else if (!error.empty()) Toast(error);
		m_switchExtrasKey.clear();
		return saved;
	}
}
