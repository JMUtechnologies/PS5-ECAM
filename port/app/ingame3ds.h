// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the in-game menu over a 3DS game, a panel down the left as the Wii U's (ingame.h,
// side_menu.h) in Azahar's gold (menu_canvas.h), with the game's box art at its top. It is drawn with Cemu's ImGui and its Vulkan backend into Azahar's frames,
// through the hook patches/azahar adds to Azahar's renderer; the game's loop (port/azahar/core.cpp)
// applies what it changes. Both sides include this header, so it names no Cemu or Azahar type.
//
// A DS game on the 3DS side (melonDS: port/melonds) has the same menu, drawn into the frames of its
// screens (port/melonds/screens.cpp), with the DS's rows where the 3DS's would not apply: a screen
// filter for the internal resolution and texture filter, the lid for motion controls, no CPU clock
// and no amiibo.

#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include "lang.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace ps5ingame3ds
{
	// What the menu shows and changes
	struct Settings
	{
		int layout = 2;			  // ps5azahar::Layout
		bool swapScreens = false; // the bottom screen where the top one is
		int resolution = 6;		  // times the 3DS's 400x240
		int textureFilter = 0;	  // None, Anime4K, Bicubic, ScaleForce, xBRZ, MMPX
		int volume = 100;		  // percent
		bool performance = false; // the frame rate and speed in a corner
		int cpuClock = 100;		  // the emulated CPU's speed, percent of the 3DS's
		int speedLimit = 100;	  // the emulation's top speed, percent of the 3DS's; 0: none (for this game only)
		bool motion = true;
		int deadzone = 15;		  // percent
		bool aOnCircle = true;	  // A on Circle and B on Cross, where the 3DS has them
		int border = 0;			  // kBorderNames
		bool frameGeneration = false; // the picture between each two frames of a game at 30 fps (port/cemu/FrameGenPS5.h)
		// the DS's (Console::Nds)
		int screenFilter = 0;	  // how its screens are scaled to the TV: kScreenFilters
		int rotation = 0;		  // the console turned this many quarter turns clockwise, the 3DS's too: kRotations
		bool screenGap = false;	  // a gap between the two screens, as the console's hinge makes (the 3DS's too)
		bool hiddenTouch = false; // the touchpad touches the bottom screen while only the top one shows
		bool lidClosed = false;	  // the DS closed, for the games that ask (for this game only)
		// a libretro core's (Console::Retro): the picture's size on the TV and its shape (kPictureSizes, kPictureShapes)
		int pictureSize = 0;
		int pictureShape = 0;
		int controller = 0; // the Buttons page's Controller row: SetControllers' choices, 0 Auto (for this game only)
		// the menu's Settings for this game only: on, the game keeps settings of its own and what the menu
		// changes is its own (ps5settings::SetGameOwn); off, it follows its system's
		bool gameOnly = false;
		// the console's buttons on the DualSense where not the default (ps5settings::N3ds::buttons): the menu's
		// Buttons change them, and the A and B row's swap is in them
		std::map<std::string, std::string> buttons;
	};

	// Which console the game is, whose rows the menu shows
	enum class Console
	{
		N3ds,  // Azahar's
		Nds,   // melonDS's
		Retro, // a libretro core's (port/libretro): one screen, the DS's rows that apply to it, cheats where its core takes them
	};
	// tr: how the DS's screens are scaled to the TV: the filters' names
	// the last two, a tube TV's, for the libretro consoles only (port/melonds/shaders/screens.frag)
	constexpr const char* kScreenFilters[] = {ps5lang::TrMark("Sharp"), ps5lang::TrMark("Smooth"), ps5lang::TrMark("Square pixels"),
		ps5lang::TrMark("CRT"), ps5lang::TrMark("Curved CRT")};
	constexpr int kScreenFilterCount = (int)std::size(kScreenFilters);
	constexpr int kDsScreenFilterCount = 3;
	// tr: the DS's (and the 3DS's) two screens turned together, a quarter turn at a time, for the games held on their side:
	// upright, a quarter turn clockwise, upside down, a quarter turn counterclockwise
	constexpr const char* kRotations[] = {ps5lang::TrMark("Upright"), ps5lang::TrMark("90° right"), ps5lang::TrMark("Upside down"),
		ps5lang::TrMark("90° left")};
	constexpr int kRotationCount = (int)std::size(kRotations);
	// tr: how a libretro game's picture fills the TV: as large as fits in its shape, in whole multiples of its pixels, or
	// the whole screen
	constexpr const char* kPictureSizes[] = {ps5lang::TrMark("Fit"), ps5lang::TrMark("Whole pixels"), ps5lang::TrMark("Fill screen")};
	constexpr int kPictureSizeCount = (int)std::size(kPictureSizes);
	// tr: a libretro game's picture's shape: the console's own, a 4:3 TV's, each pixel square, a 16:9 TV's
	constexpr const char* kPictureShapes[] = {ps5lang::TrMark("Original"), "4:3", ps5lang::TrMark("Square pixels"), "16:9"};
	constexpr int kPictureShapeCount = (int)std::size(kPictureShapes);

	// Azahar's frame being recorded (its renderer's FrontendOverlayTarget): before its render pass
	// begins, then in it
	struct Target
	{
		VkInstance instance;
		VkPhysicalDevice physicalDevice;
		VkDevice device;
		uint32_t queueFamily;
		VkQueue queue;
		VkRenderPass renderPass;
		uint32_t imageCount;
		VkCommandBuffer commandBuffer;
		uint32_t width, height;
		bool insideRenderPass;
		// which of Azahar's renderers (a loaded save state makes it again, device and all): a new
		// device can have the old one's handle, so it is told apart by this
		uint64_t generation;
	};

	// When the game starts: its name and title ID, the settings it starts with, and its console (a
	// libretro core's: its system's name, for the menu's badge, and its colour, 0xRRGGBB: ps5retro::kSystems').
	void Start(const std::string& name, uint64_t titleId, const Settings& settings, Console console = Console::N3ds,
		const std::string& system = "", uint32_t colour = 0x5aa9ff);
	// The menu's top, once: GameTDB's facts on a line ("Nintendo  /  2015"), and the TGA of the
	// game's box art or icon (empty: none).
	void SetGame(const std::string& details, const std::string& coverPath);
	// The loading screen (loading_screen.h, every side's), for a 3DS game: while Azahar makes the game's
	// cached shaders ready before its first step, port/azahar/core.cpp presents a frame now and then
	// and the screen is drawn in it, saying what Azahar is doing, done of of (of 0: not counted).
	// EndLoading as the game starts: the screen fades over its first frames. Start and SetGame first.
	enum class LoadingStep
	{
		Loading,
		Shaders,
		Pipelines,
		Starting,
	};
	void SetLoading(LoadingStep step, size_t done = 0, size_t of = 0);
	void EndLoading();

	// Touchpad click + Options.
	void ToggleMenu();
	bool MenuOpen();

	// For the game's loop: a change made in the menu since the last call (the settings as they are
	// now), and whether the library was chosen.
	bool TakeChanges(Settings& settings);
	bool TakeLibraryRequest();
	// The performance overlay's numbers, about once a second: frames per second, speed in percent,
	// and where a frame's time goes (a line of text; empty: none).
	void SetPerformance(double fps, double speed, const std::string& breakdown);

	// Save states (the menu's Save states page), in slots 1 to kStateSlots. From the game's loop:
	// each slot's time (empty: nothing saved) and what came of the last save or load; for it, a save
	// (load false) or load chosen in the menu, once.
	constexpr int kStateSlots = 5;
	void SetStateSlots(const std::vector<std::string>& times);
	void SetStateMessage(const std::string& message);
	bool TakeStateRequest(bool& load, int& slot);
	// The save state shortcuts (ps5pad::Shortcut::SaveState, LoadState): a save to the menu's slot or
	// a load from it, asked for as the menu's own rows ask (TakeStateRequest then has it); false, with
	// why, when the slot to load is empty
	bool AskState(bool load, std::string& why);
	// Undo the last load: the game's loop keeps the game as it was before each load, says so here
	// (the Save states page then has the row), and takes the row's request, once
	void SetUndoAvailable(bool available);
	bool TakeUndoRequest();

	// The same page's amiibo and cheats. From the game's loop: the amiibo files (names in
	// /data/ps5cemu/amiibo), the game's cheats (name, on) and what came of the last request; for it,
	// what the menu asked for, once.
	void SetExtras(const std::vector<std::string>& amiibo, const std::vector<std::pair<std::string, bool>>& cheats);
	void SetExtrasMessage(const std::string& message);
	// A libretro core's game: whether the page has Cheats (its core takes them); after Start
	void SetCheatsShown(bool shown);
	struct ExtrasRequest
	{
		enum Kind
		{
			None,
			Amiibo,		  // scan amiibo[index]
			RemoveAmiibo,
			Cheat,		  // turn cheats[index] on or off
		} kind = None;
		int index = 0;
	};
	bool TakeExtrasRequest(ExtrasRequest& request);

	// The border: artwork drawn around the screens, never over them, with a frame round each (a soft
	// shadow, a hairline, a ring for Shell). From the game's loop: the theme with its picture (RGBA,
	// top row first; empty for none), and where the screens are on a width x height picture.
	// tr: the 3DS borders' names (artwork around the screens)
	constexpr const char* kBorderNames[] = {ps5lang::TrMarkC("border", "None"), ps5lang::TrMarkC("border", "Midnight"),
		ps5lang::TrMarkC("border", "Waves"), ps5lang::TrMarkC("border", "Aurora"), ps5lang::TrMarkC("border", "Shell"), "Playtendoh"};
	constexpr int kBorderCount = (int)std::size(kBorderNames);
	struct ScreenRect
	{
		float left, top, right, bottom;
	};
	// RetroAchievements (app/achievements.h), a DS game's Achievements category, while someone is signed
	// in: a line saying how many are earned (or why there are none), and each achievement, the earned
	// first. From the game's loop; shown false takes the category away.
	struct Achievement
	{
		std::string title, description;
		uint32_t points = 0;
		bool unlocked = false;
		std::string when; // when it was earned, as the menu says it
	};
	void SetAchievements(bool shown, const std::string& summary, const std::vector<Achievement>& achievements);

	// A libretro game's emulator options (its core's, port/libretro/session.h), its Emulator category: each
	// option's key, label, its values' labels and the one in use, and what it is for. From the game's loop;
	// TakeCoreOption gives a change made in the menu, once: the key and its new value's index.
	struct CoreOption
	{
		std::string key, label, help;
		std::vector<std::string> values;
		int index = 0;
	};
	void SetCoreOptions(const std::vector<CoreOption>& options);
	bool TakeCoreOption(std::string& key, int& index);
	// The Reset tile (a console that can be reset): asked for once
	bool TakeResetRequest();
	// A libretro game's special controllers (port/libretro/devices.h), on its Buttons page: the Controller
	// row's choices (English; the first is Auto, the second the pad; none: no row), what Auto plugged in
	// (English; empty: the pad), what the DualSense does with the controller in use (what, and which input,
	// English), and whether the game reads the DualSense's motion (a row to level it, asked for once). From
	// the game's loop.
	struct Controllers
	{
		std::vector<std::string> choices;
		std::string found;
		std::vector<std::pair<std::string, std::string>> mapping;
		bool motion = false;
	};
	void SetControllers(const Controllers& controllers);
	bool TakeLevelRequest();

	void SetBorder(int theme, std::vector<uint8_t> rgba, int width, int height);
	// The theme's picture from the app's assets/borders (tools/render-borders.py), as SetBorder takes it
	void LoadBorder(int theme);
	void SetScreens(const std::vector<ScreenRect>& screens, float width, float height);

	// For Azahar's renderer, twice a frame, where it records its commands.
	void Record(const Target& target);

	// A game asking for text (Azahar's software keyboard): the port's keyboard, drawn over the game
	// like the menu, typed with the D-pad and Cross.
	struct KeyboardRequest
	{
		std::string hint;				  // what the game asks for; may be empty
		int maxLength = 0;				  // 0: no limit
		std::vector<std::string> buttons; // the game's buttons, left to right; the last one confirms
	};
	// From Azahar's emulation thread.
	void OpenKeyboard(const KeyboardRequest& request);
	bool KeyboardOpen();
	// For the game's loop: the text typed and the button chosen (an index into buttons), once.
	bool TakeKeyboardResult(std::string& text, int& button);
	// The game refused what was typed (too long, empty...): the keyboard opens again, saying why.
	void KeyboardError(const std::string& message);
}
