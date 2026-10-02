// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: one game's graphic packs for the launcher (emulator.h).
//
// The packs are the ones Cemu loaded at start (GraphicPack2::LoadAll), and the choices are saved
// as Cemu's Graphic Packs window saves them (GraphicPacksWindow2::SaveStateToConfig), so this file
// keeps Cemu's MPL-2.0 licence. Changes apply to the next game started, as on the desktop.

#include "emulator.h"
#include "../ps5/log.h"

#include "Cafe/GraphicPack/GraphicPack2.h"
#include "config/CemuConfig.h"

namespace ps5emu
{
	namespace
	{
		using PackPtr = std::shared_ptr<GraphicPack2>;

		// A game's packs in the order the launcher lists them: by their path in the pack tree.
		std::vector<PackPtr> PacksOf(uint64_t titleId)
		{
			std::vector<PackPtr> packs;
			for (const auto& pack : GraphicPack2::GetGraphicPacks())
				if (pack->ContainsTitleId(titleId))
					packs.push_back(pack);
			std::sort(packs.begin(), packs.end(), [](const PackPtr& a, const PackPtr& b) {
				return boost::ilexicographical_compare(a->GetVirtualPath(), b->GetVirtualPath());
			});
			return packs;
		}

		// As GraphicPacksWindow2::SaveStateToConfig.
		void SaveState()
		{
			auto& data = GetConfigHandle().data();
			data.graphic_pack_entries.clear();
			for (const auto& pack : GraphicPack2::GetGraphicPacks())
			{
				const auto filename = _utf8ToPath(pack->GetNormalizedPathString());
				if (pack->IsEnabled())
				{
					auto& entry = data.graphic_pack_entries[filename];
					for (const auto& preset : pack->GetActivePresets())
						entry.try_emplace(preset->category, preset->name);
				}
				else if (pack->IsDefaultEnabled())
					data.graphic_pack_entries[filename].try_emplace("_disabled", "false");
			}
			GetConfigHandle().Save();
		}

		// The preset categories with a preset to show, in the pack's order, as
		// GraphicPacksWindow2::LoadPresetSelections lists them.
		std::vector<GraphicPackChoice> Choices(const GraphicPack2& pack)
		{
			std::vector<std::string> order;
			auto categorized = pack.GetCategorizedPresets(order);
			std::vector<GraphicPackChoice> choices;
			for (const auto& category : order)
			{
				GraphicPackChoice choice;
				choice.category = category;
				for (const auto& preset : categorized[category])
				{
					if (!preset->visible)
						continue;
					if (preset->active)
						choice.active = (int)choice.presets.size();
					choice.presets.push_back(preset->name);
				}
				if (!choice.presets.empty())
					choices.push_back(std::move(choice));
			}
			return choices;
		}
	}

	std::vector<GraphicPackInfo> ListGraphicPacks(uint64_t titleId)
	{
		std::vector<GraphicPackInfo> result;
		for (const auto& pack : PacksOf(titleId))
		{
			GraphicPackInfo info;
			// "Game name/Mods/FPS++": the last part names the pack, the middle its folder
			std::vector<std::string> parts;
			boost::split(parts, pack->GetVirtualPath(), boost::is_any_of("/"));
			info.name = pack->HasName() ? pack->GetName() : parts.back();
			if (parts.size() > 2)
				info.folder = boost::join(std::vector<std::string>(parts.begin() + 1, parts.end() - 1), " / ");
			info.description = pack->GetDescription();
			info.enabled = pack->IsEnabled();
			info.choices = Choices(*pack);
			result.push_back(std::move(info));
		}
		return result;
	}

	int EnabledGraphicPackCount(uint64_t titleId)
	{
		int count = 0;
		for (const auto& pack : PacksOf(titleId))
			count += pack->IsEnabled();
		return count;
	}

	bool ToggleGraphicPack(uint64_t titleId, size_t index)
	{
		const auto packs = PacksOf(titleId);
		if (index >= packs.size())
			return false;
		const auto& pack = packs[index];
		pack->SetEnabled(!pack->IsEnabled());
		SaveState();
		ps5log::Line("[packs] {} {}", pack->GetVirtualPath(), pack->IsEnabled() ? "on" : "off");
		return pack->IsEnabled();
	}

	void SetGraphicPackPreset(uint64_t titleId, size_t index, const std::string& category, const std::string& preset)
	{
		const auto packs = PacksOf(titleId);
		if (index >= packs.size())
			return;
		const auto& pack = packs[index];
		// as GraphicPacksWindow2::OnActivePresetChanged: the other categories follow its conditions
		pack->SetActivePreset(category, preset);
		pack->SetEnabled(true);
		SaveState();
		ps5log::Line("[packs] {}: {} {}", pack->GetVirtualPath(), category.empty() ? "preset" : category, preset);
	}
}
