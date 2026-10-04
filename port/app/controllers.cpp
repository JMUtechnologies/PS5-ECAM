// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Cemu's controller settings for the launcher (emulator.h).
//
// Each player has an emulated controller (GamePad, Pro Controller, Classic Controller, Wii Remote)
// fed by that player's DualSense (cemu/PS5PadController.h). What Cemu's Input Settings window sets
// for it is set here the same way, on the emulated controller and the DualSense under it, and saved
// to the player's profile in controllerProfiles/, which Cemu loads at start.

#include "emulator.h"
#include "../ps5/log.h"
#include "PS5PadController.h"

#include "input/InputManager.h"
#include "input/emulated/ClassicController.h"
#include "input/emulated/ProController.h"
#include "input/emulated/VPADController.h"
#include "input/emulated/WiimoteController.h"

#include <span>

namespace ps5emu
{
	namespace
	{
		struct Button
		{
			uint64 id; // the emulated controller's mapping
			const char* label;
		};

		constexpr Button kGamePad[] = {
			{VPADController::kButtonId_A, "A"}, {VPADController::kButtonId_B, "B"},
			{VPADController::kButtonId_X, "X"}, {VPADController::kButtonId_Y, "Y"},
			{VPADController::kButtonId_L, "L"}, {VPADController::kButtonId_R, "R"},
			{VPADController::kButtonId_ZL, "ZL"}, {VPADController::kButtonId_ZR, "ZR"},
			{VPADController::kButtonId_Plus, "+ (Start)"}, {VPADController::kButtonId_Minus, "- (Select)"},
			{VPADController::kButtonId_Up, "D-pad up"}, {VPADController::kButtonId_Down, "D-pad down"},
			{VPADController::kButtonId_Left, "D-pad left"}, {VPADController::kButtonId_Right, "D-pad right"},
			{VPADController::kButtonId_StickL_Up, "Left stick up"}, {VPADController::kButtonId_StickL_Down, "Left stick down"},
			{VPADController::kButtonId_StickL_Left, "Left stick left"}, {VPADController::kButtonId_StickL_Right, "Left stick right"},
			{VPADController::kButtonId_StickL, "Left stick click"},
			{VPADController::kButtonId_StickR_Up, "Right stick up"}, {VPADController::kButtonId_StickR_Down, "Right stick down"},
			{VPADController::kButtonId_StickR_Left, "Right stick left"}, {VPADController::kButtonId_StickR_Right, "Right stick right"},
			{VPADController::kButtonId_StickR, "Right stick click"},
			{VPADController::kButtonId_Home, "Home"},
			{VPADController::kButtonId_Mic, "Blow into the mic"},
			{VPADController::kButtonId_Screen, "Show the GamePad's screen"},
		};

		constexpr Button kPro[] = {
			{ProController::kButtonId_A, "A"}, {ProController::kButtonId_B, "B"},
			{ProController::kButtonId_X, "X"}, {ProController::kButtonId_Y, "Y"},
			{ProController::kButtonId_L, "L"}, {ProController::kButtonId_R, "R"},
			{ProController::kButtonId_ZL, "ZL"}, {ProController::kButtonId_ZR, "ZR"},
			{ProController::kButtonId_Plus, "+ (Start)"}, {ProController::kButtonId_Minus, "- (Select)"},
			{ProController::kButtonId_Up, "D-pad up"}, {ProController::kButtonId_Down, "D-pad down"},
			{ProController::kButtonId_Left, "D-pad left"}, {ProController::kButtonId_Right, "D-pad right"},
			{ProController::kButtonId_StickL_Up, "Left stick up"}, {ProController::kButtonId_StickL_Down, "Left stick down"},
			{ProController::kButtonId_StickL_Left, "Left stick left"}, {ProController::kButtonId_StickL_Right, "Left stick right"},
			{ProController::kButtonId_StickL, "Left stick click"},
			{ProController::kButtonId_StickR_Up, "Right stick up"}, {ProController::kButtonId_StickR_Down, "Right stick down"},
			{ProController::kButtonId_StickR_Left, "Right stick left"}, {ProController::kButtonId_StickR_Right, "Right stick right"},
			{ProController::kButtonId_StickR, "Right stick click"},
			{ProController::kButtonId_Home, "Home"},
		};

