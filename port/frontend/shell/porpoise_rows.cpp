// SPDX-License-Identifier: GPL-3.0-or-later
// The GameCube and Wii sides' settings: Porpoise-X's own (src/ui_app_settings.cpp, add_game_rows), in the
// app's words, on the pages the other sides have. Each is read from and written to Porpoise's files
// (port/retro/porpoise_settings.h): every game's in its settings.ini, which the two sides share (Porpoise
// plays both), a game's own in its file in game-settings, which has only the keys the game changes.
// Porpoise reads them as PS5-ECAM starts a game in it.
#include "shell_internal.h"
#include "../../app/achievements.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <filesystem>
#include "../actions.h"
#include "../../ps5/pad.h"

namespace ps5shell
{
	using ps5lang::Tr;
	using ps5lang::TrF;
	using ps5lang::TrMark;

	namespace
	{
		enum class Kind
		{
			Toggle,	 // 0 or 1
			Choice,	 // one of its options, from its first value
			Percent, // tenths, shown as a percentage on a slider
			Border, // a border's name: "" none, a built-in's, or a PNG's in <data>/borders
			Position, // a GBA screen's place, in steps of 120 pixels (GbaWindow's numbers); its first, Automatic
			GbaGame, // a GBA's game: one of the files in its folder, or none
			SoundPreset, // one of Porpoise's presets, each a set of the sound's keys (Settings::use_audio_preset)
		};
		enum class Only
		{
			Both,
			GameCube,
			Wii,
		};
		struct Setting
		{
			const char* page; // its Settings page
			const char* key;  // its key in Porpoise's files (ui_settings.cpp's kFields)
			Kind kind;
			const char* label;
			const char* description;
			const char* help; // Triangle's; the description where there is none
			std::vector<const char*> options; // a choice's (Tr looks up the names marked)
			int first, last, fallback; // its values, and Porpoise's own default (ui_settings.hpp)
			Only only = Only::Both;
			bool everyGame = false; // never a game's own (add_game_rows leaves it out of a game's settings)
			const char* group = nullptr; // behind this › row of its page (kGroups), where it isn't an everyday one
			bool gameOnly = false;		 // only a game's own (add_game_rows' per_game)
			int part = -1;				 // its place in a key holding several values (gba_romN, gba_windowN)
		};

		// The › rows, each with the settings Porpoise's Show All Settings shows, in add_game_rows' sections
		struct Group
		{
			const char* id; // Row::opens
			const char* label;
			const char* description;
		};
		const Group kGroups[] = {
			{"porpoise-enhancements", TrMark("Enhancements"), TrMark("Bloom, colour, texture filtering, resampling, lighting and fog.")},
			{"porpoise-rendering", TrMark("Rendering"), TrMark("Shaders, the texture cache and texture packs.")},
			{"porpoise-player1", TrMark("Player 1"), TrMark("Button layout, vibration, sticks, turbo and the triggers.")},
			{"porpoise-player2", TrMark("Player 2"), TrMark("This player's light bar. Buttons are the same as player 1's.")},
			{"porpoise-player3", TrMark("Player 3"), TrMark("This player's light bar. Buttons are the same as player 1's.")},
			{"porpoise-player4", TrMark("Player 4"), TrMark("This player's light bar. Buttons are the same as player 1's.")},
			{"porpoise-port1", TrMark("Port 1"), TrMark("What's plugged into this port, and who plays it.")},
			{"porpoise-port2", TrMark("Port 2"), TrMark("What's plugged into this port, and who plays it.")},
			{"porpoise-port3", TrMark("Port 3"), TrMark("What's plugged into this port, and who plays it.")},
			{"porpoise-port4", TrMark("Port 4"), TrMark("What's plugged into this port, and who plays it.")},
			{"porpoise-gbavideo", TrMark("GBA video"), TrMark("Each GBA's screen on the TV.")},
			{"porpoise-gbasound", TrMark("GBA sound"), TrMark("Each GBA's sound.")},
			{"porpoise-gbav1", TrMark("Port 1"), TrMark("This port's GBA screen.")},
			{"porpoise-gbav2", TrMark("Port 2"), TrMark("This port's GBA screen.")},
			{"porpoise-gbav3", TrMark("Port 3"), TrMark("This port's GBA screen.")},
			{"porpoise-gbav4", TrMark("Port 4"), TrMark("This port's GBA screen.")},
			{"porpoise-gbas1", TrMark("Port 1"), TrMark("This port's GBA sound.")},
			{"porpoise-gbas2", TrMark("Port 2"), TrMark("This port's GBA sound.")},
			{"porpoise-gbas3", TrMark("Port 3"), TrMark("This port's GBA sound.")},
			{"porpoise-gbas4", TrMark("Port 4"), TrMark("This port's GBA sound.")},
			{"porpoise-remote", TrMark("Wii Remote"), TrMark("The pointer's reach, your grip, motion and real Wii Remotes.")},
			{"porpoise-experimental", TrMark("Experimental"), TrMark("Settings that may not work on PS5 or need extra setup.")},
		};

