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
			action("nxextra:error", Tr("Eden settings unavailable"), view.error);
			return rows;
		}
		if ((page == "nxmods" || page == "nxcheats" || (page == "nxpost" && InGameSettings())) && !context.title)
		{
			action("nxextra:noid", Tr("A real Switch title ID is required"), Tr("Read the game with its console keys before changing its mods or cheats."));
			return rows;
		}
		auto toggle = [&](const std::string& id, const std::string& name, bool on, const std::string& description) {
			Row row; row.id = id; row.kind = Row::Kind::Toggle; row.label = name;
			row.on = on; row.value = on ? Tr("On") : Tr("Off"); row.description = description;
			row.help = Tr("Applied the next time the game starts. Files are kept in place; only Eden's choice is changed.");
			rows.push_back(std::move(row));
		};
		if (page == "nxmods" || page == "nxcheats")
		{
			const bool cheats = page == "nxcheats";
			toggle(cheats ? "nxextra:cheats" : "nxextra:mods", cheats ? Tr("Enable cheats") : Tr("Enable mods"),
				cheats ? view.cheatsOn : view.modsOn, Tr("Individual choices are kept while this switch is off."));
			const auto& items = cheats ? view.cheats : view.mods;
			for (size_t i = 0; i < items.size(); i++)
				toggle(std::string(cheats ? "nxextra:cheat:" : "nxextra:mod:") + std::to_string(i), items[i].name, items[i].on,
					cheats ? items[i].source : Tr("Installed code patches or replacement game files."));
			if (items.empty()) action("nxextra:empty", cheats ? Tr("No cheats found") : Tr("No mods found"),
				Tr("Copy your files into the game's mods or cheats folder, then reopen this page."));
		}
		else if (page == "nxpost")
		{
			Row row; row.id = "nxextra:preset"; row.kind = Row::Kind::Choice; row.label = Tr("Post-processing");
			if (context.title) row.options.push_back(Tr("Default"));
			for (const auto& preset : view.presets) row.options.push_back(preset.empty() ? Tr("Off") : preset);
			const auto found = std::find(view.presets.begin(), view.presets.end(), view.preset);
			row.index = context.title && !view.ownPreset ? 0 : (context.title ? 1 : 0) + (int)(found - view.presets.begin());
			row.value = row.options[std::clamp(row.index, 0, (int)row.options.size() - 1)];
			row.description = Tr("Eden's Vulkan ReShade preset. Applied when the next game starts.");
			row.help = Tr("Default follows the selected profile. Off disables the effect for this game. Custom selections already in Eden are preserved.");
			rows.push_back(std::move(row));
		}
		else if (page == "nxprofiles")
		{
			if (view.profiles.empty()) action("nxextra:noprofiles", Tr("No profiles found"), Tr("Start a Switch game once to create Eden's first profile."));
			else
			{
				Row row; row.id = "nxextra:profile"; row.kind = Row::Kind::Choice; row.label = Tr("Player profile");
				for (const auto& profile : view.profiles) row.options.push_back(profile.name + " · " + profile.id.substr(24));
				row.index = std::clamp(view.profile, 0, (int)row.options.size() - 1); row.value = row.options[row.index];
				row.description = Tr("The Switch player used the next time a game starts.");
				row.help = Tr("Selects an existing Eden player and remembers the choice for this PS5 user. Save files are not moved or deleted.");
				rows.push_back(std::move(row));
			}
		}
		Row refresh;
		refresh.id = "nxextra:refresh"; refresh.kind = Row::Kind::Action;
		refresh.label = Tr("Refresh");
		refresh.description = Tr("Read the installed files and Eden settings again.");
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
