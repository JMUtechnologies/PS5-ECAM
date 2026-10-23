// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.h"
#include "tokens.h"

#include <rapidjson/document.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ui
{
	namespace
	{
		using namespace tokens;

		// tokens.json's colours: the name a theme sets each by, the token the kit draws with, and its value
		struct Slot
		{
			const char* name;
			uint32_t* token;
			uint32_t standard;
		};
		const Slot kSlots[] = {
			{"ink-0", &kInk0, kInk0Default}, {"ink-1", &kInk1, kInk1Default}, {"ink-2", &kInk2, kInk2Default},
			{"glass", &kGlass, kGlassDefault}, {"glass-2", &kGlass2, kGlass2Default}, {"glass-edge", &kGlassEdge, kGlassEdgeDefault},
			{"line", &kLine, kLineDefault}, {"scrim", &kScrim, kScrimDefault}, {"text", &kText, kTextDefault},
			{"text-2", &kText2, kText2Default}, {"text-3", &kText3, kText3Default}, {"wiiu", &kWiiu, kWiiuDefault},
			{"wiiu-strong", &kWiiuStrong, kWiiuStrongDefault}, {"wiiu-ink", &kWiiuInk, kWiiuInkDefault}, {"n3ds", &kN3ds, kN3dsDefault},
			{"n3ds-strong", &kN3dsStrong, kN3dsStrongDefault}, {"n3ds-ink", &kN3dsInk, kN3dsInkDefault}, {"nds", &kNds, kNdsDefault},
			{"nds-strong", &kNdsStrong, kNdsStrongDefault}, {"nds-ink", &kNdsInk, kNdsInkDefault}, {"nx", &kNx, kNxDefault},
			{"nx-strong", &kNxStrong, kNxStrongDefault}, {"nx-ink", &kNxInk, kNxInkDefault}, {"good", &kGood, kGoodDefault},
			{"warn", &kWarn, kWarnDefault}, {"bad", &kBad, kBadDefault}, {"focus", &kFocus, kFocusDefault},
		};

		// "#rrggbb" or "#rrggbbaa" as the kit's colour (RGBA, R in the lowest byte); false for anything else
		bool ParseColour(const std::string& text, uint32_t& colour)
		{
			if ((text.size() != 7 && text.size() != 9) || text[0] != '#')
				return false;
			uint32_t value = 0;
			for (size_t i = 1; i < text.size(); i++)
			{
				const char c = (char)std::tolower((unsigned char)text[i]);
				const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
				if (digit < 0)
					return false;
				value = value << 4 | (uint32_t)digit;
			}
			if (text.size() == 7)
				value = value << 8 | 0xff;
			const uint32_t r = value >> 24, g = value >> 16 & 0xff, b = value >> 8 & 0xff, a = value & 0xff;
			colour = r | g << 8 | b << 16 | a << 24;
			return true;
		}

		std::map<std::string, uint32_t> Colours(std::initializer_list<std::pair<const char*, const char*>> list)
		{
			std::map<std::string, uint32_t> colours;
			for (const auto& [name, text] : list)
			{
				uint32_t colour = 0;
				if (ParseColour(text, colour))
					colours[name] = colour;
			}
			return colours;
		}

		uint32_t Darker(uint32_t colour, float amount)
		{
			auto channel = [&](int shift) { return (uint32_t)(((colour >> shift) & 0xff) * (1.0f - amount)) << shift; };
			return channel(0) | channel(8) | channel(16) | (colour & 0xff000000);
		}

		// the ids themes had before 5.1, as they are now: Meyer Oscar's layout is everyone's, so its light
		// look is the default and its dark one Link Jack's; the experiments Phantom and Terminal are gone.
		// "light" names the default too (its id is empty), as a theme file's base
		std::string CurrentId(const std::string& id)
		{
			if (id == "light" || id == "meyer-oscar-light" || id == "phantom" || id == "terminal")
				return "";
			if (id == "meyer-oscar")
				return "link-jack";
			return id;
		}

		Motif MotifNamed(const std::string& name, Motif fallback)
		{
			return name == "side" ? Motif::Side : name == "none" ? Motif::None : name == "eden" ? Motif::Eden : fallback;
		}

		Theme s_current;
		std::atomic<float> s_corners{1.0f};
	}

	const std::vector<Theme>& BuiltInThemes()
	{
		static const std::vector<Theme> themes = [] {
			std::vector<Theme> list;
			// the default: Meyer Oscar Light's light ground with dark text (its id empty, as the default's is)
			Theme light;
			light.name = "Light";
			light.description = "A light background with dark text.";
			light.light = true;
			light.colours = Colours({{"ink-0", "#eef2f7"}, {"ink-1", "#ffffff"}, {"ink-2", "#e3e8f0"}, {"glass", "#ffffffb8"},
				{"glass-2", "#ffffffeb"}, {"glass-edge", "#0c16281a"}, {"line", "#0c16281a"}, {"scrim", "#0c1628a6"}, {"text", "#0d1526"},
				{"text-2", "#0d1526a8"}, {"text-3", "#0d152675"}, {"wiiu", "#1f73d6"}, {"n3ds", "#c4870f"}, {"nds", "#3f9e2a"}, {"nx", "#d4202e"},
				{"good", "#13a578"}, {"warn", "#c47a00"}, {"bad", "#d23c3c"}, {"focus", "#0d1526"}});
			list.push_back(light);

			Theme standard;
			standard.id = "link-jack";
			standard.name = "Link Jack";
			standard.description = "Frosted glass over each side's colours.";
			list.push_back(standard);

			// the deep blue panels the launcher of 3.0.0 had, flatter and squarer
			Theme classic;
			classic.id = "classic";
			classic.name = "Classic (3.0)";
			classic.description = "3.0.0's launcher: deep blue panels, flatter, a little squarer.";
			classic.colours = Colours({{"ink-0", "#06101e"}, {"ink-1", "#081426"}, {"ink-2", "#0e1f38"}, {"text", "#f3f7ff"}, {"text-2", "#b8c8de"},
				{"text-3", "#8a9db8"}, {"glass", "#ffffff10"}, {"glass-2", "#ffffff1a"}, {"glass-edge", "#ffffff1e"}, {"line", "#ffffff1e"},
				{"wiiu", "#5aa2f0"}, {"n3ds", "#e3b14a"}, {"nds", "#62c454"}, {"nx", "#e84a55"}, {"focus", "#f3f7ff"}});
			classic.corners = 0.6f;
			list.push_back(classic);

			Theme midnight;
			midnight.id = "midnight";
			midnight.name = "Midnight";
			midnight.description = "True black behind everything and nothing moving: for OLED screens.";
			midnight.colours = Colours({{"ink-0", "#000000"}, {"ink-1", "#05060a"}, {"ink-2", "#0b0d14"}, {"glass", "#ffffff0a"},
				{"glass-2", "#ffffff12"}, {"glass-edge", "#ffffff16"}, {"wiiu", "#4d9cff"}, {"n3ds", "#f0ad2e"}, {"nds", "#6fcf4a"}, {"nx", "#ff4b55"}});
			midnight.motif = Motif::None;
			list.push_back(midnight);

			// Eden: ProsperoEden 1.001's Canopy look (blackbearreloaded/ProsperoEden, GPL-3.0-or-later): frosted
			// glass tinted dark, a pale ring of light as the focus, rounder corners, the side's light drifting
			// where Canopy's sun is, with motes of light rising, each side's own motif faint behind
			Theme eden;
			eden.id = "eden";
			eden.name = "Eden";
			eden.description = "Dark frosted glass and rounder corners, the side's light drifting and motes of light rising.";
			eden.colours = Colours({{"ink-0", "#05080c"}, {"ink-1", "#0a1219"}, {"ink-2", "#121c26"}, {"glass", "#14202cb8"},
				{"glass-2", "#22344ac8"}, {"glass-edge", "#8fa4b85c"}, {"line", "#53667a52"}, {"scrim", "#010407cc"}, {"text", "#f0f5f8"},
				{"text-2", "#c5d0d9"}, {"text-3", "#8c9aa6"}, {"focus", "#e8f1ff"}, {"warn", "#e8b39a"}});
			eden.corners = 1.2f;
			eden.motif = Motif::Eden;
			list.push_back(eden);

			return list;
		}();
		return themes;
	}

	std::vector<Theme> UserThemes(const std::string& folder, std::string* error)
	{
		std::vector<Theme> themes;
		std::error_code ec;
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
		{
			std::string extension = entry.path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
			if (extension == ".json" && entry.is_regular_file(ec))
				files.push_back(entry.path());
		}
		std::sort(files.begin(), files.end());
		auto fail = [&](const std::filesystem::path& file, const std::string& why) {
			if (error)
				*error += file.filename().string() + ": " + why + "\n";
		};
		for (const auto& file : files)
		{
			std::ifstream in(file);
			std::stringstream text;
			text << in.rdbuf();
			rapidjson::Document json;
			if (json.Parse(text.str().c_str()).HasParseError() || !json.IsObject())
			{
				fail(file, "not JSON");
				continue;
			}
			Theme theme;
			// the base's look first, then the file's over it
			if (json.HasMember("base") && json["base"].IsString())
				for (const Theme& base : BuiltInThemes())
					if (base.id == CurrentId(json["base"].GetString()))
						theme = base;
			theme.id = "user:" + file.filename().string();
			theme.name = file.stem().string();
			theme.description = "Your own, from " + file.filename().string() + ".";
			if (json.HasMember("name") && json["name"].IsString() && json["name"].GetStringLength() > 0)
				theme.name = json["name"].GetString();
			// "colours", or "colors" as American English writes it
			const char* colours = json.HasMember("colours") ? "colours" : "colors";
			if (json.HasMember(colours) && json[colours].IsObject())
				for (const auto& member : json[colours].GetObject())
				{
					uint32_t colour = 0;
					const std::string name = member.name.GetString();
					const bool known = std::any_of(std::begin(kSlots), std::end(kSlots), [&](const Slot& slot) { return name == slot.name; });
					if (known && member.value.IsString() && ParseColour(member.value.GetString(), colour))
						theme.colours[name] = colour;
					else
						fail(file, "colour " + name + " left out");
				}
			if (json.HasMember("corners") && json["corners"].IsNumber())
				theme.corners = std::clamp((float)json["corners"].GetDouble(), 0.0f, 1.5f);
			if (json.HasMember("motif") && json["motif"].IsString())
				theme.motif = MotifNamed(json["motif"].GetString(), theme.motif);
			themes.push_back(std::move(theme));
		}
		return themes;
	}

	std::vector<Theme> AllThemes(const std::string& folder)
	{
		std::vector<Theme> themes = BuiltInThemes();
		for (Theme& theme : UserThemes(folder))
			themes.push_back(std::move(theme));
		return themes;
	}

	Theme FindTheme(const std::string& id, const std::string& folder)
	{
		for (const Theme& theme : BuiltInThemes())
			if (theme.id == CurrentId(id))
				return theme;
		if (id.rfind("user:", 0) == 0)
			for (Theme& theme : UserThemes(folder))
				if (theme.id == id)
					return theme;
		return BuiltInThemes().front();
	}

	void ApplyTheme(const Theme& theme)
	{
		for (const Slot& slot : kSlots)
		{
			const auto own = theme.colours.find(slot.name);
			*slot.token = own != theme.colours.end() ? own->second : slot.standard;
		}
		// a side's colour set alone: its stronger and inkier shades follow it
		for (const char* side : {"wiiu", "n3ds", "nds", "nx"})
		{
			const auto accent = theme.colours.find(side);
			if (accent == theme.colours.end())
				continue;
			for (const Slot& slot : kSlots)
			{
				const std::string name = slot.name;
				if (name == std::string(side) + "-strong" && !theme.colours.count(name))
					*slot.token = Darker(accent->second, 0.2f);
				else if (name == std::string(side) + "-ink" && !theme.colours.count(name))
					*slot.token = Darker(accent->second, 0.85f);
			}
		}
		s_corners = std::clamp(theme.corners, 0.0f, 1.5f);
		s_current = theme;
	}

	const Theme& CurrentTheme()
	{
		return s_current;
	}

	Motif CurrentMotif()
	{
		return s_current.motif;
	}

	bool LightTheme()
	{
		return s_current.light;
	}

	float CornerScale()
	{
		return s_corners.load(std::memory_order_relaxed);
	}
}