		// Porpoise's Settings has them in this order. My layouts (button_layout 2 to 5) are named in
		// PorpoiseRows.
		const std::vector<Setting> kSettings = {
			{"graphics", "resolution", Kind::Choice, TrMark("Internal resolution"),
				TrMark("Higher is sharper but can slow games down."), nullptr,
				{"1x (480p)", "2x (720p)", "3x (1080p)", "4x (1440p)", "5x (1800p)", "6x (4K)"}, 1, 6, 3},
			{"graphics", "antialiasing", Kind::Choice, TrMark("Anti-aliasing"),
				TrMark("Smooths jagged edges. SSAA is sharpest but heaviest."), nullptr,
				{TrMark("Off"), "2x MSAA", "4x MSAA", "8x MSAA", "2x SSAA", "4x SSAA", "8x SSAA"}, 0, 6, 0},
			{"graphics", "wide", Kind::Choice, TrMark("Widescreen"),
				TrMark("16:9 for games with a widescreen code or option, 4:3 for the rest."),
				TrMark("Automatic: 16:9 for games with a widescreen code or option, 4:3 for the rest. On: also uses a widescreen hack for other games, which can glitch at the edges. Off: always 4:3. Universal (beta): widens the game's 3D view where it can, and uses the hack elsewhere."),
				{TrMark("Automatic"), TrMark("On"), TrMark("Off"), TrMark("Universal (beta)")}, 0, 3, 0},
			{"graphics", "screen_filter", Kind::Choice, TrMark("Screen filter"),
				TrMark("A filter over the picture: scaling, CRT, VHS, pixels or LCD."),
				TrMark("Smooth or sharp scaling, sharpening, CRT and arcade monitors, VHS, 8-bit and 16-bit pixels, a green handheld screen, scanlines, composite, shadow mask, aperture grille, sharp LCD, AMD FSR 1, or FXAA. Filter strength sets how strong it is."),
				{TrMark("Smooth"), TrMark("Sharp"), TrMark("Sharpen (CAS)"), "CRT", TrMark("Arcade CRT"), "VHS", TrMark("Soft VHS"),
					TrMark("8-bit"), TrMark("Pocket"), TrMark("Scanlines"), TrMark("Shadow mask"), "LCD", "FSR 1", TrMark("16-bit"),
					TrMark("NTSC composite"), TrMark("Aperture grille"), TrMark("Smooth edges (FXAA)")},
				0, 16, 0},
			{"graphics", "filter_strength", Kind::Percent, TrMark("Filter strength"), TrMark("How strong the screen filter is."), nullptr, {},
				1, 10, 6},
			{"graphics", "frame_gen", Kind::Toggle, TrMark("Frame generation"),
				TrMark("Adds a frame between each pair, so 30 fps plays at 60. Adds a little delay."),
				TrMark("Adds a frame between each pair: 30 fps plays at 60, and at 120 Hz, 60 fps plays at 120. Adds a little delay and can smear fast motion."),
				{}, 0, 1, 0},
			{"graphics", "fg_steady30", Kind::Toggle, TrMark("Steady 30 fps"),
				TrMark("With frame generation, holds the game at an even 30 for a steady 60."),
				TrMark("With frame generation on, the game is held at an even 30 fps and frame generation fills in the rest, for a steady 60. For games that swing between 30 and 60. Leave it off for games that hold 60."),
				{}, 0, 1, 0},
			{"graphics", "colour_filter", Kind::Choice, TrMark("Color filter"), TrMark("For color blindness: moves the colors you may not tell apart to ones you can."), nullptr,
				{TrMark("Off"), TrMark("Red-weak"), TrMark("Green-weak"), TrMark("Blue-weak"), TrMark("Grayscale")}, 0, 4, 0, Only::Both, true},
			{"graphics", "colour_filter_games", Kind::Toggle, TrMark("Color filter in games"), TrMark("Applies the color filter to games too."), nullptr, {}, 0, 1, 0, Only::Both, true},
			{"graphics", "fps_overlay", Kind::Toggle, TrMark("Frame rate counter"), TrMark("Shows the frame rate in a corner."),
				nullptr, {}, 0, 1, 0},
			{"graphics", "output_res", Kind::Choice, TrMark("Output resolution"),
				TrMark("The resolution sent to the TV. Match the PS5 uses the console's setting."),
				TrMark("The resolution sent to the TV. Match the PS5 uses the console's setting, so the TV doesn't switch modes. 1080p is fastest; 1440p and 4K are sharper on a 4K TV."),
				{TrMark("Match the PS5"), "1080p", "1440p", "4K"}, 0, 3, 0, Only::Both, true},
			{"graphics", "aspect", Kind::Choice, TrMark("Aspect ratio"), TrMark("The picture's shape. Automatic follows the game."), nullptr,
				{TrMark("Automatic"), TrMark("Force 16:9"), TrMark("Force 4:3"), TrMark("Stretch to fill")}, 0, 3, 3, Only::Both, false, nullptr},
			{"graphics", "border", Kind::Border, TrMark("Border"), TrMark("Fills the bars beside a 4:3 picture. Your own 1920x1080 PNGs go in /data/porpoise/borders."), nullptr, {}, 0, 0, 0},
			{"graphics", "vsync", Kind::Toggle, TrMark("V-Sync"), TrMark("Syncs frames to the TV's refresh for the smoothest motion."),
				TrMark("Syncs frames to the TV's refresh for the smoothest motion. Off uses the emulator's own timing."),
				{}, 0, 1, 1, Only::Both, false, nullptr},
			{"graphics", "shader_mode", Kind::Choice, TrMark("Shader compilation"), TrMark("Ubershaders prevent stutter when something new is drawn, but use more GPU."), nullptr,
				{TrMark("Synchronous"), TrMark("Ubershaders"), TrMark("Async ubershaders"), TrMark("Async, skip drawing")}, 0, 3, 2, Only::Both, false, "porpoise-rendering"},
			{"graphics", "wait_shaders", Kind::Toggle, TrMark("Build shaders before starting"), TrMark("Shaders the game used last time are built while it loads. It starts a moment later."),
				TrMark("Builds the shaders the game used last time while it loads, so effects don't stutter. The game takes a moment longer to start."),
				{}, 0, 1, 1, Only::Both, false, "porpoise-rendering"},
			{"graphics", "threaded_gpu", Kind::Toggle, TrMark("Threaded GPU recording"), TrMark("The driver records drawing on its own thread. Turn it off if a game crashes."),
				TrMark("Records drawing on a separate thread. Can speed up demanding games. Turn it off if a game crashes or looks wrong. Applies next time a game starts."),
				{}, 0, 1, 0, Only::Both, false, "porpoise-rendering"},
			{"graphics", "texture_cache", Kind::Choice, TrMark("Texture cache accuracy"), TrMark("Safe fixes some games' text and effects; Fast is quickest."), nullptr,
				{TrMark("Fast"), TrMark("Middle"), TrMark("Safe")}, 0, 2, 0, Only::Both, false, "porpoise-rendering"},
			{"graphics", "crop_overscan", Kind::Toggle, TrMark("Crop overscan"), TrMark("Hides the black borders some games draw at the edges."), nullptr, {},
				0, 1, 0, Only::Both, false, "porpoise-rendering"},
			{"graphics", "crop_left", Kind::Choice, TrMark("Crop left"), TrMark("Trims the left edge of the picture."), nullptr,
				{"0%", "0.5%", "1%", "1.5%", "2%", "2.5%", "3%", "3.5%", "4%", "4.5%", "5%", "5.5%", "6%", "6.5%", "7%", "7.5%", "8%", "8.5%", "9%", "9.5%", "10%"}, 0, 20, 0, Only::Both, false, "porpoise-rendering"},
			{"graphics", "crop_right", Kind::Choice, TrMark("Crop right"), TrMark("Trims the right edge of the picture."), nullptr,
				{"0%", "0.5%", "1%", "1.5%", "2%", "2.5%", "3%", "3.5%", "4%", "4.5%", "5%", "5.5%", "6%", "6.5%", "7%", "7.5%", "8%", "8.5%", "9%", "9.5%", "10%"}, 0, 20, 0, Only::Both, false, "porpoise-rendering"},
			{"graphics", "crop_top", Kind::Choice, TrMark("Crop top"), TrMark("Trims the top edge of the picture."), nullptr,
				{"0%", "0.5%", "1%", "1.5%", "2%", "2.5%", "3%", "3.5%", "4%", "4.5%", "5%", "5.5%", "6%", "6.5%", "7%", "7.5%", "8%", "8.5%", "9%", "9.5%", "10%"}, 0, 20, 0, Only::Both, false, "porpoise-rendering"},
			{"graphics", "crop_bottom", Kind::Choice, TrMark("Crop bottom"), TrMark("Trims the bottom edge of the picture."), nullptr,
				{"0%", "0.5%", "1%", "1.5%", "2%", "2.5%", "3%", "3.5%", "4%", "4.5%", "5%", "5.5%", "6%", "6.5%", "7%", "7.5%", "8%", "8.5%", "9%", "9.5%", "10%"}, 0, 20, 0, Only::Both, false, "porpoise-rendering"},
			{"graphics", "skip_dupes", Kind::Toggle, TrMark("Skip duplicate frames"), TrMark("Saves work when a game shows the same frame twice."), nullptr, {},
				0, 1, 1, Only::Both, false, "porpoise-rendering"},
			{"graphics", "anisotropy", Kind::Choice, TrMark("Anisotropic filtering"), TrMark("Sharper textures on floors and walls seen at an angle."), nullptr,
				{"1x", "2x", "4x", "8x", "16x"}, 0, 4, 2, Only::Both, false, "porpoise-enhancements"},
			{"graphics", "bloom", Kind::Choice, TrMark("Extra bloom"), TrMark("Adds a soft glow around bright areas."), nullptr,
				{TrMark("Off"), TrMark("Low"), TrMark("Medium"), TrMark("High")}, 0, 3, 0, Only::Both, false, "porpoise-enhancements"},
			{"graphics", "color_saturation", Kind::Choice, TrMark("Saturation"), TrMark("How rich the colours are. 0 is as the game has them."), nullptr,
				{"-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5"}, 0, 10, 5, Only::Both, false, "porpoise-enhancements"},
			{"graphics", "color_contrast", Kind::Choice, TrMark("Contrast"), TrMark("How far apart the darks and the lights are. 0 is as the game has them."), nullptr,
				{"-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5"}, 0, 10, 5, Only::Both, false, "porpoise-enhancements"},
			{"graphics", "color_warmth", Kind::Choice, TrMark("Warmth"), TrMark("Warmer (more red) or cooler (more blue). 0 is as the game has it."), nullptr,
				{"-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5"}, 0, 10, 5, Only::Both, false, "porpoise-enhancements"},
			{"graphics", "texture_filter", Kind::Choice, TrMark("Texture filtering"), TrMark("Force sharp or smooth textures, or leave it to the game."), nullptr,
				{TrMark("The game's own"), TrMark("Nearest (sharp)"), TrMark("Linear (smooth)")}, 0, 2, 0, Only::Both, false, "porpoise-enhancements"},
			{"graphics", "resampling", Kind::Choice, TrMark("Output resampling"), TrMark("How the picture is scaled. Sharp bilinear keeps pixels crisp."), nullptr,
				{TrMark("Default"), TrMark("Bilinear"), "B-Spline", "Mitchell-Netravali", "Catmull-Rom", TrMark("Sharp bilinear"), TrMark("Area sampling")},
				0, 6, 0, Only::Both, false, "porpoise-enhancements"},
			{"graphics", "pixel_lighting", Kind::Toggle, TrMark("Per-pixel lighting"), TrMark("Smoother lighting on surfaces. A little heavier."), nullptr, {},
				0, 1, 0, Only::Both, false, "porpoise-enhancements"},
			{"graphics", "disable_fog", Kind::Toggle, TrMark("Disable fog"), TrMark("Removes distance fog. Some games use fog for their look."), nullptr, {},
				0, 1, 0, Only::Both, false, "porpoise-enhancements"},
			{"graphics", "gfx_bloom", Kind::Choice, TrMark("Bloom"), TrMark("A graphics mod for this game's glow. Applies next time it starts."), nullptr,
				{TrMark("The game's own"), TrMark("Off"), TrMark("Blurred"), TrMark("Native resolution")}, 0, 3, 0, Only::Both, false, "porpoise-enhancements", true},
			{"graphics", "gfx_dof", Kind::Choice, TrMark("Depth of field"), TrMark("A graphics mod for the blur on faraway things. Applies next time it starts."), nullptr,
				{TrMark("The game's own"), TrMark("Off"), TrMark("Blurred"), TrMark("Native resolution")}, 0, 3, 0, Only::Both, false, "porpoise-enhancements", true},
			{"graphics", "gfx_hud", Kind::Toggle, TrMark("Hide the HUD"), TrMark("Hides the game's HUD, for clean screenshots. Applies the next time it starts."), nullptr, {}, 0, 1, 0, Only::Both, false, "porpoise-enhancements", true},
			{"graphics", "gfx_extra", Kind::Toggle, TrMark("Native resolution goop"), TrMark("Super Mario Sunshine's goop at the game's own resolution. Applies the next time it starts."), nullptr, {}, 0, 1, 0, Only::Both, false, "porpoise-enhancements", true},
			{"graphics", "custom_textures", Kind::Toggle, TrMark("Custom textures"), TrMark("Loads HD texture packs."),
				TrMark("Loads HD texture packs. Put each pack's folder, named with the game's ID (like GALE01), in /data/porpoise/saves/User/Load/Textures."),
				{}, 0, 1, 0, Only::Both, false, "porpoise-enhancements"},
			{"controls", "gc_port1_device", Kind::Choice, TrMark("Device"), TrMark("What's plugged into this controller port."),
				TrMark("What's plugged into this controller port. GBA (Integrated) is a Game Boy Advance on the link cable, for games like Four Swords Adventures. It needs the GBA BIOS at /data/porpoise/saves/User/GBA/gba_bios.bin. Best set per game."),
				{TrMark("GameCube Controller"), TrMark("GBA (Integrated)"), TrMark("None")}, 0, 2, 0, Only::GameCube, false, "porpoise-port1"},
			{"controls", "gba_port1_control", Kind::Choice, TrMark("Controlled by"), TrMark("Who plays this GBA by holding their GBA button."),
				TrMark("A player: they play it while holding their GBA button, with its sound on their controller. Its own controller: the controller in this port is the GBA, no button needed. Nobody: only from the web page."),
				{TrMark("Player 1"), TrMark("Player 2"), TrMark("Player 3"), TrMark("Player 4"), TrMark("Its own controller"), TrMark("None")}, 0, 5, 0, Only::GameCube, false, "porpoise-port1"},
			{"controls", "gc_port1_player", Kind::Choice, TrMark("Controlled by"), TrMark("Whose controller is in this port."), nullptr,
				{TrMark("Player 1"), TrMark("Player 2"), TrMark("Player 3"), TrMark("Player 4"), TrMark("None")}, 0, 4, 0, Only::GameCube, false, "porpoise-port1"},
			{"controls", "gba_port1_mode", Kind::Choice, TrMark("GBA button"), TrMark("Hold the GBA button, press it to toggle, or keep it always on or off."), nullptr,
				{TrMark("Hold"), TrMark("Toggle"), TrMark("Always on"), TrMark("Always off")}, 0, 3, 0, Only::GameCube, false, "porpoise-port1"},
			{"controls", "gba_rom1", Kind::GbaGame, TrMark("GBA game"), TrMark("The GBA game loaded at start, from games/gba/port1. None: BIOS only."), nullptr, {}, 0, 0, 0, Only::GameCube, false, "porpoise-port1", false, 1},
			{"controls", "gba_rom1", Kind::Toggle, TrMark("Save beside the game"), TrMark("Keeps this GBA's save next to its game instead of with the other saves."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-port1", false, 0},
			{"controls", "gc_port2_device", Kind::Choice, TrMark("Device"), TrMark("What's plugged into this controller port."),
				TrMark("What's plugged into this controller port. GBA (Integrated) is a Game Boy Advance on the link cable, for games like Four Swords Adventures. It needs the GBA BIOS at /data/porpoise/saves/User/GBA/gba_bios.bin. Best set per game."),
				{TrMark("GameCube Controller"), TrMark("GBA (Integrated)"), TrMark("None")}, 0, 2, 0, Only::GameCube, false, "porpoise-port2"},
			{"controls", "gba_port2_control", Kind::Choice, TrMark("Controlled by"), TrMark("Who plays this GBA by holding their GBA button."),
				TrMark("A player: they play it while holding their GBA button, with its sound on their controller. Its own controller: the controller in this port is the GBA, no button needed. Nobody: only from the web page."),
				{TrMark("Player 1"), TrMark("Player 2"), TrMark("Player 3"), TrMark("Player 4"), TrMark("Its own controller"), TrMark("None")}, 0, 5, 0, Only::GameCube, false, "porpoise-port2"},
			{"controls", "gc_port2_player", Kind::Choice, TrMark("Controlled by"), TrMark("Whose controller is in this port."), nullptr,
				{TrMark("Player 1"), TrMark("Player 2"), TrMark("Player 3"), TrMark("Player 4"), TrMark("None")}, 0, 4, 1, Only::GameCube, false, "porpoise-port2"},
			{"controls", "gba_port2_mode", Kind::Choice, TrMark("GBA button"), TrMark("Hold the GBA button, press it to toggle, or keep it always on or off."), nullptr,
				{TrMark("Hold"), TrMark("Toggle"), TrMark("Always on"), TrMark("Always off")}, 0, 3, 0, Only::GameCube, false, "porpoise-port2"},
			{"controls", "gba_rom2", Kind::GbaGame, TrMark("GBA game"), TrMark("The GBA game loaded at start, from games/gba/port2. None: BIOS only."), nullptr, {}, 0, 0, 0, Only::GameCube, false, "porpoise-port2", false, 1},
			{"controls", "gba_rom2", Kind::Toggle, TrMark("Save beside the game"), TrMark("Keeps this GBA's save next to its game instead of with the other saves."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-port2", false, 0},
			{"controls", "gc_port3_device", Kind::Choice, TrMark("Device"), TrMark("What's plugged into this controller port."),
				TrMark("What's plugged into this controller port. GBA (Integrated) is a Game Boy Advance on the link cable, for games like Four Swords Adventures. It needs the GBA BIOS at /data/porpoise/saves/User/GBA/gba_bios.bin. Best set per game."),
				{TrMark("GameCube Controller"), TrMark("GBA (Integrated)"), TrMark("None")}, 0, 2, 0, Only::GameCube, false, "porpoise-port3"},
			{"controls", "gba_port3_control", Kind::Choice, TrMark("Controlled by"), TrMark("Who plays this GBA by holding their GBA button."),
				TrMark("A player: they play it while holding their GBA button, with its sound on their controller. Its own controller: the controller in this port is the GBA, no button needed. Nobody: only from the web page."),
				{TrMark("Player 1"), TrMark("Player 2"), TrMark("Player 3"), TrMark("Player 4"), TrMark("Its own controller"), TrMark("None")}, 0, 5, 0, Only::GameCube, false, "porpoise-port3"},
			{"controls", "gc_port3_player", Kind::Choice, TrMark("Controlled by"), TrMark("Whose controller is in this port."), nullptr,
				{TrMark("Player 1"), TrMark("Player 2"), TrMark("Player 3"), TrMark("Player 4"), TrMark("None")}, 0, 4, 2, Only::GameCube, false, "porpoise-port3"},
			{"controls", "gba_port3_mode", Kind::Choice, TrMark("GBA button"), TrMark("Hold the GBA button, press it to toggle, or keep it always on or off."), nullptr,
				{TrMark("Hold"), TrMark("Toggle"), TrMark("Always on"), TrMark("Always off")}, 0, 3, 0, Only::GameCube, false, "porpoise-port3"},
			{"controls", "gba_rom3", Kind::GbaGame, TrMark("GBA game"), TrMark("The GBA game loaded at start, from games/gba/port3. None: BIOS only."), nullptr, {}, 0, 0, 0, Only::GameCube, false, "porpoise-port3", false, 1},
			{"controls", "gba_rom3", Kind::Toggle, TrMark("Save beside the game"), TrMark("Keeps this GBA's save next to its game instead of with the other saves."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-port3", false, 0},
			{"controls", "gc_port4_device", Kind::Choice, TrMark("Device"), TrMark("What's plugged into this controller port."),
				TrMark("What's plugged into this controller port. GBA (Integrated) is a Game Boy Advance on the link cable, for games like Four Swords Adventures. It needs the GBA BIOS at /data/porpoise/saves/User/GBA/gba_bios.bin. Best set per game."),
				{TrMark("GameCube Controller"), TrMark("GBA (Integrated)"), TrMark("None")}, 0, 2, 0, Only::GameCube, false, "porpoise-port4"},
			{"controls", "gba_port4_control", Kind::Choice, TrMark("Controlled by"), TrMark("Who plays this GBA by holding their GBA button."),
				TrMark("A player: they play it while holding their GBA button, with its sound on their controller. Its own controller: the controller in this port is the GBA, no button needed. Nobody: only from the web page."),
				{TrMark("Player 1"), TrMark("Player 2"), TrMark("Player 3"), TrMark("Player 4"), TrMark("Its own controller"), TrMark("None")}, 0, 5, 0, Only::GameCube, false, "porpoise-port4"},
			{"controls", "gc_port4_player", Kind::Choice, TrMark("Controlled by"), TrMark("Whose controller is in this port."), nullptr,
				{TrMark("Player 1"), TrMark("Player 2"), TrMark("Player 3"), TrMark("Player 4"), TrMark("None")}, 0, 4, 3, Only::GameCube, false, "porpoise-port4"},
			{"controls", "gba_port4_mode", Kind::Choice, TrMark("GBA button"), TrMark("Hold the GBA button, press it to toggle, or keep it always on or off."), nullptr,
				{TrMark("Hold"), TrMark("Toggle"), TrMark("Always on"), TrMark("Always off")}, 0, 3, 0, Only::GameCube, false, "porpoise-port4"},
			{"controls", "gba_rom4", Kind::GbaGame, TrMark("GBA game"), TrMark("The GBA game loaded at start, from games/gba/port4. None: BIOS only."), nullptr, {}, 0, 0, 0, Only::GameCube, false, "porpoise-port4", false, 1},
			{"controls", "gba_rom4", Kind::Toggle, TrMark("Save beside the game"), TrMark("Keeps this GBA's save next to its game instead of with the other saves."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-port4", false, 0},
			{"controls", "console", Kind::Choice, TrMark("Console"), TrMark("Which console's controls to use, if a game is detected wrongly."), nullptr,
				{TrMark("Automatic"), "GameCube", "Wii"}, 0, 2, 0, Only::Both, false, nullptr, true},
			{"hidden", "button_layout", Kind::Choice, TrMark("Button layout"),
				TrMark("PlayStation: Cross is A and Circle B. GameCube: Circle is A and Cross B."),
				TrMark("PlayStation: Cross is A and Circle is B. GameCube: Circle is A and Cross is B. My layouts are the ones you make on the Buttons page. For GameCube games and Wii games played with a GameCube controller."),
				{"GameCube", "PlayStation"}, 0, 5, 1, Only::Both, false, "porpoise-player1"},
			{"controls", "rumble", Kind::Toggle, TrMark("Vibration"), TrMark("Controller rumble."), nullptr, {},
				0, 1, 1, Only::Both, false, "porpoise-player1"},
			{"controls", "wii_controller", Kind::Choice, TrMark("Wii controller"), TrMark("How a Wii game sees your DualSense."),
				TrMark("Remote and Nunchuk: Nunchuk on the left stick, L1 and L2. Remote: point at the TV, with motion. Held sideways: like an NES pad, tilt to steer. Two DualSenses (alpha): the second is the Nunchuk. GameCube controller: for Wii games that support one. Each other DualSense is another player's Wii Remote."),
				{TrMark("Remote and Nunchuk"), TrMark("Remote"), TrMark("Remote held sideways"), TrMark("Classic Controller"),
					TrMark("Two DualSenses (alpha)"), TrMark("GameCube controller")},
				0, 5, 0, Only::Wii, false, "porpoise-player1"},
			{"controls", "wii_pointer", Kind::Choice, TrMark("Pointer"), TrMark("What moves the Wii Remote's pointer."),
				TrMark("Gyro: point the controller at the screen, and hold R1 a moment to centre it. Touchpad: slide a finger on it. "
					   "Right stick: push it."),
				{TrMark("Gyro"), TrMark("Touchpad"), TrMark("Right stick")}, 0, 2, 0, Only::Wii, false, "porpoise-player1"},
			{"hidden", "p1_port", Kind::Choice, TrMark("Player 1's port"), TrMark("The GameCube port your controller is in, for the few games that want another."),
				TrMark("The GameCube port your controller is in. A few games briefly need it in another port."),
				{TrMark("Port 1"), TrMark("Port 2"), TrMark("Port 3"), TrMark("Port 4")}, 0, 3, 0, Only::GameCube, false, "porpoise-player1"},
			{"controls", "invert_main", Kind::Choice, TrMark("Invert the control stick"), TrMark("For a game whose controls feel reversed. Best set per game."), nullptr,
				{TrMark("Off"), TrMark("Up and down"), TrMark("Left and right"), TrMark("Both")}, 0, 3, 0, Only::Both, false, "porpoise-player1"},
			{"controls", "invert_c", Kind::Choice, TrMark("Invert the C-stick"), TrMark("For a game whose camera feels reversed. Best set per game."), nullptr,
				{TrMark("Off"), TrMark("Up and down"), TrMark("Left and right"), TrMark("Both")}, 0, 3, 0, Only::Both, false, "porpoise-player1"},
			{"controls", "stick_sensitivity", Kind::Choice, TrMark("Stick sensitivity"), TrMark("Lower makes small pushes gentler. A full push still turns fully."), nullptr,
				{TrMark("Normal"), TrMark("Lower"), TrMark("Low"), TrMark("Lowest")}, 0, 3, 0, Only::Both, false, "porpoise-player1"},
			{"controls", "stick_deadzone", Kind::Choice, TrMark("Stick dead zone"), TrMark("How far the stick moves before the game sees it, for a stick that drifts."), nullptr,
				{TrMark("Off"), TrMark("Small"), TrMark("Medium"), TrMark("Large")}, 0, 3, 0, Only::Both, false, "porpoise-player1"},
			{"controls", "ff_buttons", Kind::Toggle, TrMark("Fast forward buttons"), TrMark("Touchpad + R1 steps fast forward; touchpad + R2 fast-forwards while held."), nullptr, {},
				0, 1, 1, Only::Both, false, "porpoise-player1"},
			{"controls", "quick_slot", Kind::Choice, TrMark("Quick save buttons"), TrMark("Touchpad + L1 saves to this slot and touchpad + L2 loads it."), nullptr,
				{TrMark("Off"), TrMark("Slot 1"), TrMark("Slot 2"), TrMark("Slot 3")}, 0, 3, 0, Only::Both, false, "porpoise-player1"},
			{"controls", "turbo", Kind::Choice, TrMark("Turbo button"), TrMark("Holding this button presses it repeatedly."), nullptr,
				{TrMark("Off"), TrMark("Cross"), TrMark("Circle"), TrMark("Square"), TrMark("Triangle"), "L1", "R1", "L2", "R2"}, 0, 8, 0, Only::Both, false, "porpoise-player1"},
			{"controls", "shot_buttons", Kind::Toggle, TrMark("Screenshot buttons"), TrMark("In a game, touchpad + Square takes a screenshot."), nullptr, {},
				0, 1, 1, Only::Both, true, nullptr},
			{"controls", "trigger_feel", Kind::Choice, TrMark("Trigger click"), TrMark("L2 and R2 click near the bottom, like the GameCube's triggers."), nullptr,
				{TrMark("Off"), TrMark("Light"), TrMark("Firm"), TrMark("Extra firm")}, 0, 3, 2, Only::Both, false, "porpoise-player1"},
			{"controls", "light_1", Kind::Choice, TrMark("Light bar"), TrMark("The colour of this player's light bar."), nullptr,
				{TrMark("Blue"), TrMark("Red"), TrMark("Green"), TrMark("Pink"), TrMark("Purple"), TrMark("Orange"), TrMark("Yellow"), TrMark("Cyan"), TrMark("White"), TrMark("Off")}, 0, 9, 0, Only::Both, true, "porpoise-player1"},
			{"controls", "light_2", Kind::Choice, TrMark("Light bar"), TrMark("The colour of this player's light bar."), nullptr,
				{TrMark("Blue"), TrMark("Red"), TrMark("Green"), TrMark("Pink"), TrMark("Purple"), TrMark("Orange"), TrMark("Yellow"), TrMark("Cyan"), TrMark("White"), TrMark("Off")}, 0, 9, 1, Only::Both, true, "porpoise-player2"},
			{"controls", "light_3", Kind::Choice, TrMark("Light bar"), TrMark("The colour of this player's light bar."), nullptr,
				{TrMark("Blue"), TrMark("Red"), TrMark("Green"), TrMark("Pink"), TrMark("Purple"), TrMark("Orange"), TrMark("Yellow"), TrMark("Cyan"), TrMark("White"), TrMark("Off")}, 0, 9, 2, Only::Both, true, "porpoise-player3"},
			{"controls", "light_4", Kind::Choice, TrMark("Light bar"), TrMark("The colour of this player's light bar."), nullptr,
				{TrMark("Blue"), TrMark("Red"), TrMark("Green"), TrMark("Pink"), TrMark("Purple"), TrMark("Orange"), TrMark("Yellow"), TrMark("Cyan"), TrMark("White"), TrMark("Off")}, 0, 9, 3, Only::Both, true, "porpoise-player4"},
			{"controls", "wii_source", Kind::Choice, TrMark("Wii Remotes"), TrMark("Use your DualSense, or real Remotes through a Mayflash DolphinBar."),
				TrMark("DualSense: your controller is the Wii Remote. Real Wii Remotes: up to four through a Mayflash DolphinBar (mode 4) in a USB port, with their own attachments, pointer and motion. Applies when the game starts."),
				{"DualSense", TrMark("Real Wii Remotes (DolphinBar)")}, 0, 1, 0, Only::Wii, false, "porpoise-experimental"},
			{"controls", "wii_buttons_use", Kind::Choice, TrMark("Wii buttons"), TrMark("Which buttons the Wii controllers use."), nullptr,
				{TrMark("Default set"), TrMark("Mine 1"), TrMark("Mine 2")}, 0, 2, 1, Only::Wii, false, "porpoise-remote"},
			{"controls", "wii_speed", Kind::Choice, TrMark("Pointer speed"), TrMark("How far you turn the controller to reach the screen's edge."), nullptr,
				{TrMark("Your screen"), "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, 0, 10, 5, Only::Wii, false, "porpoise-remote"},
			{"controls", "wii_range", Kind::Choice, TrMark("Pointer range"), TrMark("Widen it if the pointer can't reach the screen's edge."), nullptr,
				{TrMark("Automatic"), TrMark("Standard"), TrMark("Wide"), TrMark("Extra wide")}, 0, 3, 0, Only::Wii, false, "porpoise-remote"},
			{"controls", "wii_grip", Kind::Choice, TrMark("Grip"), TrMark("How you hold the DualSense. Automatic detects it."), nullptr,
				{TrMark("Automatic"), TrMark("Both hands"), TrMark("Upright, trigger to the TV"), TrMark("Upright, facing you")}, 0, 3, 0, Only::Wii, false, "porpoise-remote"},
			{"controls", "wii_motion", Kind::Toggle, TrMark("Motion"), TrMark("Tilt, swing and point with the DualSense."), nullptr, {},
				0, 1, 1, Only::Wii, false, "porpoise-remote"},
			{"controls", "wii_shake", Kind::Toggle, TrMark("Flick to shake"), TrMark("A quick flick of the controller shakes the Remote."), nullptr, {},
				0, 1, 1, Only::Wii, false, "porpoise-remote"},
			{"controls", "wii_motion_plus", Kind::Choice, TrMark("Wii MotionPlus"), TrMark("Adds MotionPlus using the gyroscope. Automatic: for games that need it."), nullptr,
				{TrMark("Automatic"), TrMark("On"), TrMark("Off")}, 0, 2, 0, Only::Wii, false, "porpoise-remote"},
			{"controls", "wii_setup_ask", Kind::Toggle, TrMark("Setup before each game"), TrMark("Shows the Wii Remote's setup when a game starts."), nullptr, {},
				0, 1, 0, Only::Wii, false, "porpoise-remote"},
			{"sound", "volume", Kind::Percent, TrMark("Volume"), TrMark("The game's sound."), nullptr, {}, 0, 10, 10},
			{"sound", "muted", Kind::Toggle, TrMark("Mute the game"), TrMark("Silences the game."), nullptr, {}, 0, 1, 0, Only::Both, false, nullptr},
			{"sound", "dsp_accurate", Kind::Toggle, TrMark("Accurate sound"), TrMark("Exact sound emulation. Fixes a few games but is much slower."), nullptr, {},
				0, 1, 0, Only::Both, false, nullptr},
			{"sound", "wiimote_speaker", Kind::Choice, TrMark("Wii Remote speaker"), TrMark("The Remote's sounds: from the TV, or from each player's controller."), nullptr,
				{TrMark("Off"), "TV", TrMark("Controller")}, 0, 2, 0, Only::Wii, false, "porpoise-experimental"},
			{"sound", "audio_preset", Kind::SoundPreset, TrMark("Sound preset"), TrMark("How sound handles a game running slow."),
				TrMark("Smooth fills gaps when a game runs slow instead of crackling. Responsive has a little less delay. Extra smooth suits games that slow down often. Classic is the older sound; changing to or from it applies next time a game starts."),
				{TrMark("Smooth"), TrMark("Responsive"), TrMark("Extra smooth"), TrMark("Classic")}, 0, 3, 0, Only::Both, false, nullptr},
			{"sound", "audio_buffer", Kind::Choice, TrMark("Audio buffer"), TrMark("More prevents crackling but adds a little delay."), nullptr,
				{TrMark("Low"), TrMark("Normal"), TrMark("Safe")}, 0, 2, 1},
			{"sound", "audio_fill", Kind::Toggle, TrMark("Fill audio gaps"), TrMark("Fills gaps with a faded repeat when a game runs slow."), nullptr, {}, 0, 1, 1},
			{"sound", "audio_stretch", Kind::Toggle, TrMark("Audio stretching"), TrMark("When a game slows down, its sound slows with it, instead of crackling."), nullptr, {}, 0, 1, 0},
			{"sound", "microphone", Kind::Toggle, TrMark("Microphone"), TrMark("Use the DualSense mic as the GameCube Microphone and Wii Speak."), nullptr, {},
				0, 1, 0, Only::Both, false, nullptr},
			{"system", "quick_resume", Kind::Toggle, TrMark("Resume where you left off"),
				TrMark("Pick up where you left off when you quit from the in-game menu."),
				TrMark("Quit from the in-game menu and the game continues from there next time. Start Over in the in-game menu starts fresh."),
				{}, 0, 1, 0},
			{"system", "language", Kind::Choice, TrMark("System language"),
				TrMark("The console's language. European games use it."), nullptr,
				{TrMark("English"), TrMark("Japanese"), TrMark("German"), TrMark("French"), TrMark("Spanish"), TrMark("Italian"),
					TrMark("Dutch"), TrMark("Chinese (simplified)"), TrMark("Chinese (traditional)"), TrMark("Korean")},
				0, 9, 0},
			{"system", "fast_states", Kind::Toggle, TrMark("Fast save states"), TrMark("Quicker and smaller states. Turn it off if a game looks wrong after loading one."), nullptr, {},
				0, 1, 1},
			{"system", "progressive", Kind::Toggle, TrMark("Progressive scan"), TrMark("480p output, as on a component cable."), nullptr, {},
				0, 1, 1, Only::Both, false, nullptr},
			{"system", "wii_widescreen", Kind::Toggle, TrMark("Wii widescreen"), TrMark("The Wii's 16:9 setting, used by Wii games."), nullptr, {},
				0, 1, 1, Only::Wii, false, "porpoise-experimental"},
			{"system", "pal60", Kind::Toggle, TrMark("PAL games at 60 Hz"), TrMark("European Wii games at 60 Hz, as a Wii set to EURGB60. Off: 50 Hz."), nullptr, {},
				0, 1, 1, Only::Wii, false, "porpoise-experimental"},
			{"system", "sensor_bar", Kind::Choice, TrMark("Sensor bar"), TrMark("Where the sensor bar is."), nullptr,
				{TrMark("Below the TV"), TrMark("Above the TV")}, 0, 1, 0, Only::Wii, false, "porpoise-experimental"},
			{"system", "gc_bios", Kind::Toggle, TrMark("GameCube boot animation"), TrMark("Shows the GameCube start-up before games. Needs your own IPL.bin."), nullptr, {},
				0, 1, 0, Only::GameCube, false, "porpoise-experimental"},
			{"system", "wii_online", Kind::Toggle, TrMark("WiiConnect24 channels"), TrMark("Forecast, News, Check Mii Out and more through WiiLink. Needs the PS5 online."), nullptr, {},
				0, 1, 0, Only::Wii, false, "porpoise-experimental"},
			{"system", "cpu_clock", Kind::Choice, TrMark("CPU clock"), TrMark("Overclocking can smooth a game that slows down. 100% is the real console."), nullptr,
				{"50%", "60%", "70%", "80%", "90%", "100%", "150%", "200%", "250%", "300%"}, 0, 9, 5, Only::Both, false, nullptr},
			{"system", "auto_boost", Kind::Toggle, TrMark("Auto Boost"), TrMark("New games start with the settings that run them best on PS5. Turn it off for a game in its own settings."), nullptr, {},
				0, 1, 1, Only::Both, true, nullptr},
			// (a game's own is beside its Auto Boost: PorpoiseBoostRows)
			{"system", "fast_float", Kind::Toggle, TrMark("Riptide Boost"), TrMark("Speeds up the emulated processor's 3D math. Turn it off if a game misbehaves."), nullptr, {},
				0, 1, 0, Only::Both, true, nullptr},
			{"system", "perf_profile", Kind::Toggle, TrMark("Performance profiler"), TrMark("For reporting a slow game: logs where the CPU spends its time. Costs a little speed; turn it off after."), nullptr, {},
				0, 1, 0, Only::Both, true, nullptr},
			{"system", "debug_log_folder", Kind::Toggle, TrMark("Debug logs"), TrMark("Keeps detailed logs for reporting a problem. Can slow games a little."), nullptr, {},
				0, 1, 0, Only::Both, true, nullptr},
			{"system", "dual_core", Kind::Toggle, TrMark("Dual core"), TrMark("Faster. Turn it off for a game that freezes or glitches."), nullptr, {},
				0, 1, 1, Only::Both, false, nullptr},
			{"system", "own_cores", Kind::Toggle, TrMark("Emulator on its own cores"), TrMark("A PS5 core each for the emulated processor and graphics. Usually faster."), nullptr, {},
				0, 1, 1, Only::Both, false, nullptr},
			{"system", "accurate_fma", Kind::Toggle, TrMark("Exact multiply-add"), TrMark("Exact floating-point rounding. Much slower, and very few games need it."), nullptr, {},
				0, 1, 0, Only::Both, false, nullptr},
			{"system", "fast_disc", Kind::Toggle, TrMark("Fast disc loading"), TrMark("Shorter loading screens. A few games need real disc speed."), nullptr, {},
				0, 1, 0, Only::Both, false, nullptr},
			{"system", "cheats", Kind::Toggle, TrMark("Cheats"), TrMark("Cheat codes, for games that have them."), nullptr, {},
				0, 1, 0, Only::Both, false, nullptr},
			{"hidden", "gba_buttons_use", Kind::Choice, TrMark("GBA buttons in use"), TrMark("The default set, or one of your own."), nullptr,
				{TrMark("Default set"), TrMark("Mine 1"), TrMark("Mine 2")}, 0, 2, 1},
			{"gba", "gba_web", Kind::Toggle, TrMark("GBA web page"), TrMark("Play the GBA in a browser on the same network, on port 6771."), nullptr, {},
				0, 1, 1, Only::GameCube, false, nullptr},
			{"gba", "gba_port1_corner", Kind::Choice, TrMark("Position"), TrMark("Where its screen sits on the TV. Moving it in a game makes it Manual."), nullptr,
				{TrMark("Automatic"), TrMark("Top left"), TrMark("Top right"), TrMark("Bottom left"), TrMark("Bottom right"), TrMark("Manual")}, 0, 5, 0, Only::GameCube, false, "porpoise-gbav1"},
			{"gba", "gba_port1_show", Kind::Choice, TrMark("Show the GBA's screen"), TrMark("Shows it over the game while held, toggled with each press, or always on or off."), nullptr,
				{TrMark("While held"), TrMark("Toggle"), TrMark("Always on"), TrMark("Always off")}, 0, 3, 0, Only::GameCube, false, "porpoise-gbav1"},
			{"gba", "gba_window1", Kind::Position, TrMark("Position across"), TrMark("Where its screen sits on the TV. Automatic picks a corner."), nullptr,
				{TrMark("Automatic"), "0", "120", "240", "360", "480", "600", "720", "840", "960", "1080", "1200", "1320", "1440", "1560", "1680"}, 0, 15, 0, Only::GameCube, false, "porpoise-gbav1", false, 0},
			{"gba", "gba_window1", Kind::Position, TrMark("Position down"), TrMark("Where its screen sits on the TV. Automatic picks a corner."), nullptr,
				{TrMark("Automatic"), "0", "120", "240", "360", "480", "600", "720", "840", "960"}, 0, 9, 0, Only::GameCube, false, "porpoise-gbav1", false, 1},
			{"gba", "gba_window1", Kind::Choice, TrMark("Screen size"), TrMark("How big its screen is: 1 to 4 times the GBA's size."), nullptr, {"1x", "2x", "3x", "4x"}, 0, 3, 2, Only::GameCube, false, "porpoise-gbav1", false, 2},
			{"gba", "gba_window1", Kind::Choice, TrMark("Opacity"), TrMark("How much of the game shows through its screen on the TV."), nullptr, {"40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 0, 6, 6, Only::GameCube, false, "porpoise-gbav1", false, 3},
			{"gba", "gba_window1", Kind::Choice, TrMark("Screen filter"), TrMark("Sharp or soft pixels. The web page has its own setting."), nullptr, {TrMark("Off"), TrMark("Soft pixels")}, 0, 1, 0, Only::GameCube, false, "porpoise-gbav1", false, 6},
			{"gba", "gba_window1", Kind::Toggle, TrMark("Interframe blending"), TrMark("Blends each frame with the last, for games that flicker sprites. TV only."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbav1", false, 7},
			{"gba", "gba_port2_corner", Kind::Choice, TrMark("Position"), TrMark("Where its screen sits on the TV. Moving it in a game makes it Manual."), nullptr,
				{TrMark("Automatic"), TrMark("Top left"), TrMark("Top right"), TrMark("Bottom left"), TrMark("Bottom right"), TrMark("Manual")}, 0, 5, 0, Only::GameCube, false, "porpoise-gbav2"},
			{"gba", "gba_port2_show", Kind::Choice, TrMark("Show the GBA's screen"), TrMark("Shows it over the game while held, toggled with each press, or always on or off."), nullptr,
				{TrMark("While held"), TrMark("Toggle"), TrMark("Always on"), TrMark("Always off")}, 0, 3, 0, Only::GameCube, false, "porpoise-gbav2"},
			{"gba", "gba_window2", Kind::Position, TrMark("Position across"), TrMark("Where its screen sits on the TV. Automatic picks a corner."), nullptr,
				{TrMark("Automatic"), "0", "120", "240", "360", "480", "600", "720", "840", "960", "1080", "1200", "1320", "1440", "1560", "1680"}, 0, 15, 0, Only::GameCube, false, "porpoise-gbav2", false, 0},
			{"gba", "gba_window2", Kind::Position, TrMark("Position down"), TrMark("Where its screen sits on the TV. Automatic picks a corner."), nullptr,
				{TrMark("Automatic"), "0", "120", "240", "360", "480", "600", "720", "840", "960"}, 0, 9, 0, Only::GameCube, false, "porpoise-gbav2", false, 1},
			{"gba", "gba_window2", Kind::Choice, TrMark("Screen size"), TrMark("How big its screen is: 1 to 4 times the GBA's size."), nullptr, {"1x", "2x", "3x", "4x"}, 0, 3, 2, Only::GameCube, false, "porpoise-gbav2", false, 2},
			{"gba", "gba_window2", Kind::Choice, TrMark("Opacity"), TrMark("How much of the game shows through its screen on the TV."), nullptr, {"40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 0, 6, 6, Only::GameCube, false, "porpoise-gbav2", false, 3},
			{"gba", "gba_window2", Kind::Choice, TrMark("Screen filter"), TrMark("Sharp or soft pixels. The web page has its own setting."), nullptr, {TrMark("Off"), TrMark("Soft pixels")}, 0, 1, 0, Only::GameCube, false, "porpoise-gbav2", false, 6},
			{"gba", "gba_window2", Kind::Toggle, TrMark("Interframe blending"), TrMark("Blends each frame with the last, for games that flicker sprites. TV only."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbav2", false, 7},
			{"gba", "gba_port3_corner", Kind::Choice, TrMark("Position"), TrMark("Where its screen sits on the TV. Moving it in a game makes it Manual."), nullptr,
				{TrMark("Automatic"), TrMark("Top left"), TrMark("Top right"), TrMark("Bottom left"), TrMark("Bottom right"), TrMark("Manual")}, 0, 5, 0, Only::GameCube, false, "porpoise-gbav3"},
			{"gba", "gba_port3_show", Kind::Choice, TrMark("Show the GBA's screen"), TrMark("Shows it over the game while held, toggled with each press, or always on or off."), nullptr,
				{TrMark("While held"), TrMark("Toggle"), TrMark("Always on"), TrMark("Always off")}, 0, 3, 0, Only::GameCube, false, "porpoise-gbav3"},
			{"gba", "gba_window3", Kind::Position, TrMark("Position across"), TrMark("Where its screen sits on the TV. Automatic picks a corner."), nullptr,
				{TrMark("Automatic"), "0", "120", "240", "360", "480", "600", "720", "840", "960", "1080", "1200", "1320", "1440", "1560", "1680"}, 0, 15, 0, Only::GameCube, false, "porpoise-gbav3", false, 0},
			{"gba", "gba_window3", Kind::Position, TrMark("Position down"), TrMark("Where its screen sits on the TV. Automatic picks a corner."), nullptr,
				{TrMark("Automatic"), "0", "120", "240", "360", "480", "600", "720", "840", "960"}, 0, 9, 0, Only::GameCube, false, "porpoise-gbav3", false, 1},
			{"gba", "gba_window3", Kind::Choice, TrMark("Screen size"), TrMark("How big its screen is: 1 to 4 times the GBA's size."), nullptr, {"1x", "2x", "3x", "4x"}, 0, 3, 2, Only::GameCube, false, "porpoise-gbav3", false, 2},
			{"gba", "gba_window3", Kind::Choice, TrMark("Opacity"), TrMark("How much of the game shows through its screen on the TV."), nullptr, {"40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 0, 6, 6, Only::GameCube, false, "porpoise-gbav3", false, 3},
			{"gba", "gba_window3", Kind::Choice, TrMark("Screen filter"), TrMark("Sharp or soft pixels. The web page has its own setting."), nullptr, {TrMark("Off"), TrMark("Soft pixels")}, 0, 1, 0, Only::GameCube, false, "porpoise-gbav3", false, 6},
			{"gba", "gba_window3", Kind::Toggle, TrMark("Interframe blending"), TrMark("Blends each frame with the last, for games that flicker sprites. TV only."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbav3", false, 7},
			{"gba", "gba_port4_corner", Kind::Choice, TrMark("Position"), TrMark("Where its screen sits on the TV. Moving it in a game makes it Manual."), nullptr,
				{TrMark("Automatic"), TrMark("Top left"), TrMark("Top right"), TrMark("Bottom left"), TrMark("Bottom right"), TrMark("Manual")}, 0, 5, 0, Only::GameCube, false, "porpoise-gbav4"},
			{"gba", "gba_port4_show", Kind::Choice, TrMark("Show the GBA's screen"), TrMark("Shows it over the game while held, toggled with each press, or always on or off."), nullptr,
				{TrMark("While held"), TrMark("Toggle"), TrMark("Always on"), TrMark("Always off")}, 0, 3, 0, Only::GameCube, false, "porpoise-gbav4"},
			{"gba", "gba_window4", Kind::Position, TrMark("Position across"), TrMark("Where its screen sits on the TV. Automatic picks a corner."), nullptr,
				{TrMark("Automatic"), "0", "120", "240", "360", "480", "600", "720", "840", "960", "1080", "1200", "1320", "1440", "1560", "1680"}, 0, 15, 0, Only::GameCube, false, "porpoise-gbav4", false, 0},
			{"gba", "gba_window4", Kind::Position, TrMark("Position down"), TrMark("Where its screen sits on the TV. Automatic picks a corner."), nullptr,
				{TrMark("Automatic"), "0", "120", "240", "360", "480", "600", "720", "840", "960"}, 0, 9, 0, Only::GameCube, false, "porpoise-gbav4", false, 1},
			{"gba", "gba_window4", Kind::Choice, TrMark("Screen size"), TrMark("How big its screen is: 1 to 4 times the GBA's size."), nullptr, {"1x", "2x", "3x", "4x"}, 0, 3, 2, Only::GameCube, false, "porpoise-gbav4", false, 2},
			{"gba", "gba_window4", Kind::Choice, TrMark("Opacity"), TrMark("How much of the game shows through its screen on the TV."), nullptr, {"40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 0, 6, 6, Only::GameCube, false, "porpoise-gbav4", false, 3},
			{"gba", "gba_window4", Kind::Choice, TrMark("Screen filter"), TrMark("Sharp or soft pixels. The web page has its own setting."), nullptr, {TrMark("Off"), TrMark("Soft pixels")}, 0, 1, 0, Only::GameCube, false, "porpoise-gbav4", false, 6},
			{"gba", "gba_window4", Kind::Toggle, TrMark("Interframe blending"), TrMark("Blends each frame with the last, for games that flicker sprites. TV only."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbav4", false, 7},
			{"gba", "gba_window1", Kind::Choice, TrMark("Volume"), TrMark("Its sound, on the TV or its player's controller."), nullptr, {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 0, 10, 10, Only::GameCube, false, "porpoise-gbas1", false, 4},
			{"gba", "gba_window1", Kind::Toggle, TrMark("Mute"), TrMark("Turns its sound off."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas1", false, 5},
			{"gba", "gba_window1", Kind::Toggle, TrMark("Sound on the controller"), TrMark("From its player's controller speaker instead of the TV."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas1", false, 8},
			{"gba", "gba_window1", Kind::Choice, TrMark("Speaker resampling"), TrMark("Simple is sharper; Smooth is a little softer."), nullptr, {TrMark("Simple"), TrMark("Smooth")}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas1", false, 9},
			{"gba", "gba_window2", Kind::Choice, TrMark("Volume"), TrMark("Its sound, on the TV or its player's controller."), nullptr, {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 0, 10, 10, Only::GameCube, false, "porpoise-gbas2", false, 4},
			{"gba", "gba_window2", Kind::Toggle, TrMark("Mute"), TrMark("Turns its sound off."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas2", false, 5},
			{"gba", "gba_window2", Kind::Toggle, TrMark("Sound on the controller"), TrMark("From its player's controller speaker instead of the TV."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas2", false, 8},
			{"gba", "gba_window2", Kind::Choice, TrMark("Speaker resampling"), TrMark("Simple is sharper; Smooth is a little softer."), nullptr, {TrMark("Simple"), TrMark("Smooth")}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas2", false, 9},
			{"gba", "gba_window3", Kind::Choice, TrMark("Volume"), TrMark("Its sound, on the TV or its player's controller."), nullptr, {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 0, 10, 10, Only::GameCube, false, "porpoise-gbas3", false, 4},
			{"gba", "gba_window3", Kind::Toggle, TrMark("Mute"), TrMark("Turns its sound off."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas3", false, 5},
			{"gba", "gba_window3", Kind::Toggle, TrMark("Sound on the controller"), TrMark("From its player's controller speaker instead of the TV."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas3", false, 8},
			{"gba", "gba_window3", Kind::Choice, TrMark("Speaker resampling"), TrMark("Simple is sharper; Smooth is a little softer."), nullptr, {TrMark("Simple"), TrMark("Smooth")}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas3", false, 9},
			{"gba", "gba_window4", Kind::Choice, TrMark("Volume"), TrMark("Its sound, on the TV or its player's controller."), nullptr, {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, 0, 10, 10, Only::GameCube, false, "porpoise-gbas4", false, 4},
			{"gba", "gba_window4", Kind::Toggle, TrMark("Mute"), TrMark("Turns its sound off."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas4", false, 5},
			{"gba", "gba_window4", Kind::Toggle, TrMark("Sound on the controller"), TrMark("From its player's controller speaker instead of the TV."), nullptr, {}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas4", false, 8},
			{"gba", "gba_window4", Kind::Choice, TrMark("Speaker resampling"), TrMark("Simple is sharper; Smooth is a little softer."), nullptr, {TrMark("Simple"), TrMark("Smooth")}, 0, 1, 0, Only::GameCube, false, "porpoise-gbas4", false, 9},
		};

		// which of Dolphin's graphics mods a game has, by its ID's first three letters (porpoise_gfxmods.cpp)
		struct GameMods
		{
		    const char *id; /* the game ID's first three letters, or all six */
		    bool bloom, dof, hud;
		    const char *bloom_off, *bloom_blur; /* the game's own bloom mods */
		    const char *extra, *extra_title;    /* the game's own mod */
		};
		constexpr GameMods kGameMods[] = {
#include "../../retro/porpoise_gfxmods.inc"
		};
		const GameMods* ModsOf(const std::string& id)
		{
			for (const GameMods& mods : kGameMods)
				if (id.compare(0, std::strlen(mods.id), mods.id) == 0)
					return &mods;
			return nullptr;
		}
		// the built-in borders' names and labels (porpoise_borders.cpp), then the player's own; not those of
		// Porpoise's own looks (Porpoise, Porpoise Glass, Revolution, Star Cube), which its copy here leaves out
		constexpr const char* kBorders[][2] = {{"", TrMark("None")}, {"midnight", "Midnight"}, {"frost", "Frost"}, {"carbon", "Carbon"},
			{"arcade", TrMark("Arcade Cabinet")}, {"arcade-retro", TrMark("Retro Cabinet")}, {"arcade-synth", TrMark("Synthwave Cabinet")}, {"tv-80s", TrMark("80s TV")},
			{"tv-woodgrain", TrMark("Wood-Grain TV")}, {"tv-90s", TrMark("90s TV")}, {"tv-black", TrMark("Black TV")}, {"monitor-beige", TrMark("Beige Monitor")}};

		constexpr const char* kPrefix = "porpoise:";
		constexpr int kCustomSound = 4; // the sound's keys as no preset has them

		bool IsOn(const std::string& value) { return value == "1" || value == "true" || value == "on" || value == "yes" || value == "enabled"; }

		// Settings::audio_preset, from the values with a game's own over every game's
		int SoundPresetOf(const std::map<std::string, std::string>& values)
		{
			const auto pull = values.find("audio_pull"), fill = values.find("audio_fill"), buffer = values.find("audio_buffer");
			if (pull != values.end() && !IsOn(pull->second))
				return 3;
			if (fill != values.end() && !IsOn(fill->second))
				return kCustomSound;
			const int size = buffer == values.end() ? 1 : std::atoi(buffer->second.c_str());
			return size == 1 ? 0 : size == 0 ? 1 : size == 2 ? 2 : kCustomSound;
		}

		const std::vector<std::string> kSoundKeys = {"audio_pull", "audio_buffer", "audio_fill", "audio_stretch"};

		// Settings::use_audio_preset
		std::map<std::string, std::string> SoundPresetValues(int preset)
		{
			if (preset == 3)
				return {{"audio_pull", "0"}, {"audio_buffer", "1"}, {"audio_stretch", "0"}};
			return {{"audio_pull", "1"}, {"audio_buffer", preset == 1 ? "0" : preset == 2 ? "2" : "1"}, {"audio_fill", "1"}};
		}

		std::string RowId(const Setting& setting)
		{
			return kPrefix + std::string(setting.key) + (setting.part >= 0 ? ":" + std::to_string(setting.part) : "");
		}

		const Setting* Find(const std::string& id)
		{
			if (!id.starts_with(kPrefix))
				return nullptr;
			for (const Setting& setting : kSettings)
				if (id == RowId(setting))
					return &setting;
			return nullptr;
		}

		// The numbers of gba_windowN, as Settings::parse reads them: fewer keep the rest
		std::vector<int> GbaWindow(const std::map<std::string, std::string>& values, const std::string& key)
		{
			std::vector<int> numbers = {-1, -1, 2, 6, 10, 0, 0, 0, 0, 0};
			if (const auto it = values.find(key); it != values.end())
			{
				const char* at = it->second.c_str();
				for (int& number : numbers)
				{
					char* end = nullptr;
					const long value = std::strtol(at, &end, 10);
					if (end == at)
						break;
					number = int(value);
					at = end;
				}
			}
			return numbers;
		}

		// A GBA's games: the files in its port's folder
		std::vector<std::string> GbaGames(char port)
		{
			std::vector<std::string> names;
			if (DIR* d = opendir((ps5porpoise::Data() + "/games/gba/port" + port).c_str()))
			{
				while (const dirent* e = readdir(d))
				{
					std::string name = e->d_name, low = name;
					std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return char(std::tolower(c)); });
					// a new line in a name would be another line of the settings file
					if (low.size() > 4 && name.find_first_of("\r\n") == std::string::npos &&
						(low.ends_with(".gba") || low.ends_with(".agb") || low.ends_with(".bin")))
						names.push_back(std::move(name));
				}
				closedir(d);
			}
			std::sort(names.begin(), names.end());
			return names;
		}
		std::string GbaGameOf(const std::map<std::string, std::string>& values, const std::string& key)
		{
			const auto it = values.find(key);
			return it != values.end() && it->second.size() > 2 ? it->second.substr(2) : "";
		}
		// Which port's GBA the setting is for, or 0
		std::vector<std::string> BorderNames()
		{
			std::vector<std::string> names;
			for (const auto& border : kBorders)
				names.push_back(border[0]);
			std::vector<std::string> mine;
			if (DIR* d = opendir((ps5porpoise::Data() + "/borders").c_str()))
			{
				while (const dirent* e = readdir(d))
				{
					const std::string name = e->d_name;
					if (name.size() > 4 && name[0] != '.' && strcasecmp(name.c_str() + name.size() - 4, ".png") == 0 && name.find_first_of("\r\n") == std::string::npos &&
						std::find(names.begin(), names.end(), name.substr(0, name.size() - 4)) == names.end())
						mine.push_back(name.substr(0, name.size() - 4));
				}
				closedir(d);
			}
			std::sort(mine.begin(), mine.end());
			names.insert(names.end(), mine.begin(), mine.end());
			return names;
		}
		char GbaPort(const Setting& setting)
		{
			const std::string_view key = setting.key;
			return key.starts_with("gba_port") ? key[8] : key.starts_with("gba_window") ? key[10] : key.starts_with("gba_rom") ? key[7] : 0;
		}

		// The value in Porpoise's files, as its Settings::load reads it; fallback where they have none
		int Value(const Setting& setting, const std::map<std::string, std::string>& values, int fallback, bool* found = nullptr)
		{
			const auto it = values.find(setting.key);
			if (found)
				*found = it != values.end();
			if (it == values.end())
				return fallback;
			if (setting.part >= 0)
			{
				if (std::string_view(setting.key).starts_with("gba_rom"))
					return !it->second.empty() && it->second[0] == '1' ? 1 : 0;
				const int number = GbaWindow(values, setting.key)[setting.part];
				if (setting.kind == Kind::Position)
					return number < 0 ? 0 : std::clamp(number / 120 + 1, 1, setting.last); // Automatic, then 0, 120, ...
				return std::clamp(number, setting.first, setting.last);
			}
			if (setting.kind == Kind::Toggle)
			{
				return IsOn(it->second) ? 1 : 0;
			}
			return std::clamp(std::atoi(it->second.c_str()), setting.first, setting.last);
		}
	}

	void Shell::ReadPorpoise()
	{
		const std::string game = InGameSettings() ? m_gameRef.file : "";
		if (m_porpoise.read && m_porpoise.game == game)
			return;
		m_porpoise.read = true;
		m_porpoise.game = game;
		m_porpoise.gameFile = game.empty() ? "" : ps5porpoise::GameFile(game);
		m_porpoise.side = ps5porpoise::Read(ps5porpoise::SettingsFile());
		m_porpoise.own = game.empty() ? std::map<std::string, std::string>{} : ps5porpoise::Read(m_porpoise.gameFile);
	}

	namespace
	{
		// porpoise::pad::Control, in its order: what a Porpoise button set's numbers are
		constexpr const char* kControlNames[] = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Options", "Touchpad click",
			"D-pad up", "D-pad down", "D-pad left", "D-pad right"};
		const std::vector<const char*> kGameCubeButtons = {"A", "B", "X", "Y", "Z", "L", "R", "Start", TrMark("D-pad up"), TrMark("D-pad down"), TrMark("D-pad left"), TrMark("D-pad right")};
		const std::vector<int> kGameCubeDefaults = {0, 2, 1, 3, 5, 6, 7, 10, 12, 13, 14, 15}; // Settings::presets
		// porpoise::pad::GbaInput's order
		const std::vector<const char*> kGbaButtons = {"A", "B", TrMark("Select"), "Start", TrMark("D-pad right"), TrMark("D-pad left"), TrMark("D-pad up"), TrMark("D-pad down"),
			"R", "L", TrMark("Show the GBA's screen"), TrMark("Move the GBA's screen"), TrMark("Adjust the GBA's opacity"), TrMark("Reset the GBA"), TrMark("Mute the GBA"),
			TrMark("GBA volume"), TrMark("GBA link cable"), TrMark("Quick save the GBA"), TrMark("Quick load the GBA")};
		const std::vector<int> kGbaDefaults = {0, 2, -1, 10, 15, 14, 12, 13, 5, 4, 3, 1, 6, -1, -1, 7, -1, -1, -1}; // Settings::gba_buttons
		constexpr int kGbaModifierDefault = 8; // L3
		constexpr const char* kGbaModifierLabel = TrMark("GBA button");

		bool IsGbaSet(const std::string& key) { return key.starts_with("gba_"); }

		std::vector<int> ReadButtonSet(const std::map<std::string, std::string>& values, const std::string& key)
		{
			std::vector<int> set = IsGbaSet(key) ? kGbaDefaults : kGameCubeDefaults;
			if (const auto it = values.find(key); it != values.end())
			{
				const char* at = it->second.c_str();
				for (int& control : set)
				{
					char* end = nullptr;
					const long value = std::strtol(at, &end, 10);
					if (end == at)
						break;
					control = std::clamp(int(value), -1, 15);
					at = end;
				}
			}
			return set;
		}
	}

	namespace
	{
		// porpoise::pad::preset's
		const std::vector<int> kPlayStationLayout = {0, 1, 2, 3, 5, 6, 7, 10, 12, 13, 14, 15};
		const std::vector<int> kGameCubeLayout = {1, 0, 2, 3, 5, 6, 7, 10, 12, 13, 14, 15};
		int HeldInt(const std::map<std::string, std::string>& side, const std::map<std::string, std::string>& own, const char* key, int fallback)
		{
			auto it = own.find(key);
			if (it == own.end())
			{
				it = side.find(key);
				if (it == side.end())
					return fallback;
			}
			return std::atoi(it->second.c_str());
		}
	}

	// The set the Buttons page shows: 0.. in button_layout's or gba_buttons_use's order
	int Shell::PorpoiseLayout()
	{
		ReadPorpoise();
		return m_porpoiseButtons == "gba" ? std::clamp(HeldInt(m_porpoise.side, m_porpoise.own, "gba_buttons_use", 1), 0, 2) :
											std::clamp(HeldInt(m_porpoise.side, m_porpoise.own, "button_layout", 1), 0, 5);
	}

	std::vector<std::string> Shell::PorpoiseLayoutNames() const
	{
		if (m_porpoiseButtons == "gba")
			return {Tr("Default set"), Tr("Mine 1"), Tr("Mine 2")};
		std::vector<std::string> names = {"GameCube", "PlayStation"};
		for (int layout = 1; layout <= 4; layout++)
			names.push_back(TrF("My layout {0}", layout));
		return names;
	}

	bool Shell::PorpoiseLayoutIsOwn()
	{
		return PorpoiseLayout() >= (m_porpoiseButtons == "gba" ? 1 : 2);
	}

	void Shell::SetPorpoiseLayout(int layout)
	{
		SetPorpoise(std::string("porpoise:") + (m_porpoiseButtons == "gba" ? "gba_buttons_use" : "button_layout"), layout);
	}

	namespace
	{
		// the DualSense's buttons a row is for: every control but the touchpad's click, which stays the touchpad's
		constexpr int kRowControls[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15};
		constexpr int kRowControlCount = int(std::size(kRowControls));
	}

	std::vector<ps5emu::ButtonMapping> Shell::PorpoiseMappings()
	{
		ReadPorpoise();
		const bool gba = m_porpoiseButtons == "gba";
		const int layout = PorpoiseLayout();
		std::vector<ps5emu::ButtonMapping> mappings;
		mappings.push_back({Tr("Layout"), PorpoiseLayoutNames()[layout]});
		if (gba)
		{
			const auto& modifierValues = PorpoiseButtonValues("gba_modifier");
			const auto it = modifierValues.find("gba_modifier");
			const int modifier = std::clamp(it == modifierValues.end() ? kGbaModifierDefault : std::atoi(it->second.c_str()), 0, 15);
			mappings.push_back({Tr(kGbaModifierLabel), kControlNames[modifier]});
			const auto& stickValues = PorpoiseButtonValues("gba_stick_dpad");
			const auto stick = stickValues.find("gba_stick_dpad");
			mappings.push_back({Tr("Left stick as the D-pad"), stick != stickValues.end() && IsOn(stick->second) ? Tr("On") : Tr("Off")});
		}
		std::vector<int> set = gba ? kGbaDefaults : layout == 1 ? kPlayStationLayout : kGameCubeLayout;
		if (gba ? layout >= 1 : layout >= 2)
			{
			const std::string key = gba ? (layout == 1 ? "gba_buttons" : "gba_buttons2") : "layout" + std::to_string(layout - 1);
			set = ReadButtonSet(PorpoiseButtonValues(key), key);
		}
		const auto& names = gba ? kGbaButtons : kGameCubeButtons;
		// the DualSense's on the left, what each does on the right
		for (const int control : kRowControls)
		{
			std::string does;
			for (size_t i = 0; i < set.size(); i++)
				if (set[i] == control)
					does += (does.empty() ? "" : ", ") + std::string(Tr(names[i]));
			mappings.push_back({kControlNames[control], does});
		}
		// GameCube's Buttons leads to the GBA's, on the side that can have one
		if (!gba && !ps5retro::Is(SideIndex(), "wii"))
			mappings.push_back({Tr("GBA Buttons"), " "}); // a page, not a button: no Not set
		mappings.push_back({Tr("Clear all"), " "});
		return mappings;
	}

	bool Shell::PorpoiseRowIsControl(int index)
	{
		const int first = m_porpoiseButtons == "gba" ? 3 : 1;
		return index >= first && index < first + kRowControlCount;
	}

	int Shell::PorpoiseExtraRow(int index)
	{
		const bool gba = m_porpoiseButtons == "gba";
		const int after = index - (gba ? 3 : 1) - kRowControlCount;
		if (after < 0)
			return 0;
		const bool gbaRow = !gba && !ps5retro::Is(SideIndex(), "wii");
		return gbaRow && after == 0 ? 1 : 2; // 1 GBA Buttons, 2 Clear all
	}

	// A GBA key's values and where they go: in a game's settings, the game's. Your own GameCube layouts are every game's.
	const std::map<std::string, std::string>& Shell::PorpoiseButtonValues(const std::string& key)
	{
		return InGameSettings() && m_porpoise.own.count(key) ? m_porpoise.own : m_porpoise.side;
	}

	std::string Shell::PorpoiseButtonFile() const
	{
		return InGameSettings() && m_porpoiseButtons == "gba" ? m_porpoise.gameFile : ps5porpoise::SettingsFile();
	}

	void Shell::ClearPorpoiseButtons()
	{
		ReadPorpoise();
		const bool gba = m_porpoiseButtons == "gba";
		const int layout = PorpoiseLayout();
		const std::string key = gba ? (layout == 1 ? "gba_buttons" : "gba_buttons2") : "layout" + std::to_string(layout - 1);
		std::string line;
		for (size_t i = 0; i < (gba ? kGbaDefaults : kGameCubeDefaults).size(); i++)
			line += i ? " -1" : "-1";
		std::string error;
		if (ps5porpoise::Write(PorpoiseButtonFile(), {{key, line}}, error))
			m_porpoise.read = false;
		else
			Toast(TrF("Porpoise's settings could not be saved: {0}", error));
	}

	void Shell::ChoosePorpoiseButton(int index)
	{
		ReadPorpoise();
		const bool gba = m_porpoiseButtons == "gba";
		if (gba && index == 1)
		{
			// the held GBA button: any of the rows' controls
			std::vector<std::string> names;
			for (const int control : kRowControls)
				names.push_back(ps5actions::ButtonName(kControlNames[control]));
			OpenPicker(SystemName(m_side), Tr(kGbaModifierLabel), names, -1, [this](int choice) {
				std::string error;
				if (ps5porpoise::Write(PorpoiseButtonFile(), {{"gba_modifier", std::to_string(kRowControls[choice])}}, error))
					m_porpoise.read = false;
			});
			return;
		}
		if (gba && index == 2)
		{
			const auto& stickValues = PorpoiseButtonValues("gba_stick_dpad");
			const auto stick = stickValues.find("gba_stick_dpad");
			const bool on = stick != stickValues.end() && IsOn(stick->second);
			std::string error;
			if (ps5porpoise::Write(PorpoiseButtonFile(), {{"gba_stick_dpad", on ? "0" : "1"}}, error))
				m_porpoise.read = false;
			return;
		}
		const int control = kRowControls[index - (gba ? 3 : 1)];
		const auto& names = gba ? kGbaButtons : kGameCubeButtons;
		std::vector<std::string> options = {Tr("Nothing")};
		for (const char* name : names)
			options.push_back(Tr(name));
		OpenPicker(SystemName(m_side), ps5actions::ButtonName(kControlNames[control]), options, -1, [this, control](int choice) { SetPorpoiseButton(control, choice - 1); });
	}

	// This DualSense button does input (-1: nothing). An input another button had swaps with what this one did.
	bool Shell::SetPorpoiseButton(int control, int input)
	{
		ReadPorpoise();
		const bool gba = m_porpoiseButtons == "gba";
		if (!PorpoiseLayoutIsOwn())
			return false;
		const int layout = PorpoiseLayout();
		const std::string key = gba ? (layout == 1 ? "gba_buttons" : "gba_buttons2") : "layout" + std::to_string(layout - 1);
		std::vector<int> set = ReadButtonSet(PorpoiseButtonValues(key), key);
		const int before = input >= 0 && input < (int)set.size() ? set[input] : -1;
		for (size_t i = 0; i < set.size(); i++)
			if (set[i] == control && (int)i != input)
			{
				const int to = before; // swap: on what the new one was on, or none
				set[i] = to;
			}
		if (input >= 0 && input < (int)set.size())
			set[input] = control;
		std::string line;
		for (const int number : set)
			line += (line.empty() ? "" : " ") + std::to_string(number);
		std::string error;
		if (!ps5porpoise::Write(PorpoiseButtonFile(), {{key, line}}, error))
		{
			Toast(TrF("Porpoise's settings could not be saved: {0}", error));
			return false;
		}
		m_porpoise.read = false;
		return true;
	}

	namespace
	{
		// as the Wii U's player page (pages.cpp)
		constexpr float kListTop = 240, kListBottom = 1000, kListX = 96, kListWidth = 800, kDetailX = 980, kDetailWidth = 844;
	}

	// A player's page: their rows of Controls, on a page of its own as the Wii U's
	std::vector<Row> Shell::PorpoisePlayerRows()
	{
		const std::string group = "porpoise-player" + std::to_string(m_player + 1);
		std::vector<Row> rows;
		for (Row& row : PorpoiseRows("controls"))
			if (row.group == group)
			{
				row.group.clear();
				rows.push_back(std::move(row));
			}
		if (!InGameSettings())
		{
			Row test;
			test.kind = Row::Kind::Link;
			test.id = "porpoisepadtest";
			test.label = Tr("Controller test");
			test.description = Tr("This player's DualSense input, live.");
			test.help = test.description;
			rows.push_back(std::move(test));
		}
		return rows;
	}

	void Shell::PorpoisePlayerUpdate(const ui::Press& press, const ui::Actions& actions)
	{
		if (m_porpoisePadTest)
		{
			// every button is the test's, so a held Circle is the way out
			if (actions.Held(ui::Button::Circle) && actions.HeldFor(ui::Button::Circle) >= 1.0f)
			{
				m_porpoisePadTest = false;
				m_feedback.Play(ui::Cue::Back);
			}
			return;
		}
		const std::vector<Row> rows = PorpoisePlayerRows();
		m_playerRow = std::clamp(m_playerRow, 0, std::max(0, (int)rows.size() - 1));
		const Button b = press.button;
		if (b == Button::Count)
			return;
		if (b == Button::Circle)
		{
			ClosePage(ScreenId::Settings);
			return;
		}
		if (b == Button::Up || b == Button::Down)
		{
			const int next = m_playerRow + (b == Button::Down ? 1 : -1);
			if (next < 0 || next >= (int)rows.size())
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_playerRow = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			return;
		}
		if (rows.empty())
			return;
		const Row& row = rows[m_playerRow];
		if (b == Button::Triangle)
		{
			OpenHelp(row.label, row.help);
			return;
		}
		if (b != Button::Left && b != Button::Right && b != Button::Cross)
			return;
		if (row.id == "porpoisepadtest")
		{
			if (b == Button::Cross)
			{
				m_porpoisePadTest = true;
				m_feedback.Play(ui::Cue::Select);
			}
			return;
		}
		if (row.id.starts_with("porpoisebuttons:"))
		{
			if (b == Button::Cross)
			{
				m_porpoiseButtons = "gc";
				m_feedback.Play(ui::Cue::Select);
				MappingOpen();
			}
			return;
		}
		ChangePorpoise(row, b == Button::Left ? -1 : b == Button::Right ? 1 : 0, b == Button::Cross);
	}

	void Shell::PorpoisePadTestDraw(Canvas& canvas)
	{
		using namespace ui::tokens;
		ps5pad::Data pad{};
		const bool connected = ps5pad::Read(m_player, pad);
		DrawPageHeader(canvas, std::string(SystemName(m_side)) + " · " + TrF("Player {0}", m_player + 1), Tr("Controller test"),
			connected ? Tr("This DualSense's input, live. Hold Circle to leave.") : Tr("No DualSense for this player: turn it on and sign in. Hold Circle on yours to leave."));
		canvas.PushAlpha(Enter(1));
		static constexpr struct { uint32_t bit; const char* name; } kButtons[] = {
			{ps5pad::kCross, "Cross"}, {ps5pad::kCircle, "Circle"}, {ps5pad::kSquare, "Square"}, {ps5pad::kTriangle, "Triangle"},
			{ps5pad::kL1, "L1"}, {ps5pad::kR1, "R1"}, {ps5pad::kL2, "L2"}, {ps5pad::kR2, "R2"},
			{ps5pad::kL3, "L3"}, {ps5pad::kR3, "R3"}, {ps5pad::kOptions, "Options"}, {ps5pad::kTouchPad, "Touchpad click"},
			{ps5pad::kUp, "D-pad up"}, {ps5pad::kDown, "D-pad down"}, {ps5pad::kLeft, "D-pad left"}, {ps5pad::kRight, "D-pad right"}};
		constexpr float kCell = 200, kRowStep = 76;
		int i = 0;
		for (const auto& button : kButtons)
		{
			const Box box{kListX + (i % 4) * kCell, kListTop + (i / 4) * kRowStep, kCell - 12, kRowStep - 12};
			const bool on = connected && (pad.buttons & button.bit);
			canvas.Rect(box, 12, on ? Accent() : Surface());
			canvas.Text(Style(kCaptionStyle), box.CentreX(), box.y + 18, ps5actions::ButtonName(button.name), on ? kInk1 : kText, box.w - 12, 1, ui::Align::Centre);
			i++;
		}
		canvas.PopAlpha();
		canvas.PushAlpha(Enter(2));
		const Box panel{kDetailX, kListTop, kDetailWidth, 620};
		Panel(canvas, panel, kRadiusSheet);
		const auto stick = [&](float x, const char* label, uint8_t sx, uint8_t sy) {
			const Box area{x, panel.y + 70, 200, 200};
			canvas.Text(Style(kOverlineStyle), x, panel.y + 32, Tr(label), Tertiary(), 200, 1);
			canvas.Ring(area, 100, 2, Wash(0.3f));
			const float dx = (sx - 127.5f) / 127.5f * 88, dy = (sy - 127.5f) / 127.5f * 88;
			canvas.Rect({area.CentreX() + dx - 12, area.CentreY() + dy - 12, 24, 24}, 12, Accent());
		};
		stick(panel.x + 60, TrMark("Left stick"), pad.leftX, pad.leftY);
		stick(panel.x + 320, TrMark("Right stick"), pad.rightX, pad.rightY);
		const auto bar = [&](float y, const char* label, uint8_t value) {
			canvas.Text(Style(kCaptionStyle), panel.x + 60, y, label, kText, 80, 1);
			const Box track{panel.x + 140, y + 4, 400, 20};
			canvas.Rect(track, 10, Wash(0.15f));
			canvas.Rect({track.x, track.y, track.w * (value / 255.0f), track.h}, 10, Accent());
		};
		bar(panel.y + 300, "L2", pad.l2);
		bar(panel.y + 345, "R2", pad.r2);
		ui::TextStyle numbers = Style(kCaptionStyle);
		numbers.tabular = true;
		canvas.Text(numbers, panel.x + 60, panel.y + 400, fmt::format("{} {:+.2f}  {:+.2f}  {:+.2f}", Tr("Motion (g):"), pad.acceleration[0], pad.acceleration[1], pad.acceleration[2]), kText, panel.w - 120, 1);
		canvas.Text(numbers, panel.x + 60, panel.y + 440, TrF("Fingers on the touchpad: {0}", (int)pad.touchCount), kText, panel.w - 120, 1);
		canvas.PopAlpha();
	}

	void Shell::PorpoisePlayerDraw(Canvas& canvas)
	{
		using namespace ui::tokens;
		if (m_porpoisePadTest)
		{
			PorpoisePadTestDraw(canvas);
			return;
		}
		const std::vector<Row> rows = PorpoisePlayerRows();
			DrawPageHeader(canvas, std::string(SystemName(m_side)) + " · " + Tr("Controls"), TrF("Player {0}", m_player + 1),
				m_player == 0 || ps5pad::IsConnected(m_player) ? Tr("This player's DualSense.") : Tr("No DualSense for this player yet."));
		canvas.PushAlpha(Enter(1));
		constexpr float kStep = 100, kHeight = 84;
		const float scroll = std::max(0.0f, (m_playerRow + 1) * kStep - (kListBottom - kListTop));
		canvas.PushClip({kListX - 20, kListTop - 12, kListWidth + 40, kListBottom - kListTop + 24});
		const float labelScale = LabelScale(rows, kListWidth);
		for (int i = 0; i < (int)rows.size(); i++)
			DrawRow(canvas, rows[i], {kListX, kListTop + i * kStep - scroll, kListWidth, kHeight}, i == m_playerRow, labelScale);
		canvas.PopClip();
		canvas.PopAlpha();
		canvas.PushAlpha(Enter(2));
		const Box panel{kDetailX, kListTop, kDetailWidth, 420};
		Panel(canvas, panel, kRadiusSheet);
		canvas.Text(Style(kOverlineStyle), panel.x + 36, panel.y + 32, Tr("Players"), Tertiary(), panel.w - 72, 1);
		for (int player = 0; player < ps5pad::kMaxPlayers; player++)
		{
			const bool connected = player == 0 || ps5pad::IsConnected(player);
			const float y = panel.y + 80 + player * 80;
			const Box box{panel.x + 36, y, 56, 40};
			if (player == m_player)
				canvas.Rect(box, 10, Accent());
			else
				canvas.Ring(box, 10, 2, connected ? Wash(0.5f) : Wash(0.2f));
			canvas.Text(Style({18, ui::Weight::Bold, 1.0f}), box.CentreX(), box.y + 10, TrF("P{0}", player + 1), player == m_player ? kInk1 : Secondary(), 0, 0, ui::Align::Centre);
			canvas.Text(Style(kBodyStyle), panel.x + 116, y + 2, connected ? Tr("Connected") : Tr("No DualSense"), connected ? kText : Tertiary(), 420, 1);
		}
		canvas.PopAlpha();
	}

	std::vector<Row> Shell::PorpoiseRows(const std::string& page)
	{
		ReadPorpoise();
		const bool wii = ps5retro::Is(SideIndex(), "wii");
		std::vector<Row> rows;
		std::string opened; // the › row last made
		const std::string gameId = InGameSettings() ? ps5porpoise::GameKey(m_gameRef.file) : "";
		const GameMods* mods = gameId.empty() ? nullptr : ModsOf(gameId);
		for (const Setting& setting : kSettings)
		{
			if (page != setting.page || (setting.only == Only::Wii && !wii) || (setting.only == Only::GameCube && wii) ||
				(InGameSettings() && setting.everyGame) || (!InGameSettings() && setting.gameOnly))
				continue;
			// a game's mods: only those it has
			const std::string_view keyName = setting.key;
			if (keyName.starts_with("gfx_"))
			{
				const std::string_view extra = mods ? mods->extra_title : "";
				const bool has = mods && (keyName == "gfx_bloom" ? (mods->bloom || mods->bloom_off[0]) : keyName == "gfx_dof" ? mods->dof : keyName == "gfx_hud" ? mods->hud : !extra.empty());
				if (!has)
					continue;
			}
			// the sound's two ways: gaps filled with Dolphin's mixer, stretched in Classic's
			if (keyName == "audio_fill" || keyName == "audio_stretch")
			{
				const auto own = m_porpoise.own.find("audio_pull"), side = m_porpoise.side.find("audio_pull");
				const bool pull = InGameSettings() && own != m_porpoise.own.end() ? IsOn(own->second) : side == m_porpoise.side.end() || IsOn(side->second);
				if (pull != (keyName == "audio_fill"))
					continue;
			}
			// a port's rows: as what it holds; a GBA's picture and sound stay on their pages either way
			if (std::string_view(setting.key).starts_with("gc_port") && std::string_view(setting.key).ends_with("_player"))
			{
				const std::string device = std::string("gc_port") + setting.key[7] + "_device";
				const auto own = m_porpoise.own.find(device), side = m_porpoise.side.find(device);
				const std::string held = InGameSettings() && own != m_porpoise.own.end() ? own->second : side != m_porpoise.side.end() ? side->second : "0";
				if (held != "0")
					continue;
			}
			// a port's GBA: only with a GBA on that port (Settings::port_has_gba)
			if (setting.kind == Kind::Position)
			{
				const std::string corner = std::string("gba_port") + setting.key[10] + "_corner";
				const auto own = m_porpoise.own.find(corner), side = m_porpoise.side.find(corner);
				const std::string held = InGameSettings() && own != m_porpoise.own.end() ? own->second : side != m_porpoise.side.end() ? side->second : "0";
				if (held != "5")
					continue;
			}
			if (const char port = GbaPort(setting))
			{
				const std::string device = std::string("gc_port") + port + "_device";
				const auto own = m_porpoise.own.find(device), side = m_porpoise.side.find(device);
				const std::string held = InGameSettings() && own != m_porpoise.own.end() ? own->second : side != m_porpoise.side.end() ? side->second : "0";
				if (held != "1")
					continue;
			}
			// its › row, before the first of its settings this side has
			if (setting.group && opened != setting.group)
			{
				opened = setting.group;
				for (const Group& group : kGroups)
					if (opened == group.id)
					{
						Row link;
						link.kind = Row::Kind::Link;
						link.id = "sub";
						link.opens = group.id;
						// a GBA's video or sound, port by port, on a page of their own
						if (std::string_view(group.id).starts_with("porpoise-gbav") || std::string_view(group.id).starts_with("porpoise-gbas"))
						{
							const std::string_view id = group.id;
							if (id != "porpoise-gbavideo" && id != "porpoise-gbasound")
							{
								const std::string parent = id[12] == 'v' ? "porpoise-gbavideo" : "porpoise-gbasound";
								if (std::none_of(rows.begin(), rows.end(), [&](const Row& row) { return row.opens == parent; }))
								{
									Row sound;
									sound.kind = Row::Kind::Link;
									sound.id = "sub";
									sound.opens = parent;
									sound.label = id[12] == 'v' ? Tr("GBA video") : Tr("GBA sound");
									sound.description = id[12] == 'v' ? Tr("Each GBA's screen on the TV.") : Tr("Each GBA's sound.");
									sound.help = sound.description;
									rows.push_back(std::move(sound));
								}
								link.group = parent;
							}
						}
						// a port's: its own page, under Ports, its row saying what it holds and who plays it
						if (std::string_view(group.id).starts_with("porpoise-port"))
						{
							const char port = group.id[13];
							if (port == '1')
							{
								Row ports;
								ports.kind = Row::Kind::Link;
								ports.id = "sub";
								ports.opens = "porpoise-ports";
								ports.label = Tr("Ports");
								ports.description = Tr("What's plugged into each controller port, and who plays it.");
								ports.help = ports.description;
								rows.push_back(std::move(ports));
							}
							const auto valueOf = [&](const std::string& key) {
								auto it = m_porpoise.own.find(key);
								if (!InGameSettings() || it == m_porpoise.own.end())
								{
									it = m_porpoise.side.find(key);
									if (it == m_porpoise.side.end())
										return 0;
								}
								return std::atoi(it->second.c_str());
							};
							const int device = std::clamp(valueOf(std::string("gc_port") + port + "_device"), 0, 2);
							const int number = port - '0';
							std::string who;
							if (device == 1)
							{
								const int control = std::clamp(valueOf(std::string("gba_port") + port + "_control"), 0, 5);
								who = control < 4 ? TrF("Player {0}", control + 1) : control == 4 ? std::string(Tr("Its own controller")) : std::string(Tr("None"));
							}
							else if (device == 0)
							{
								const bool set = m_porpoise.side.count(std::string("gc_port") + port + "_player") || m_porpoise.own.count(std::string("gc_port") + port + "_player");
								const int player = set ? std::clamp(valueOf(std::string("gc_port") + port + "_player"), 0, 4) : number - 1;
								who = player < 4 ? TrF("Player {0}", player + 1) : std::string(Tr("None"));
							}
							link.group = "porpoise-ports";
							link.value = (device == 0 ? std::string(Tr("GameCube Controller")) : device == 1 ? std::string(Tr("GBA (Integrated)")) : std::string(Tr("None"))) +
								(who.empty() ? "" : " · " + who);
						}
						// a player's: its own page, as the Wii U's, for a controller that's connected
						if (std::string_view(group.id).starts_with("porpoise-player"))
						{
							const int player = group.id[15] - '1';
							// player 1 is whoever started the app: always there
							const bool connected = player == 0 || ps5pad::IsConnected(player);
							link.id = "porpoiseplayer";
							link.opens.clear();
							link.index = player;
							link.dimmed = !connected;
							link.value = connected ? std::string() : std::string(Tr("No DualSense"));
						}
						link.label = Tr(group.label);
						link.description = Tr(group.description);
						link.help = link.description;
						rows.push_back(std::move(link));
					}
			}
			bool own = false;
			const int sideValue = Value(setting, m_porpoise.side, setting.fallback);
			int value = InGameSettings() ? Value(setting, m_porpoise.own, sideValue, &own) : sideValue;
			if (setting.kind == Kind::SoundPreset)
			{
				std::map<std::string, std::string> values = m_porpoise.side;
				for (const std::string& key : kSoundKeys)
					if (const auto it = m_porpoise.own.find(key); it != m_porpoise.own.end())
					{
						values[key] = it->second;
						own = true;
					}
				value = SoundPresetOf(values);
			}
			Row row;
			row.id = RowId(setting);
			row.setting = row.id;
			row.fromSide = !own;
			row.group = setting.group ? setting.group : "";
			row.label = Tr(setting.label);
			row.description = Tr(setting.description);
			row.help = Tr(setting.help ? setting.help : setting.description);
			if (setting.kind == Kind::Toggle)
			{
				row.kind = Row::Kind::Toggle;
				row.on = value != 0;
				row.value = row.on ? Tr("On") : Tr("Off");
			}
			else if (setting.kind == Kind::Border)
			{
				row.kind = Row::Kind::Choice;
				const auto it = own ? m_porpoise.own.find("border") : m_porpoise.side.find("border");
				const std::string current = it != (own ? m_porpoise.own : m_porpoise.side).end() ? it->second : "";
				const std::vector<std::string> names = BorderNames();
				for (size_t i = 0; i < names.size(); i++)
				{
					row.options.push_back(i < std::size(kBorders) ? std::string(Tr(kBorders[i][1])) : names[i]);
					if (names[i] == current)
						row.index = (int)i;
				}
				row.value = row.options[row.index];
			}
			else if (setting.kind == Kind::GbaGame)
			{
				row.kind = Row::Kind::Choice;
				const std::string current = GbaGameOf(own ? m_porpoise.own : m_porpoise.side, setting.key);
				row.options.push_back(Tr("None (BIOS)"));
				for (const std::string& name : GbaGames(GbaPort(setting)))
				{
					if (name == current)
						row.index = (int)row.options.size();
					row.options.push_back(name);
				}
				if (!current.empty() && row.index == 0)
				{
					row.index = (int)row.options.size();
					// tr: {0} is a file kept in the settings, not in its folder now
					row.options.push_back(TrF("{0} (missing)", current));
				}
				row.value = row.options[row.index];
			}
			else if (setting.kind == Kind::Percent)
			{
				row.kind = Row::Kind::Slider;
				row.fraction = value / 10.0f;
				row.value = ps5lang::Percent(value * 10);
			}
			else
			{
				row.kind = Row::Kind::Choice;
				for (const char* option : setting.options)
					row.options.push_back(Tr(option));
				// as long as Dolphin's mixer keeps it; Classic's has no such measure (Settings::audio_buffer_ms)
				if (std::string_view(setting.key) == "audio_buffer")
				{
					const auto ownPull = m_porpoise.own.find("audio_pull"), sidePull = m_porpoise.side.find("audio_pull");
					if (InGameSettings() && ownPull != m_porpoise.own.end() ? IsOn(ownPull->second) : sidePull == m_porpoise.side.end() || IsOn(sidePull->second))
						row.options = {"40 ms", "80 ms", "160 ms"};
				}
				// your own button layouts, made in Porpoise's Customize Buttons
				if (setting.kind == Kind::SoundPreset && value == kCustomSound)
					row.options.push_back(Tr("Custom"));
				for (int layout = 1; setting.key == std::string_view("button_layout") && layout <= 4; layout++)
					// tr: a button layout of the player's own, made in Porpoise's Customize Buttons: {0} is its number
					row.options.push_back(TrF("My layout {0}", layout));
				row.index = std::clamp(value - setting.first, 0, (int)row.options.size() - 1);
				row.value = row.options[row.index];
			}
			rows.push_back(std::move(row));
		}
		// Custom textures' page: its switch, where packs go, and those found
		if (page == "graphics")
		{
			const auto at = std::find_if(rows.begin(), rows.end(), [](const Row& row) { return row.id == "porpoise:custom_textures"; });
			if (at != rows.end())
			{
				Row toggle = *at;
				toggle.group = "porpoise-textures";
				rows.push_back(std::move(toggle));
				Row folder;
				folder.kind = Row::Kind::Action;
				folder.id = "porpoisetextures-folder";
				folder.group = "porpoise-textures";
				folder.label = Tr("Pack folder");
				folder.value = "Load/Textures";
				folder.description = Tr("Each pack goes in /data/porpoise/saves/User/Load/Textures, in a folder named with the game's ID, like GALE01.");
				folder.help = folder.description;
				rows.push_back(std::move(folder));
				if (!InGameSettings())
				{
					std::vector<std::string> packs;
					if (DIR* d = opendir((ps5porpoise::Data() + "/saves/User/Load/Textures").c_str()))
					{
						while (const dirent* e = readdir(d))
							if (e->d_name[0] != '.')
								packs.push_back(e->d_name);
						closedir(d);
					}
					std::sort(packs.begin(), packs.end());
					for (const std::string& name : packs)
					{
						Row pack;
						pack.kind = Row::Kind::Action;
						pack.id = "porpoisetextures-pack";
						pack.group = "porpoise-textures";
						pack.label = name;
						pack.description = Tr("A texture pack. Custom textures turns packs on or off.");
						pack.help = pack.description;
						rows.push_back(std::move(pack));
					}
					if (packs.empty())
					{
						Row none;
						none.kind = Row::Kind::Action;
						none.dimmed = true;
						none.id = "porpoisetextures-none";
						none.group = "porpoise-textures";
						none.label = Tr("No packs found");
						rows.push_back(std::move(none));
					}
				}
			}
		}
		// the buttons, at the bottom: their page chooses the layout too, as the other sides' Buttons
		if (page == "controls" && (!wii || InGameSettings()))
		{
			Row link;
			link.kind = Row::Kind::Link;
			link.id = "porpoisebuttons:gc";
			link.group = "porpoise-player1";
			link.label = Tr("Buttons");
			link.description = Tr("Which layout, and what each DualSense button does.");
			link.help = link.description;
			rows.push_back(std::move(link));
		}
		return rows;
	}

	bool Shell::SetPorpoise(const std::string& id, int value)
	{
		const Setting* setting = Find(id);
		if (!setting)
			return false;
		ReadPorpoise();
		std::string error;
		const std::string file = InGameSettings() ? m_porpoise.gameFile : ps5porpoise::SettingsFile();
		if (setting->kind == Kind::SoundPreset && value == kCustomSound)
			return false; // shown when the keys are so, never chosen
		std::map<std::string, std::string> values = setting->kind == Kind::SoundPreset ? SoundPresetValues(value) :
			std::map<std::string, std::string>{{setting->key, std::to_string(value)}};
		if (setting->kind == Kind::Border)
		{
			const std::vector<std::string> names = BorderNames();
			if (value < 0 || value >= (int)names.size())
				return false;
			values["border"] = names[value];
		}
		// Automatic: back to its own corner, wherever it was moved to
		if (std::string_view(setting->key).ends_with("_corner") && value == 0)
		{
			const std::string window = std::string("gba_window") + setting->key[8];
			std::vector<int> numbers = GbaWindow(InGameSettings() && m_porpoise.own.count(window) ? m_porpoise.own : m_porpoise.side, window);
			numbers[0] = numbers[1] = -1;
			std::string line;
			for (const int number : numbers)
				line += (line.empty() ? "" : " ") + std::to_string(number);
			values[window] = line;
		}
		if (setting->part >= 0)
		{
			// the rest of the key as it is where this is written, else as every game has it
			const bool game = InGameSettings() && m_porpoise.own.count(setting->key);
			const auto& from = game ? m_porpoise.own : m_porpoise.side;
			if (std::string_view(setting->key).starts_with("gba_rom"))
			{
				const auto it = from.find(setting->key);
				const bool beside = it != from.end() && !it->second.empty() && it->second[0] == '1';
				std::string name = GbaGameOf(from, setting->key);
				if (setting->kind == Kind::GbaGame)
				{
					const std::vector<std::string> names = GbaGames(GbaPort(*setting));
					if (value > (int)names.size())
						return false; // the missing one, kept
					name = value == 0 ? "" : names[value - 1];
				}
				values[setting->key] = std::string((setting->kind == Kind::GbaGame ? beside : value != 0) ? "1 " : "0 ") + name;
			}
			else
			{
				std::vector<int> numbers = GbaWindow(from, setting->key);
				numbers[setting->part] = setting->kind == Kind::Position ? (value == 0 ? -1 : (value - 1) * 120) : value;
				std::string line;
				for (const int number : numbers)
					line += (line.empty() ? "" : " ") + std::to_string(number);
				values[setting->key] = line;
			}
		}
		if (!ps5porpoise::Write(file, values, error))
		{
			m_feedback.Play(ui::Cue::Denied);
			// tr: {0} is why, in the system's words
			Toast(TrF("Porpoise's settings could not be saved: {0}", error));
			return false;
		}
		m_porpoise.read = false;
		m_feedback.Play(setting->kind == Kind::Toggle ? ui::Cue::Toggle : ui::Cue::Focus);
		return true;
	}

	bool Shell::ChangePorpoise(const Row& row, int step, bool cross)
	{
		const Setting* setting = Find(row.id);
		if (!setting)
			return false;
		// Custom textures' row: Left and Right its switch, Cross its page; the page has the same switch
		if (cross && row.group == "porpoise-enhancements" && std::string_view(setting->key) == "custom_textures")
		{
			Row opens = row;
			opens.opens = "porpoise-textures";
			OpenSubPage(opens);
			return false;
		}
		if (setting->kind == Kind::Toggle)
		{
			if (!cross && step != 0 && ((step > 0) == row.on))
			{
				m_feedback.Play(ui::Cue::Edge);
				return false;
			}
			return SetPorpoise(row.id, row.on ? 0 : 1);
		}
		if (setting->kind == Kind::Choice || setting->kind == Kind::SoundPreset || setting->kind == Kind::GbaGame || setting->kind == Kind::Position || setting->kind == Kind::Border)
		{
			if (cross)
			{
				// the dropdown: its choice is written where the row's value lives (every game's, or the game's)
				const Row chosen = row;
				OpenPicker(InGameSettings() ? m_gameName : SystemName(m_side), row.label, row.options, row.index,
					[this, chosen](int index) { SetPorpoise(chosen.id, Find(chosen.id)->first + index); });
				return false;
			}
			const int count = setting->kind == Kind::SoundPreset ? setting->last - setting->first + 1 : (int)row.options.size(); // never onto Custom
			return SetPorpoise(row.id, setting->first + (row.index + step + count) % count);
		}
		const int value = (int)(row.fraction * 10 + 0.5f);
		int next = value + (cross ? 1 : step);
		if (cross && next > setting->last)
			next = setting->first;
		if (next < setting->first || next > setting->last)
		{
			m_feedback.Play(ui::Cue::Edge);
			return false;
		}
		return SetPorpoise(row.id, next);
	}

	void Shell::ResetAllPorpoise()
	{
		// As Porpoise's Reset All Settings: its folders, the player's own layouts and each game's settings stay
		std::vector<std::string> keys;
		for (const Setting& setting : kSettings)
			keys.push_back(setting.key);
		std::string error;
		if (ps5porpoise::Remove(ps5porpoise::SettingsFile(), keys, error))
		{
			m_porpoise.read = false;
			m_feedback.Play(ui::Cue::Toggle);
			Toast(Tr("Done."));
		}
		else
		{
			m_feedback.Play(ui::Cue::Denied);
			Toast(TrF("Porpoise's settings could not be saved: {0}", error));
		}
	}

	bool Shell::ResetPorpoise(const Row& row)
	{
		const Setting* setting = Find(row.id);
		ReadPorpoise();
		std::string error;
		if (!setting || !InGameSettings())
		{
			m_feedback.Play(ui::Cue::Edge);
			return false;
		}
		const std::vector<std::string> keys = setting->kind == Kind::SoundPreset ? kSoundKeys : std::vector<std::string>{setting->key};
		if (std::none_of(keys.begin(), keys.end(), [this](const std::string& key) { return m_porpoise.own.count(key) != 0; }))
		{
			m_feedback.Play(ui::Cue::Edge);
			return false;
		}
		if (!ps5porpoise::Remove(m_porpoise.gameFile, keys, error))
		{
			m_feedback.Play(ui::Cue::Denied);
			Toast(TrF("Porpoise's settings could not be saved: {0}", error));
			return false;
		}
		m_porpoise.read = false;
		m_feedback.Play(ui::Cue::Back);
		return true;
	}

	namespace
	{
		// as a backup's folder is named (saves.cpp)
		std::string BackupStamp()
		{
			const std::time_t now = std::time(nullptr);
			char stamp[32] = "";
			std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H%M%S", std::localtime(&now));
			return stamp;
		}
	}

	void Shell::PorpoiseCardAction(const std::string& id)
	{
		const size_t at = id.find(':');
		const size_t c = (size_t)std::atoi(id.c_str() + at + 1);
		if (c >= m_porpoiseSaves.size())
			return;
		const ps5titlesaves::Container card = m_porpoiseSaves[c];
		const std::string saves = ps5porpoise::Data() + "/saves", drive = ps5porpoise::FirstUsbDrive();
		if (id.starts_with("porpoisecardsave:"))
		{
			const auto found = ps5porpoise::FindUsbSaves(drive, card.id == "wii");
			if (found.empty())
			{
				Toast(Tr("No saves on a USB drive."));
				return;
			}
			std::vector<std::string> names;
			for (const auto& save : found)
				names.push_back(save.label);
			OpenPicker(SystemName(m_side), Tr("Copy a save from USB"), names, -1, [this, found, card, saves](int choice) {
				std::string error;
				if (ps5porpoise::ImportOneSave(saves, found[choice], card.folder, error))
					Toast(Tr("Done."));
				else
					Toast(TrF("The save could not be copied: {0}", error));
				m_porpoiseSavesRead = false;
			});
		}
		else if (id.starts_with("porpoisecardload:"))
		{
			const auto found = ps5porpoise::FindUsbCards(drive);
			if (found.empty())
			{
				Toast(Tr("No cards on a USB drive."));
				return;
			}
			std::vector<std::string> names;
			for (const auto& one : found)
				names.push_back(one.label);
			OpenPicker(SystemName(m_side), Tr("Copy a card from USB"), names, -1, [this, found, card](int choice) {
				ps5porpoise::Imported imported;
				std::string error;
				if (ps5porpoise::ImportCard(found[choice], card.folder, imported, error))
					Toast(TrF("{0} saves copied, {1} already here", imported.gameCube, imported.skipped));
				else
					Toast(TrF("The saves could not be copied: {0}", error));
				m_porpoiseSavesRead = false;
			});
		}
		else
			OpenPicker(SystemName(m_side), Tr("Delete every save on this card"), {Tr("Keep them"), Tr("Delete")}, -1, [this, card, saves](int choice) {
				if (choice != 1)
					return;
				const std::string stamp = BackupStamp();
				std::string error;
				for (const auto& save : card.saves)
					ps5porpoise::MoveSaveAside(saves, save, stamp, error);
				Toast(Tr("Done."));
				m_porpoiseSavesRead = false;
			});
	}

	void Shell::CopyPorpoiseCard(const Row& row)
	{
		size_t c = 0;
		if (std::sscanf(row.id.c_str(), "porpoisecard:%zu", &c) != 1 || c >= m_porpoiseSaves.size())
			return;
		std::string where, error;
		if (ps5porpoise::ExportCard(ps5porpoise::Data() + "/saves", m_porpoiseSaves[c], ps5porpoise::FirstUsbDrive(), BackupStamp(), where, error))
			Toast(TrF("Copied to {0}", where));
		else
			Toast(TrF("The save could not be copied: {0}", error));
	}

	// Its own picture beside it first (ps5retro::CoverPath), then Porpoise's download, then GameTDB's
	std::string Shell::PorpoiseCover(const ps5emu::Game& game) const
	{
		std::string cover = ps5retro::CoverPath(SideIndex(), game);
		if (!cover.empty() || game.gameId.empty())
			return cover;
		const std::string own = ps5porpoise::Data() + "/covers/" + game.gameId + ".png";
		struct stat st;
		if (stat(own.c_str(), &st) == 0)
			return own;
		return ps5boxart::Path(ps5boxart::System::GameCubeWii, game.gameId);
	}

	std::vector<Row> Shell::PorpoiseOnlineRows(const ps5settings::Launcher& settings)
	{
		std::vector<Row> rows;
		int missing = 0;
		for (const Game& game : m_games)
			missing += !game.entry.game.gameId.empty() && game.boxArt.empty();
		const size_t pending = ps5boxart::Pending();
		const std::string status = pending ? TrP((long long)pending, "Downloading: {0} left", "Downloading: {0} left") :
			ps5boxart::Answered() == 0 ? std::string(Tr("GameTDB did not answer: is the PS5 online?")) :
			missing ? TrP(missing, "{0} game without box art", "{0} games without box art") : std::string(Tr("Every game has box art"));
		// (RetroAchievements, which Porpoise is told of as a game starts, and the SteamGridDB key: on every side's
		// Accounts and updates)
		Row toggle;
		toggle.kind = Row::Kind::Toggle;
		toggle.id = "boxart";
		toggle.label = Tr("Box art from GameTDB");
		toggle.on = settings.boxArt;
		toggle.value = toggle.on ? Tr("On") : Tr("Off");
		toggle.description = Tr("Covers for the library, downloaded once.");
		toggle.help = toggle.description;
		rows.push_back(std::move(toggle));
		rows.push_back(CheatsAutoRow(settings)); // every game's cheats (cheat_rows.cpp)
		Row missingRow;
		missingRow.kind = Row::Kind::Action;
		missingRow.id = "boxartmissing";
		missingRow.label = Tr("Download missing box art");
		missingRow.value = status;
		missingRow.description = Tr("Tries again for every game without a cover.");
		missingRow.help = missingRow.description;
		rows.push_back(std::move(missingRow));
		Row all;
		all.kind = Row::Kind::Hold;
		all.id = "boxartall";
		all.label = Tr("Download all box art again");
		all.value = m_message.empty() ? Tr("Hold Cross") : m_message;
		all.description = Tr("Downloads every cover for this system again.");
		all.help = all.description;
		rows.push_back(std::move(all));
		return rows;
	}

	void Shell::ReadPorpoiseGame()
	{
		ReadPorpoise();
		if (m_porpoiseGame.file == m_gameRef.file)
			return;
		const std::string sys = "/app0/retro/porpoise/system/dolphin-emu/Sys/GameSettings", data = ps5porpoise::Data();
		m_porpoiseGame = {};
		m_porpoiseGame.file = m_gameRef.file;
		m_porpoiseGame.id = ps5porpoise::GameKey(m_gameRef.file);
		m_porpoiseGame.cheats = ps5porpoise::CheatsFor(sys, data + "/cheats", m_porpoiseGame.id, "/app0/retro/porpoise/assets/widescreen", ps5retro::Is(SideIndex(), "wii"));
		m_porpoiseGame.fixes = ps5porpoise::DolphinFixes(sys, m_porpoiseGame.id);
		m_porpoiseGame.hasPick = ps5porpoise::PickFor(data + "/recommended.ini", "/app0/retro/porpoise/assets/recommended.ini", m_porpoiseGame.id, m_porpoiseGame.pick);
		m_porpoiseGame.ownDolphinFile = ps5porpoise::HasOwnDolphinFile(data + "/saves", m_porpoiseGame.id);
	}

	namespace
	{
		// a row of a game's own page: Settings shows a game's rows that have a setting
		Row GameRow(Row::Kind kind, const std::string& id, const std::string& label, const std::string& description)
		{
			Row row;
			row.kind = kind;
			row.id = "porpoisegame:" + id;
			row.setting = row.id;
			row.fromSide = true;
			row.label = label;
			row.description = description;
			row.help = description;
			return row;
		}
	}

	std::vector<Row> Shell::PorpoiseSetupRows()
	{
		ReadPorpoiseGame();
		std::vector<Row> rows;
		for (int i = 0; i < ps5porpoise::kSetups; i++)
		{
			const ps5porpoise::Setup setup = ps5porpoise::GetSetup(ps5porpoise::Data(), i);
			if (!setup.exists)
				continue;
			// tr: a saved set of Graphics settings: {0} is its number
			Row use = GameRow(Row::Kind::Action, "use:" + std::to_string(i), TrF("Use setup {0}", i + 1), Tr("Applies this setup's Graphics settings to this game."));
			use.value = setup.from;
			rows.push_back(std::move(use));
		}
		rows.push_back(GameRow(Row::Kind::Action, "save", Tr("Save as a setup"), Tr("Saves this game's Graphics settings to use on other games.")));
		return rows;
	}

	std::vector<Row> Shell::PorpoiseCheatRows()
	{
		CheatsRefresh(); // a download's codes read again
		ReadPorpoiseGame();
		std::vector<Row> rows;
		const auto& cheats = m_porpoiseGame.cheats;
		{
			const bool wii = ps5retro::Is(SideIndex(), "wii");
			const ps5porpoise::WideKind kind = ps5porpoise::WidescreenKind("/app0/retro/porpoise/system/dolphin-emu/Sys/GameSettings", "/app0/retro/porpoise/assets/widescreen", m_porpoiseGame.id, wii);
			Row wide = GameRow(Row::Kind::Action, "widestatus", Tr("Widescreen"),
				wii ? Tr("Wii games are 16:9 when Wii widescreen is on in Settings.") :
				kind == ps5porpoise::WideKind::Patch ? Tr("This game has a widescreen code: Automatic and On show it in 16:9.") :
				kind == ps5porpoise::WideKind::Native ? Tr("This game has its own 16:9 option. Turn it on in the game's options.") :
				Tr("This game has no widescreen code. On uses a widescreen hack, which can glitch at the edges."));
			wide.value = wii ? Tr("The Wii's setting") : kind == ps5porpoise::WideKind::Patch ? Tr("16:9 code") : kind == ps5porpoise::WideKind::Native ? Tr("In the game's options") : Tr("4:3 only");
			wide.dimmed = true;
			rows.push_back(std::move(wide));
			if (m_porpoiseGame.id.size() == 6)
			{
				rows.push_back(CheatsGetRow());
				Row add = GameRow(Row::Kind::Action, "addcode", Tr("Your own code"), Tr("Type a Gecko code and a name. It's saved in /data/porpoise/cheats and turned on."));
				add.value = Tr("Add a code");
				rows.push_back(std::move(add));
			}
		}
		const size_t before = rows.size();
		for (size_t i = 0; i < cheats.size() && i < 80; i++)
		{
			const auto& cheat = cheats[i];
			const bool on = cheat.defaultOn ? !m_porpoise.own.count(ps5porpoise::CheatKey(cheat, false)) : m_porpoise.own.count(ps5porpoise::CheatKey(cheat, true)) != 0;
			Row row = GameRow(Row::Kind::Toggle, "cheat:" + std::to_string(i), cheat.name.size() > 1 ? cheat.name.substr(1) : cheat.name,
				cheat.own ? Tr("Your code, from /data/porpoise/cheats.") :
				cheat.pack ? Tr("From the widescreen collection for this game.") :
				cheat.kind == "OnFrame" ? Tr("A patch for this game.") : Tr("A cheat for this game."));
			row.on = on;
			row.value = on ? Tr("On") : Tr("Off");
			rows.push_back(std::move(row));
		}
		if (rows.size() == before)
		{
			Row none = GameRow(Row::Kind::Action, "none", Tr("No codes for this game"), Tr("Put your own in /data/porpoise/cheats, named with the game's ID."));
			none.dimmed = true;
			rows.push_back(std::move(none));
		}
		return rows;
	}

	std::vector<Row> Shell::PorpoiseBoostRows()
	{
		ReadPorpoiseGame();
		std::vector<Row> rows;
		const auto has = [&](const std::string& key, const std::string& value) {
			const auto it = m_porpoise.own.find(key);
			return it != m_porpoise.own.end() && it->second == value;
		};
		if (m_porpoiseGame.ownDolphinFile)
		{
			Row own = GameRow(Row::Kind::Action, "ownfile", Tr("Your own settings file"), Tr("This game uses your own settings file, so the options below have no effect. Remove it to use them."));
			own.dimmed = true;
			rows.push_back(std::move(own));
		}
		if (m_porpoiseGame.hasPick)
		{
			const auto& values = m_porpoiseGame.pick.values;
			const bool all = std::all_of(values.begin(), values.end(), [&](const auto& v) { return has(v.first, v.second); });
			Row every = GameRow(Row::Kind::Toggle, "boostall", Tr("Auto Boost"), m_porpoiseGame.pick.note.empty() ? std::string(Tr("Settings that run this game best on PS5.")) : m_porpoiseGame.pick.note);
			every.on = all;
			every.value = all ? Tr("On") : Tr("Off");
			rows.push_back(std::move(every));
		}
		const bool nothingElse = rows.empty() && m_porpoiseGame.fixes.empty();
		{
			// Riptide Boost, right under Auto Boost: this game's, else every game's
			const auto own = m_porpoise.own.find("fast_float");
			const auto every = m_porpoise.side.find("fast_float");
			Row riptide = GameRow(Row::Kind::Toggle, "riptide", Tr("Riptide Boost"), Tr("Speeds up the emulated processor's 3D math. Turn it off if a game misbehaves."));
			riptide.on = own != m_porpoise.own.end() ? own->second == "1" : every != m_porpoise.side.end() && every->second == "1";
			riptide.value = riptide.on ? Tr("On") : Tr("Off");
			rows.push_back(std::move(riptide));
		}
		if (m_porpoiseGame.hasPick)
		{
			const auto& values = m_porpoiseGame.pick.values;
			for (size_t i = 0; i < values.size(); i++)
			{
				if (values[i].first == "fast_float")
					continue; // the row above
				std::string label, text;
				ps5porpoise::DescribeDolphin(values[i].first, values[i].second, label, text);
				Row one = GameRow(Row::Kind::Toggle, "pick:" + std::to_string(i), label + ": " + text, Tr("Recommended for this game."));
				one.on = has(values[i].first, values[i].second);
				one.value = one.on ? Tr("On") : Tr("Off");
				rows.push_back(std::move(one));
			}
		}
		for (size_t i = 0; i < m_porpoiseGame.fixes.size(); i++)
		{
			const auto& fix = m_porpoiseGame.fixes[i];
			const std::string why = fix.why.empty() ? std::string(Tr("A known fix for this game.")) : fix.why;
			if (fix.overrideKey.empty())
			{
				Row applied = GameRow(Row::Kind::Action, "fixed", fix.label + ": " + fix.value, why);
				applied.value = Tr("Applied");
				rows.push_back(std::move(applied));
				continue;
			}
			const auto it = m_porpoise.own.find(fix.overrideKey);
			Row row = GameRow(Row::Kind::Toggle, "fix:" + std::to_string(i), fix.label + ": " + fix.value, why);
			row.on = it == m_porpoise.own.end() || it->second == fix.raw;
			row.value = row.on ? Tr("On") : Tr("Off");
			rows.push_back(std::move(row));
		}
		if (nothingElse)
		{
			Row none = GameRow(Row::Kind::Action, "none", Tr("No changes needed"), Tr("This game runs well with the default settings."));
			none.dimmed = true;
			rows.push_back(std::move(none));
		}
		return rows;
	}

	void Shell::ChangePorpoiseGame(const Row& row, bool cross)
	{
		ReadPorpoiseGame();
		const std::string what = row.id.substr(std::strlen("porpoisegame:"));
		std::string error;
		const auto write = [&](const std::map<std::string, std::string>& values, const std::vector<std::string>& gone) {
			if ((!values.empty() && !ps5porpoise::Write(m_porpoise.gameFile, values, error)) || (!gone.empty() && !ps5porpoise::Remove(m_porpoise.gameFile, gone, error)))
				Toast(TrF("Porpoise's settings could not be saved: {0}", error));
			m_porpoise.read = false;
			m_feedback.Play(ui::Cue::Toggle);
		};
		const size_t colon = what.find(':');
		const size_t index = colon == std::string::npos ? 0 : (size_t)std::atoi(what.c_str() + colon + 1);
		const std::string verb = what.substr(0, colon);
		if (verb == "cheat" && index < m_porpoiseGame.cheats.size())
		{
			const auto& cheat = m_porpoiseGame.cheats[index];
			const std::string key = ps5porpoise::CheatKey(cheat, !cheat.defaultOn);
			if (row.on == cheat.defaultOn)
				write({{key, "1"}}, {});
			else
				write({}, {key});
		}
		else if (verb == "boostall" || (verb == "pick" && index < m_porpoiseGame.pick.values.size()))
		{
			std::map<std::string, std::string> values;
			std::vector<std::string> gone;
			for (size_t i = 0; i < m_porpoiseGame.pick.values.size(); i++)
				if (verb == "boostall" || i == index)
				{
					if (row.on)
						gone.push_back(m_porpoiseGame.pick.values[i].first);
					else
						values[m_porpoiseGame.pick.values[i].first] = m_porpoiseGame.pick.values[i].second;
				}
			// the choice is kept, so the next start doesn't make it again (ApplyAutoBoost)
			if (verb == "boostall")
				values[ps5porpoise::kAutoBoostKey] = row.on ? "0" : "1";
			write(values, gone);
		}
		else if (verb == "riptide")
			write({{"fast_float", row.on ? "0" : "1"}}, {});
		else if (verb == "fix" && index < m_porpoiseGame.fixes.size())
		{
			const auto& fix = m_porpoiseGame.fixes[index];
			if (row.on)
				write({{fix.overrideKey, fix.offValue}}, {});
			else
				write({}, {fix.overrideKey});
		}
		else if (verb == "addcode" && cross)
		{
			const std::string id = m_porpoiseGame.id;
			OpenTextKeyboard(Tr("A name for your code"), "", 40, false, [this, id](const std::string& name) {
				if (name.empty())
					return;
				OpenTextKeyboard(TrF("{0}: the code's digits", name), "", 960, false, [this, id, name](const std::string& digits) {
					if (digits.empty())
						return;
					std::string why;
					if (ps5porpoise::AddOwnCode(ps5porpoise::Data() + "/cheats", id, name, digits, why))
					{
						m_porpoiseGame.file.clear(); // read again, with it
						m_porpoise.read = false;
						Toast(Tr("Done."));
					}
					else
					{
						m_feedback.Play(ui::Cue::Denied);
						Toast(why == "write" ? Tr("The code could not be saved.") : Tr("A Gecko code is digits 0-9 and A-F, sixteen to a line."));
					}
				});
			});
		}
		else if (verb == "use" && cross)
		{
			const auto values = ps5porpoise::SetupValues(ps5porpoise::GetSetup(ps5porpoise::Data(), (int)index));
			write(values, {});
			Toast(Tr("Done."));
		}
		else if (verb == "save" && cross)
		{
			std::vector<std::string> names;
			for (int i = 0; i < ps5porpoise::kSetups; i++)
				names.push_back(TrF("Setup {0}", i + 1));
			OpenPicker(m_gameName, Tr("Save as a setup"), names, -1, [this](int choice) {
				// every game's values, with this game's own over them
				std::map<std::string, std::string> values = m_porpoise.side;
				for (const auto& [key, value] : m_porpoise.own)
					values[key] = value;
				Toast(ps5porpoise::SaveSetup(ps5porpoise::Data(), choice, values, m_gameName) ? Tr("Done.") : Tr("The setup could not be saved."));
			});
		}
	}

	std::vector<Row> Shell::PorpoiseSavesRows()
	{
		const std::string saves = ps5porpoise::Data() + "/saves";
		const bool wii = ps5retro::Is(SideIndex(), "wii");
		// each side's own: one screen's reading mustn't show on the other side's
		if (!m_porpoiseSavesRead || m_porpoiseSavesWii != wii)
		{
			m_porpoiseSavesWii = wii;
			for (const ui::TextureId texture : m_porpoiseSaveTextures)
				m_gfx.DestroyTexture(texture);
			m_porpoiseSaveTextures.clear();
			m_porpoiseSaves = wii ? std::vector<ps5titlesaves::Container>{ps5porpoise::ReadWiiSaves(saves)} : ps5porpoise::ReadCards(saves);
			m_porpoiseSavesRead = true;
		}
		std::vector<Row> rows;
		for (size_t c = 0; c < m_porpoiseSaves.size(); c++)
		{
			ps5titlesaves::Container& card = m_porpoiseSaves[c];
			const std::string group = wii ? "" : "porpoise-card-" + card.id + "-" + card.region;
			// tr: a GameCube memory card: {0} its slot, A or B, {1} its region, USA, EUR or JAP
			const std::string cardName = wii ? "" : TrF("Memory card {0} ({1})", card.id, card.region);
			if (!wii && card.saves.empty())
			{
				// an empty slot still shows: Dolphin makes the card when a game first saves to it
				Row empty;
				empty.kind = Row::Kind::Action;
				empty.dimmed = true;
				empty.id = "porpoisesaves-empty";
				empty.label = cardName;
				empty.value = Tr("Empty");
				empty.description = TrF("{0} of {1} blocks free", card.freeBlocks, card.totalBlocks);
				empty.help = empty.description;
				rows.push_back(std::move(empty));
				continue;
			}
			if (!wii)
			{
				Row link;
				link.kind = Row::Kind::Link;
				link.id = "sub";
				link.opens = group;
				link.label = cardName;
				link.value = TrP((int)card.saves.size(), "{0} save", "{0} saves");
				// tr: {0} and {1} are counts of a memory card's blocks
				link.description = TrF("{0} of {1} blocks free", card.freeBlocks, card.totalBlocks);
				link.help = link.description;
				rows.push_back(std::move(link));
			}
			for (size_t s = 0; s < card.saves.size(); s++)
			{
				ps5titlesaves::TitleSave& save = card.saves[s];
				Row row;
				row.kind = Row::Kind::Action;
				row.id = "porpoisesave:" + std::to_string(c) + ":" + std::to_string(s);
				row.group = group;
				row.label = save.title;
				// made once a reading, the frame they're first shown in: a row's only as long as its list
				const auto texture = [this](const ps5titlesaves::Picture& picture) -> ui::TextureId {
					if (picture.rgba.empty())
						return 0;
					const ui::TextureId id = m_gfx.CreateTexture(picture.width, picture.height, picture.rgba.data());
					m_porpoiseSaveTextures.push_back(id);
					return id;
				};
				if (!save.iconTexture && !save.icon.rgba.empty())
					save.iconTexture = texture(save.icon);
				if (!save.bannerTexture && !save.banner.rgba.empty())
					save.bannerTexture = texture(save.banner);
				row.icon = save.iconTexture;
				row.banner = save.bannerTexture;
				if (!save.banner.rgba.empty())
					row.bannerAspect = float(save.banner.width) / float(save.banner.height);
				row.value = save.blocks ? std::string(TrP(save.blocks, "{0} block", "{0} blocks")) : TrF("{0} KB", (save.bytes + 1023) / 1024);
				row.description = save.detail.empty() ? save.code : save.detail + " · " + save.code;
				if (!save.separate)
				{
					row.dimmed = true;
					row.description = Tr("This card is a single image file, so saves can't be copied one by one. Use Copy the whole card to USB instead.");
				}
				row.help = row.description;
				rows.push_back(std::move(row));
			}
			if (!wii)
			{
				Row whole;
				whole.kind = Row::Kind::Action;
				whole.id = "porpoisecard:" + std::to_string(c);
				whole.group = group;
				whole.label = Tr("Copy the whole card to USB");
				whole.description = Tr("Backs up every save on this card at once.");
				whole.help = whole.description;
				rows.push_back(std::move(whole));
				const auto action = [&](const char* id, const std::string& label, const std::string& description) {
					Row extra;
					extra.kind = Row::Kind::Action;
					extra.id = std::string(id) + std::to_string(c);
					extra.group = group;
					extra.label = label;
					extra.description = description;
					extra.help = description;
					rows.push_back(std::move(extra));
				};
				action("porpoisecardsave:", Tr("Copy a save from USB"), Tr("Copy one save from the drive to this card."));
				action("porpoisecardload:", Tr("Copy a card from USB"), Tr("Copy every save from a card on the drive to this card. Saves already here are kept."));
				action("porpoisecarddelete:", Tr("Delete every save on this card"), Tr("A copy is kept in saves/before."));
			}
		}
		if (wii && m_porpoiseSaves[0].saves.empty())
		{
			Row none;
			none.kind = Row::Kind::Action;
			none.dimmed = true;
			none.id = "porpoisesaves-none";
			none.label = Tr("No saves yet");
			rows.push_back(std::move(none));
		}
		if (wii)
		{
			Row one;
			one.kind = Row::Kind::Action;
			one.id = "porpoisecardsave:0";
			one.label = Tr("Copy a save from USB");
			one.description = Tr("Copy one save from the drive.");
			one.help = one.description;
			rows.push_back(std::move(one));
		}
		Row import;
		import.kind = Row::Kind::Action;
		import.id = "porpoisesaves-import";
		import.label = Tr("Copy every save from USB");
		import.description = Tr("From a USB drive's backups and Porpoise Saves folder. Saves already here are kept.");
		import.help = import.description;
		rows.push_back(std::move(import));
		return rows;
	}

	void Shell::ChoosePorpoiseSave(const Row& row)
	{
		size_t c = 0, s = 0;
		if (row.dimmed || std::sscanf(row.id.c_str(), "porpoisesave:%zu:%zu", &c, &s) != 2 || c >= m_porpoiseSaves.size() || s >= m_porpoiseSaves[c].saves.size())
		{
			m_feedback.Play(ui::Cue::Edge);
			return;
		}
		const ps5titlesaves::TitleSave save = m_porpoiseSaves[c].saves[s];
		OpenPicker(SystemName(m_side), save.title, {Tr("Copy to USB"), Tr("Delete")}, -1, [this, save](int choice) {
			const std::string saves = ps5porpoise::Data() + "/saves";
			std::string where, error;
			if (choice == 0)
			{
				if (ps5porpoise::ExportSave(saves, save, ps5porpoise::FirstUsbDrive(), BackupStamp(), where, error))
					Toast(TrF("Copied to {0}", where));
				else
					// tr: {0} is why, in the system's words
					Toast(TrF("The save could not be copied: {0}", error));
				return;
			}
			where = ps5porpoise::MoveSaveAside(saves, save, BackupStamp(), error);
			if (where.empty())
				Toast(TrF("The save could not be moved: {0}", error));
			else
				Toast(TrF("Moved to {0}", where));
			m_porpoiseSavesRead = false;
		});
	}

	void Shell::ImportPorpoiseSaves()
	{
		ps5porpoise::Imported imported;
		std::string error;
		// onto the first card that is a folder; the Wii's go to their own places
		std::string folder;
		const std::string saves = ps5porpoise::Data() + "/saves";
		for (const ps5titlesaves::Container& card : ps5porpoise::ReadCards(saves))
			if (folder.empty())
				folder = card.folder;
		if (!ps5porpoise::ImportSaves(saves, folder, ps5porpoise::FirstUsbDrive(), imported, error))
		{
			Toast(TrF("The saves could not be copied: {0}", error));
			return;
		}
		// tr: {0} is how many saves came in, {1} how many were already here
		Toast(TrF("{0} saves copied, {1} already here", imported.gameCube + imported.wii, imported.skipped));
		m_porpoiseSavesRead = false;
	}

	void Shell::ForgetPorpoiseGame()
	{
		// all of the game's own, as Porpoise's Reset to Defaults: its file, with whatever else Porpoise kept in it
		ReadPorpoise();
		std::error_code ec;
		if (!m_porpoise.gameFile.empty())
			std::filesystem::remove(m_porpoise.gameFile, ec);
		m_porpoise.read = false;
	}
}
