// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: one game's graphic packs for the launcher and the in-game menu (emulator.h).
//
// The packs are the ones Cemu loaded at start (GraphicPack2::LoadAll), and the choices are saved
// as Cemu's Graphic Packs window saves them (GraphicPacksWindow2::SaveStateToConfig), so this file
// keeps Cemu's MPL-2.0 licence. The launcher's changes apply to the next game started; the in-game
// menu's apply while the game runs, as GraphicPacksWindow2::OnTreeChoiceChanged and
// OnActivePresetChanged apply them.

#include "emulator.h"
#include "../ps5/log.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "Cafe/HW/Latte/Core/LatteAsyncCommands.h"
#include "config/CemuConfig.h"

#include <mutex>

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

	namespace
	{
		struct PackRequest
		{
			size_t index;
			bool toggle;
			std::string category, preset;
		};
		std::mutex s_requestMutex;
		std::vector<PackRequest> s_requests; // under s_requestMutex
		bool s_listRequested = false;		 // under s_requestMutex
		RunningGraphicPacks s_running;		 // under s_requestMutex
		std::vector<bool> s_nextStart;		 // the main thread's

		// GraphicPacksWindow2::DeleteShadersFromRuntimeCache: the GPU thread drops the pack's
		// shaders, and builds them again as they are drawn
		void DropShaders(const PackPtr& pack)
		{
			for (const auto& shader : pack->GetCustomShaders())
			{
				LatteConst::ShaderType type = LatteConst::ShaderType::Pixel;
				if (shader.type == GraphicPack2::GP_SHADER_TYPE::VERTEX)
					type = LatteConst::ShaderType::Vertex;
				else if (shader.type == GraphicPack2::GP_SHADER_TYPE::GEOMETRY)
					type = LatteConst::ShaderType::Geometry;
				LatteAsyncCommands_queueDeleteShader(shader.shader_base_hash, shader.shader_aux_hash, type);
			}
		}

		// GraphicPacksWindow2::ReloadPack
		void ReloadPack(const PackPtr& pack)
		{
			if ((pack->HasShaders() || pack->HasPatches() || pack->HasCustomVSyncFrequency()) && pack->Reload())
				DropShaders(pack);
		}

		// One request, made as Cemu's window makes it while the game runs; false when the game only
		// shows it from its next start
		bool Apply(uint64_t titleId, const PackRequest& request)
		{
			const auto packs = PacksOf(titleId);
			if (request.index >= packs.size())
				return true;
			const PackPtr& pack = packs[request.index];
			const bool running = CafeSystem::IsTitleRunning() && pack->ContainsTitleId(CafeSystem::GetForegroundTitleId());
			if (request.toggle)
			{
				const bool on = !pack->IsEnabled();
				pack->SetEnabled(on);
				const bool restart = pack->RequiresRestart(true, false);
				if (running)
				{
					if (on)
					{
						GraphicPack2::ActivateGraphicPack(pack);
						if (!restart)
							ReloadPack(pack);
					}
					else
					{
						if (!restart)
							DropShaders(pack);
						GraphicPack2::DeactivateGraphicPack(pack);
					}
				}
				SaveState();
				ps5log::Line("[packs] in game: {} {}{}", pack->GetVirtualPath(), on ? "on" : "off", restart ? " (from the next start)" : "");
				return !restart;
			}
			const bool wasOn = pack->IsEnabled();
			pack->SetActivePreset(request.category, request.preset);
			if (!wasOn)
			{
				// a preset chosen turns the pack on, as in the launcher
				pack->SetEnabled(true);
				if (running)
					GraphicPack2::ActivateGraphicPack(pack);
			}
			const bool restart = pack->RequiresRestart(false, true);
			if (running && !restart)
				ReloadPack(pack);
			SaveState();
			ps5log::Line("[packs] in game: {}: {} {}{}", pack->GetVirtualPath(), request.category.empty() ? "preset" : request.category, request.preset,
				restart ? " (from the next start)" : "");
			return !restart;
		}
	}

	void RequestGraphicPackToggle(size_t index)
	{
		std::lock_guard lock(s_requestMutex);
		s_requests.push_back({index, true, {}, {}});
	}

	void RequestGraphicPackPreset(size_t index, const std::string& category, const std::string& preset)
	{
		std::lock_guard lock(s_requestMutex);
		s_requests.push_back({index, false, category, preset});
	}

	void RequestGraphicPackList()
	{
		std::lock_guard lock(s_requestMutex);
		s_listRequested = true;
	}

	RunningGraphicPacks GetRunningGraphicPacks()
	{
		std::lock_guard lock(s_requestMutex);
		return s_running;
	}

	void ServiceGraphicPackRequests()
	{
		std::vector<PackRequest> requests;
		bool list = false;
		{
			std::lock_guard lock(s_requestMutex);
			requests.swap(s_requests);
			list = s_listRequested || !requests.empty();
			s_listRequested = false;
		}
		if (!list || !CafeSystem::IsTitleRunning())
			return;
		const uint64_t titleId = CafeSystem::GetForegroundTitleId();
		const size_t count = PacksOf(titleId).size();
		s_nextStart.resize(count, false);
		for (const auto& request : requests)
			if (!Apply(titleId, request) && request.index < count)
				s_nextStart[request.index] = true;
		RunningGraphicPacks running;
		running.packs = ListGraphicPacks(titleId);
		running.nextStart = s_nextStart;
		running.nextStart.resize(running.packs.size(), false);
		std::lock_guard lock(s_requestMutex);
		running.version = s_running.version + 1;
		s_running = std::move(running);
	}

	void ReloadGraphicPacks()
	{
		// as Cemu's Graphic Packs window does after its download
		GraphicPack2::ClearGraphicPacks();
		GraphicPack2::LoadAll();
		ps5log::Line("[packs] {} graphic packs loaded", GraphicPack2::GetGraphicPacks().size());
	}
}