		constexpr Button kClassic[] = {
			{ClassicController::kButtonId_A, "A"}, {ClassicController::kButtonId_B, "B"},
			{ClassicController::kButtonId_X, "X"}, {ClassicController::kButtonId_Y, "Y"},
			{ClassicController::kButtonId_L, "L"}, {ClassicController::kButtonId_R, "R"},
			{ClassicController::kButtonId_ZL, "ZL"}, {ClassicController::kButtonId_ZR, "ZR"},
			{ClassicController::kButtonId_Plus, "+ (Start)"}, {ClassicController::kButtonId_Minus, "- (Select)"},
			{ClassicController::kButtonId_Up, "D-pad up"}, {ClassicController::kButtonId_Down, "D-pad down"},
			{ClassicController::kButtonId_Left, "D-pad left"}, {ClassicController::kButtonId_Right, "D-pad right"},
			{ClassicController::kButtonId_StickL_Up, "Left stick up"}, {ClassicController::kButtonId_StickL_Down, "Left stick down"},
			{ClassicController::kButtonId_StickL_Left, "Left stick left"}, {ClassicController::kButtonId_StickL_Right, "Left stick right"},
			{ClassicController::kButtonId_StickR_Up, "Right stick up"}, {ClassicController::kButtonId_StickR_Down, "Right stick down"},
			{ClassicController::kButtonId_StickR_Left, "Right stick left"}, {ClassicController::kButtonId_StickR_Right, "Right stick right"},
			{ClassicController::kButtonId_Home, "Home"},
		};

		constexpr Button kWiimote[] = {
			{WiimoteController::kButtonId_A, "A"}, {WiimoteController::kButtonId_B, "B"},
			{WiimoteController::kButtonId_1, "1"}, {WiimoteController::kButtonId_2, "2"},
			{WiimoteController::kButtonId_Plus, "+"}, {WiimoteController::kButtonId_Minus, "-"},
			{WiimoteController::kButtonId_Up, "D-pad up"}, {WiimoteController::kButtonId_Down, "D-pad down"},
			{WiimoteController::kButtonId_Left, "D-pad left"}, {WiimoteController::kButtonId_Right, "D-pad right"},
			{WiimoteController::kButtonId_Home, "Home"},
		};

		constexpr Button kNunchuk[] = {
			{WiimoteController::kButtonId_A, "A"}, {WiimoteController::kButtonId_B, "B"},
			{WiimoteController::kButtonId_1, "1"}, {WiimoteController::kButtonId_2, "2"},
			{WiimoteController::kButtonId_Plus, "+"}, {WiimoteController::kButtonId_Minus, "-"},
			{WiimoteController::kButtonId_Up, "D-pad up"}, {WiimoteController::kButtonId_Down, "D-pad down"},
			{WiimoteController::kButtonId_Left, "D-pad left"}, {WiimoteController::kButtonId_Right, "D-pad right"},
			{WiimoteController::kButtonId_Nunchuck_C, "Nunchuk C"}, {WiimoteController::kButtonId_Nunchuck_Z, "Nunchuk Z"},
			{WiimoteController::kButtonId_Nunchuck_Up, "Nunchuk stick up"}, {WiimoteController::kButtonId_Nunchuck_Down, "Nunchuk stick down"},
			{WiimoteController::kButtonId_Nunchuck_Left, "Nunchuk stick left"}, {WiimoteController::kButtonId_Nunchuck_Right, "Nunchuk stick right"},
			{WiimoteController::kButtonId_Home, "Home"},
		};

