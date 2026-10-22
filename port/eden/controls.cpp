// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the Switch side's buttons and system language, as Eden takes them (eden.h)
#include "eden.h"
#include "../app/lang.h"
#include "../azahar/controls.h"

#include <fmt/format.h>

#include <algorithm>
#include <iterator>

namespace ps5eden
{
	namespace
	{
		using ps5emu::PadInput;

		// the Switch's buttons: the settings' key (Eden's own name, button_mapping.h's kGameButtonKeys),
		// the launcher's label, and ProsperoEden's default (its kDefaultMapping)
		struct SwitchButton
		{
			const char* key;
			const char* label;
			PadInput defaultInput;
		};
		constexpr SwitchButton kButtons[] = {
			{"a", "A", PadInput::Circle},
			{"b", "B", PadInput::Cross},
			{"x", "X", PadInput::Triangle},
			{"y", "Y", PadInput::Square},
			{"l", "L", PadInput::L1},
			{"r", "R", PadInput::R1},
			{"zl", "ZL", PadInput::L2},
			{"zr", "ZR", PadInput::R2},
			// the labels the launcher translates already (frontend/actions.cpp's ButtonName)
			{"plus", "+ (Start)", PadInput::Options},
			{"minus", "- (Select)", PadInput::TouchPad},
			{"left_stick", "Left stick click", PadInput::L3},
			{"right_stick", "Right stick click", PadInput::R3},
		};
		constexpr size_t kButtonCount = std::size(kButtons);

		// a DualSense button as Eden names it (button_mapping.h's kPadButtonKeys); null for one Eden does
		// not map (the D-pad, the sticks)
		const char* PadKey(PadInput input)
		{
			switch (input)
			{
			case PadInput::Cross: return "cross";
			case PadInput::Circle: return "circle";
			case PadInput::Square: return "square";
			case PadInput::Triangle: return "triangle";
			case PadInput::L1: return "l1";
			case PadInput::R1: return "r1";
			case PadInput::L2: return "l2";
			case PadInput::R2: return "r2";
			case PadInput::L3: return "l3";
			case PadInput::R3: return "r3";
			case PadInput::Options: return "options";
			case PadInput::Create: return "create";
			case PadInput::TouchPad: return "touchpad";
			default: return nullptr;
			}
		}

		PadInput MappedInput(const ps5settings::N3ds& nx, size_t index)
		{
			const auto it = nx.buttons.find(kButtons[index].key);
			if (it == nx.buttons.end())
				return kButtons[index].defaultInput;
			for (int i = (int)PadInput::Cross; i <= (int)PadInput::TouchPad; i++)
				if (it->second == ps5azahar::InputName((PadInput)i) && PadKey((PadInput)i))
					return (PadInput)i;
			return kButtons[index].defaultInput;
		}

		void Store(ps5settings::N3ds& nx, size_t index, PadInput input)
		{
			if (input == kButtons[index].defaultInput)
				nx.buttons.erase(kButtons[index].key);
			else
				nx.buttons[kButtons[index].key] = ps5azahar::InputName(input);
		}
	}

	bool Mappable(PadInput input)
	{
		return PadKey(input) != nullptr;
	}

	std::vector<ps5emu::ButtonMapping> ListMappings(const ps5settings::N3ds& nx)
	{
		std::vector<ps5emu::ButtonMapping> mappings;
		for (size_t i = 0; i < kButtonCount; i++)
			mappings.push_back({kButtons[i].label, ps5azahar::InputName(MappedInput(nx, i))});
		return mappings;
	}

	void SetMapping(ps5settings::N3ds& nx, size_t index, PadInput input)
	{
		if (index >= kButtonCount)
			return;
		if (input == PadInput::None || !Mappable(input))
			input = kButtons[index].defaultInput;
		// the button that had this input takes the one this button had, so they swap
		const PadInput previous = MappedInput(nx, index);
		if (input != previous)
			for (size_t other = 0; other < kButtonCount; other++)
				if (other != index && MappedInput(nx, other) == input)
					Store(nx, other, previous);
		Store(nx, index, input);
	}

	std::string MappingValue(const ps5settings::N3ds& nx)
	{
		std::string value;
		for (size_t i = 0; i < kButtonCount; i++)
			value += fmt::format("{}{}:{}", i ? "," : "", kButtons[i].key, PadKey(MappedInput(nx, i)));
		return value;
	}

	const Language kLanguages[18] = {{"en-US", "English (US)"}, {"en-GB", "English (UK)"}, {"fr", "Français"},
		{"fr-CA", "Français (Canada)"}, {"de", "Deutsch"}, {"it", "Italiano"}, {"es", "Español"}, {"es-419", "Español (Latinoamérica)"},
		{"pt", "Português"}, {"pt-BR", "Português (Brasil)"}, {"nl", "Nederlands"}, {"ru", "Русский"}, {"pl", "Polski"}, {"ja", "日本語"},
		{"ko", "한국어"}, {"zh-Hans", "简体中文"}, {"zh-Hant", "繁體中文"}, {"th", "ไทย"}};

	int LanguageFor(const ps5settings::N3ds& nx)
	{
		if (nx.nxLanguage >= 0 && nx.nxLanguage < (int)std::size(kLanguages))
			return nx.nxLanguage;
		// the menus' language, where the Switch has it (Portugal's Portuguese is the Switch's Portuguese)
		const std::string code = ps5lang::Current() == "pt-PT" ? std::string("pt") : ps5lang::Current();
		for (size_t i = 0; i < std::size(kLanguages); i++)
			if (code == kLanguages[i].key)
				return (int)i;
		return 0;
	}
}