		uint64 InputId(PadInput input)
		{
			switch (input)
			{
			case PadInput::Cross: return PS5PadController::kCross;
			case PadInput::Circle: return PS5PadController::kCircle;
			case PadInput::Square: return PS5PadController::kSquare;
			case PadInput::Triangle: return PS5PadController::kTriangle;
			case PadInput::L1: return PS5PadController::kL1;
			case PadInput::R1: return PS5PadController::kR1;
			case PadInput::L2: return kTriggerXP;
			case PadInput::R2: return kTriggerYP;
			case PadInput::L3: return PS5PadController::kL3;
			case PadInput::R3: return PS5PadController::kR3;
			case PadInput::Create: return PS5PadController::kCreate;
			case PadInput::Options: return PS5PadController::kOptions;
			case PadInput::Up: return PS5PadController::kDpadUp;
			case PadInput::Down: return PS5PadController::kDpadDown;
			case PadInput::Left: return PS5PadController::kDpadLeft;
			case PadInput::Right: return PS5PadController::kDpadRight;
			case PadInput::LeftStickUp: return kAxisYN;
			case PadInput::LeftStickDown: return kAxisYP;
			case PadInput::LeftStickLeft: return kAxisXN;
			case PadInput::LeftStickRight: return kAxisXP;
			case PadInput::RightStickUp: return kRotationYN;
			case PadInput::RightStickDown: return kRotationYP;
			case PadInput::RightStickLeft: return kRotationXN;
			case PadInput::RightStickRight: return kRotationXP;
			case PadInput::None: break;
			}
			return 0;
		}

		std::span<const Button> ButtonsOf(EmulatedType type)
		{
			switch (type)
			{
			case EmulatedType::GamePad: return kGamePad;
			case EmulatedType::Pro: return kPro;
			case EmulatedType::Classic: return kClassic;
			case EmulatedType::Wiimote: return kWiimote;
			case EmulatedType::Nunchuk: return kNunchuk;
			case EmulatedType::None: break;
			}
			return {};
		}

		EmulatedType TypeOf(const EmulatedControllerPtr& emulated)
		{
			if (!emulated)
				return EmulatedType::None;
			switch (emulated->type())
			{
			case EmulatedController::Type::VPAD: return EmulatedType::GamePad;
			case EmulatedController::Type::Pro: return EmulatedType::Pro;
			case EmulatedController::Type::Classic: return EmulatedType::Classic;
			case EmulatedController::Type::Wiimote:
			{
				const auto wiimote = std::dynamic_pointer_cast<WiimoteController>(emulated);
				return wiimote && wiimote->get_device_type() == kWAPDevFreestyle ? EmulatedType::Nunchuk : EmulatedType::Wiimote;
			}
			default: return EmulatedType::None;
			}
		}

		// The player's DualSense under the emulated controller.
		std::shared_ptr<ControllerBase> PadOf(const EmulatedControllerPtr& emulated)
		{
			if (!emulated)
				return {};
			for (const auto& controller : emulated->get_controllers())
				if (controller->api() == InputAPI::PS5Pad)
					return controller;
			return {};
		}

		void Save(int player)
		{
			if (!InputManager::instance().save((size_t)player))
				ps5log::Line("[controls] player {}'s profile could not be saved", player + 1);
		}

		// The default buttons: Cemu's for the GamePad and the Pro Controller (patches/cemu), and
		// the same layout for the others, which Cemu has none for on the DualSense.
		void DefaultMapping(const EmulatedControllerPtr& emulated, const std::shared_ptr<ControllerBase>& pad)
		{
			emulated->clear_mappings();
			std::vector<std::pair<uint64, PadInput>> mapping;
			switch (TypeOf(emulated))
			{
			case EmulatedType::GamePad:
			case EmulatedType::Pro:
				emulated->set_default_mapping(pad);
				return;
			case EmulatedType::Classic:
				mapping = {
					{ClassicController::kButtonId_A, PadInput::Circle}, {ClassicController::kButtonId_B, PadInput::Cross},
					{ClassicController::kButtonId_X, PadInput::Triangle}, {ClassicController::kButtonId_Y, PadInput::Square},
					{ClassicController::kButtonId_L, PadInput::L1}, {ClassicController::kButtonId_R, PadInput::R1},
					{ClassicController::kButtonId_ZL, PadInput::L2}, {ClassicController::kButtonId_ZR, PadInput::R2},
					{ClassicController::kButtonId_Plus, PadInput::Options}, {ClassicController::kButtonId_Minus, PadInput::Create},
					{ClassicController::kButtonId_Up, PadInput::Up}, {ClassicController::kButtonId_Down, PadInput::Down},
					{ClassicController::kButtonId_Left, PadInput::Left}, {ClassicController::kButtonId_Right, PadInput::Right},
					{ClassicController::kButtonId_StickL_Up, PadInput::LeftStickUp}, {ClassicController::kButtonId_StickL_Down, PadInput::LeftStickDown},
					{ClassicController::kButtonId_StickL_Left, PadInput::LeftStickLeft}, {ClassicController::kButtonId_StickL_Right, PadInput::LeftStickRight},
					{ClassicController::kButtonId_StickR_Up, PadInput::RightStickUp}, {ClassicController::kButtonId_StickR_Down, PadInput::RightStickDown},
					{ClassicController::kButtonId_StickR_Left, PadInput::RightStickLeft}, {ClassicController::kButtonId_StickR_Right, PadInput::RightStickRight},
				};
				break;
			case EmulatedType::Wiimote:
			case EmulatedType::Nunchuk:
				// 2 and 1 under the right thumb for a Wii Remote held sideways, B on the trigger
				mapping = {
					{WiimoteController::kButtonId_A, PadInput::Circle}, {WiimoteController::kButtonId_B, PadInput::R2},
					{WiimoteController::kButtonId_1, PadInput::Square}, {WiimoteController::kButtonId_2, PadInput::Cross},
					{WiimoteController::kButtonId_Plus, PadInput::Options}, {WiimoteController::kButtonId_Minus, PadInput::Create},
					{WiimoteController::kButtonId_Up, PadInput::Up}, {WiimoteController::kButtonId_Down, PadInput::Down},
					{WiimoteController::kButtonId_Left, PadInput::Left}, {WiimoteController::kButtonId_Right, PadInput::Right},
					{WiimoteController::kButtonId_Nunchuck_C, PadInput::L1}, {WiimoteController::kButtonId_Nunchuck_Z, PadInput::L2},
					{WiimoteController::kButtonId_Nunchuck_Up, PadInput::LeftStickUp}, {WiimoteController::kButtonId_Nunchuck_Down, PadInput::LeftStickDown},
					{WiimoteController::kButtonId_Nunchuck_Left, PadInput::LeftStickLeft}, {WiimoteController::kButtonId_Nunchuck_Right, PadInput::LeftStickRight},
				};
				break;
			case EmulatedType::None:
				return;
			}
			for (const auto& [button, input] : mapping)
				emulated->set_mapping(button, pad, InputId(input));
		}
	}

	PlayerControls GetPlayerControls(int player)
	{
		PlayerControls controls;
		const auto emulated = InputManager::instance().get_controller((size_t)player);
		controls.type = TypeOf(emulated);
		controls.hasMotion = controls.type == EmulatedType::GamePad || controls.type == EmulatedType::Wiimote ||
			controls.type == EmulatedType::Nunchuk;
		if (const auto pad = PadOf(emulated))
		{
			const auto settings = pad->get_settings();
			controls.connected = pad->is_connected();
			controls.motion = settings.motion;
			controls.rumble = (int)std::lround(settings.rumble * 100.0f);
			controls.leftDeadzone = (int)std::lround(settings.axis.deadzone * 100.0f);
			controls.rightDeadzone = (int)std::lround(settings.rotation.deadzone * 100.0f);
		}
		return controls;
	}

	void SetEmulatedType(int player, EmulatedType type)
	{
		auto& input = InputManager::instance();
		const auto previous = input.get_controller((size_t)player);
		if (TypeOf(previous) == type || type == EmulatedType::None)
			return;
		// the same DualSense, with its settings, under the new emulated controller
		auto pad = PadOf(previous);
		if (!pad)
			pad = std::make_shared<PS5PadController>(player);
		input.delete_controller((size_t)player);
		EmulatedController::Type cemuType = EmulatedController::Type::VPAD;
		switch (type)
		{
		case EmulatedType::Pro: cemuType = EmulatedController::Type::Pro; break;
		case EmulatedType::Classic: cemuType = EmulatedController::Type::Classic; break;
		case EmulatedType::Wiimote:
		case EmulatedType::Nunchuk: cemuType = EmulatedController::Type::Wiimote; break;
		default: break;
		}
		auto emulated = input.set_controller((size_t)player, cemuType, pad);
		if (!emulated)
		{
			ps5log::Line("[controls] player {} cannot have that controller; keeping the one it had", player + 1);
			if (previous)
				input.set_controller(previous);
			return;
		}
		if (const auto wiimote = std::dynamic_pointer_cast<WiimoteController>(emulated))
			wiimote->set_device_type(type == EmulatedType::Nunchuk ? kWAPDevFreestyle : kWAPDevCore);
		DefaultMapping(emulated, pad);
		Save(player);
		PS5PadController::SetWiiRemote(player, cemuType == EmulatedController::Type::Wiimote); // it points
		ps5log::Line("[controls] player {}: {}", player + 1, emulated->type_string());
	}

	void SetMotion(int player, bool enabled)
	{
		if (const auto pad = PadOf(InputManager::instance().get_controller((size_t)player)))
		{
			pad->set_use_motion(enabled);
			Save(player);
		}
	}

	void SetRumble(int player, int percent)
	{
		if (const auto pad = PadOf(InputManager::instance().get_controller((size_t)player)))
		{
			pad->set_rumble(std::clamp(percent, 0, 100) / 100.0f);
			Save(player);
		}
	}

	void SetDeadzones(int player, int left, int right)
	{
		if (const auto pad = PadOf(InputManager::instance().get_controller((size_t)player)))
		{
			auto settings = pad->get_settings();
			settings.axis.deadzone = std::clamp(left, 0, 90) / 100.0f;
			settings.rotation.deadzone = std::clamp(right, 0, 90) / 100.0f;
			pad->set_axis_settings(settings.axis);
			pad->set_rotation_settings(settings.rotation);
			Save(player);
		}
	}

	void ResetControls(int player)
	{
		const auto emulated = InputManager::instance().get_controller((size_t)player);
		const auto pad = PadOf(emulated);
		if (!pad)
			return;
		pad->set_settings({});
		DefaultMapping(emulated, pad);
		Save(player);
		ps5log::Line("[controls] player {}: defaults", player + 1);
	}

	std::vector<ButtonMapping> ListMappings(int player)
	{
		std::vector<ButtonMapping> mappings;
		const auto emulated = InputManager::instance().get_controller((size_t)player);
		for (const Button& button : ButtonsOf(TypeOf(emulated)))
			mappings.push_back({button.label, emulated->get_mapping_name(button.id)});
		return mappings;
	}

	void SetMapping(int player, size_t index, PadInput input)
	{
		const auto emulated = InputManager::instance().get_controller((size_t)player);
		const auto buttons = ButtonsOf(TypeOf(emulated));
		const auto pad = PadOf(emulated);
		if (index >= buttons.size() || !pad || input == PadInput::None)
			return;
		emulated->set_mapping(buttons[index].id, pad, InputId(input));
		Save(player);
		ps5log::Line("[controls] player {}: {} is {}", player + 1, buttons[index].label, emulated->get_mapping_name(buttons[index].id));
	}

	void ClearMapping(int player, size_t index)
	{
		const auto emulated = InputManager::instance().get_controller((size_t)player);
		const auto buttons = ButtonsOf(TypeOf(emulated));
		if (index >= buttons.size())
			return;
		emulated->delete_mapping(buttons[index].id);
		Save(player);
	}
}
