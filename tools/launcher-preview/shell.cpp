// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the new launcher on a PC (tools/preview-shell.sh), to see its screens without a console.
//
// The launcher's own code (port/frontend/shell, the UI kit in port/ui, the catalogue, the settings,
// GameTDB's facts and the compatibility list; port/azahar's library and controls) runs as it does on
// the console, and the kit draws with the PC's Vulkan (Mesa's lavapipe will do) into an image read
// back after each frame, which the script's shots save. In place of Cemu and the console it has
// samples (console.cpp): Wii U games, graphic packs, controllers, and the 3DS games the script
// makes (tools/launcher-preview/make-3ds-samples.py).
//
//   shell-preview OUTPUT_FOLDER SCRIPT GAMES_FOLDER 3DS_GAMES_FOLDER
//
// The script has one command a line ('#' starts a comment):
//   press BUTTON...  each button down for 3 frames, then up for 3, one after another
//                    (touchpad+options: the two together)
//   hold BUTTON N    down for N frames, then up for 3
//   wait N           N frames
//   shot NAME        the frame on screen as OUTPUT_FOLDER/NAME.png
//   record NAME      from here, every other frame (30 a second) as OUTPUT_FOLDER/NAME/00000.png on,
//                    for a video of the launcher (ffmpeg -framerate 30 -i NAME/%05d.png makes one)
//   stop             no more recording
// Buttons: up down left right cross circle square triangle l1 r1 l2 r2 l3 r3 options create
// touchpad, and the sticks: ls-up ls-down ls-left ls-right rs-up rs-down rs-left rs-right.
//
// The launcher starts as it would
// after earlier sessions, on the Wii U side's Library; with PREVIEW_FIRST set, as on a first start (the
// Setup check, then the first side with games); PREVIEW_REMOTE turns the remote touch screen on; PREVIEW_UPDATE has
// a newer release found; PREVIEW_NO_DATA, /data out of reach; PREVIEW_LAUNCH_ERROR, the last game not
// started. PREVIEW_LANGUAGE names the menus' language ("de"; "qps", tools/lang.py pseudo's, for text
// that does not fit), PREVIEW_SYSTEM_LANGUAGE the PS5's (its number: 4 is German, 0 Japanese).
// PREVIEW_WHATSNEW starts as after an update (Make it yours, with what the version brings); PREVIEW_THEME
// names a theme (ui/theme.h's id: link-jack, midnight...); PREVIEW_SIDES the sides shown (1 Wii U, 2 3DS, 4 DS);
// PREVIEW_FILTERS turns the Library's filters' row on; PREVIEW_LARGER_TEXT, PREVIEW_REDUCE_MOTION and
// PREVIEW_HIGH_CONTRAST turn on Settings > Launcher's accessibility rows.

#include "console.h"

#include "app/compatibility.h"
#include "app/gameinfo.h"
#include "app/lang.h"
#include "app/paths.h"
#include "app/updates.h"
#include "app/esde_get.h"
#include "azahar/library.h"
#include "melonds/library.h"
#include "eden/library.h"
#include "retro/library.h"
#include "frontend/shell.h"
#include "libretro/game.h"
#include "retro/library.h"
#include "stream/stream.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name);

// in place of host_ps5.cpp's: the PC's Vulkan loader, an image read back into the shots' frame, the
// repository's font, and the preview's clock, which the script moves on 1/60 s a frame
ps5shell::Host ps5shell::DefaultHost()
{
	Host host;
	host.getInstanceProcAddr = vkGetInstanceProcAddr;
	host.target = [] {
		ui::Target target;
		target.width = preview::kWidth;
		target.height = preview::kHeight;
		target.readback = [](const uint8_t* bgra, uint32_t width, uint32_t height, size_t pitch) {
			const uint32_t w = std::min<uint32_t>(width, preview::kWidth), h = std::min<uint32_t>(height, preview::kHeight);
			for (uint32_t y = 0; y < h; y++)
				std::memcpy(&preview::frame[(size_t)y * preview::kWidth * 4], bgra + y * pitch, (size_t)w * 4);
		};
		return target;
	};
	host.assets = "port/ui"; // its fonts/lexend.sdf, as the app's assets/ui has it
	host.fontFolders = {preview::output + "/fonts"}; // preview-shell.sh's stand-ins for the console's fonts
	host.clock = [] { return preview::timeUs / 1e6; };
	host.afterFrame = [] {
		preview::timeUs += 16667;
		preview::AdvanceScript();
	};
	return host;
}

int main(int argc, char* argv[])
{
	if (argc != 6)
	{
		std::fprintf(stderr, "shell-preview OUTPUT_FOLDER SCRIPT GAMES_FOLDER 3DS_GAMES_FOLDER DS_GAMES_FOLDER\n");
		return 2;
	}
	preview::output = argv[1];
	// the app's folder: preview-shell.sh's, with the assets the launcher reads pictures from (the sides'
	// tiles, Eden's launch picture)
	ps5paths::detail::AppDirSlot() = preview::output + "/app";
	ps5gameinfo::SetFolder("port/app/gametdb"); // the repository's: the preview runs from its root
	// how games run: the feed preview-shell.sh made from the list and a few made-up reports (no fetch)
	ps5compat::SetPaths(preview::output + "/compatibility.json", "");
	if (std::getenv("PREVIEW_UPDATE"))
		ps5update::Start(); // as the app's check finding a newer release
	preview::LoadScript(argv[2]);

	ps5settings::Launcher settings;
	settings.gamesFolder = argv[3];
	settings.n3ds.gamesFolder = argv[4];
	settings.nds.gamesFolder = argv[5];
	// the other sides' games in the games folder's folder of their key (gba/: preview-shell.sh puts a few there)
	for (size_t i = 0; i < ps5retro::kSystems.size(); i++)
		settings.retro[i].gamesFolder = std::string(argv[3]) + "/" + std::string(ps5retro::kSystems[i].key);
	if (!std::getenv("PREVIEW_FIRST"))
	{
		// earlier sessions: the side last used, the games last played, the counts their scans saved
		settings.ui.setupDone = true;
		settings.ui.reportsAsked = std::getenv("PREVIEW_REPORTS_PROMPT") == nullptr;
		// the version run last: this one, unless the preview is of what an update brings
		settings.ui.lastVersion = std::getenv("PREVIEW_WHATSNEW") ? "3.5.0" : PS5CEMU_VERSION;
		settings.ui.lastSide = "wiiu";
		settings.ui.layoutChosen = true;
		if (const char* layout = std::getenv("PREVIEW_LAYOUT"))
			settings.ui.layout = std::atoi(layout);
		settings.lastGame = 0x00050000101C9400;
		settings.recent = {0x00050000101C9400, 0x0005000010143500, 0x000500001010EC00, 0x0005000010101D00};
		settings.n3ds.lastGame = 0x0004000000053F00;
		settings.n3ds.recent = {0x0004000000053F00, 0x0004000000030600, 0x000400000017C100};
		settings.gameCount = 11;
		settings.n3ds.gameCount = 6;
		settings.nds.gameCount = 4;
		settings.nx.lastGame = 0x0100000000010000;
		settings.nx.recent = {0x0100000000010000, 0x0100152000022000};
		settings.nx.gameCount = 5;
	}
	if (std::getenv("PREVIEW_BOXART3D"))
		settings.ui.boxArt3d = true;
	if (std::getenv("PREVIEW_REMOTE"))
		settings.remoteScreen = true;
	if (std::getenv("PREVIEW_FILTERS"))
		settings.ui.libraryFilters = true;
	if (std::getenv("PREVIEW_NO_BADGES"))
		settings.ui.compatBadges = false; // Settings > Launcher > How games run, off
	if (const char* theme = std::getenv("PREVIEW_THEME"))
		settings.ui.theme = theme;
	if (std::getenv("PREVIEW_LARGER_TEXT"))
		settings.ui.largerText = true;
	if (std::getenv("PREVIEW_REDUCE_MOTION"))
		settings.ui.reduceMotion = true;
	if (std::getenv("PREVIEW_HIGH_CONTRAST"))
		settings.ui.highContrast = true;
	// the side it opens on: wiiu, 3ds, ds or switch
	if (const char* side = std::getenv("PREVIEW_SIDE"))
		settings.ui.lastSide = side;
	if (const char* sides = std::getenv("PREVIEW_SIDES"))
		settings.ui.sides = std::clamp(std::atoi(sides), 1, (1 << ps5launcher::kSideCount) - 1);
	ps5stream::Configure(settings.remoteScreen); // as main does
	// the menus' language: PREVIEW_LANGUAGE's code (qps: tools/lang.py pseudo's), else the PS5's, which
	// PREVIEW_SYSTEM_LANGUAGE gives as the console's number (console.cpp)
	if (const char* language = std::getenv("PREVIEW_LANGUAGE"))
		settings.ui.language = language;
	ps5lang::SetFolder("port/lang");
	ps5lang::Load(settings.ui.language);
	ps5launcher::Status status;
	status.diagnostics = {"Playtendoh preview: Cemu at 32e6628, Azahar at 4aef900", "Jailbroken by the HEN: /data reachable, executable memory",
		"Boot log: /data/ps5cemu/logs/boot.log", "Cemu's log: " + ps5paths::CemuRoot() + "/log.txt"};
	if (std::getenv("PREVIEW_NO_DATA"))
		status.notice = status.notice3ds = status.noticeDs = status.noticeSwitch =
			"Playtendoh cannot reach /data. Load a HEN with PPSA99360 in its app jailbreak list, or elfldr, then restart Playtendoh.";
	else if (std::getenv("PREVIEW_LAUNCH_ERROR"))
		status.launchError = ps5lang::Tr("This is not a Wii U game Playtendoh can start.") + std::string(" ") +
			ps5lang::TrF("Its disc key is missing from {0}/keys.txt.", ps5paths::CemuRoot());

	// as main_ps5.cpp's: a side opens when the launcher opens it (the preview's Cemu starts at once)
	std::optional<ps5launcher::Choice> choice;
	const ps5shell::Outcome outcome = ps5shell::Run(settings, status, [&](ps5launcher::System system) {
		if (system == ps5launcher::System::N3ds)
			ps5azahar::StartScan(settings.n3ds.gamesFolder);
		else if (system == ps5launcher::System::Nds)
			ps5melonds::StartScan(settings.nds.gamesFolder);
		else if (system == ps5launcher::System::Switch)
			ps5eden::StartScan(settings.nx.gamesFolder);
		else if (ps5retro::IsRetro((int)system)) // as main_ps5.cpp's: the side's folder looked through
			ps5retro::StartScan((int)system, settings.retro[ps5retro::Slot((int)system)].gamesFolder);
		else
			status.coreReady = true;
	}, choice);
	std::fprintf(stderr, "[preview] the launcher ended: %s\n", choice ? choice->game.name.c_str() : "no game");
	return outcome == ps5shell::Outcome::Chosen ? 0 : 1;
}

// What the launcher asks of the programs the preview doesn't link: an app with everything set up.
namespace ps5eglantine
{
	bool KeysFound() { return true; }
	bool FirmwareFound() { return true; }
	std::string DataFolder() { return "/data/eglantineps5"; }
}

namespace ps5libretro
{
	bool Plays(int) { return true; }
	const char* CoreTitle(int) { return "mGBA"; }
	// mGBA's options as it declares them (a few), to lay out Settings > Emulator
	const std::vector<CoreOption>& CoreOptions(int)
	{
		static const std::vector<CoreOption> options{
			{"mgba_solar_sensor_level", "Solar Sensor Level", "Sets ambient sunlight intensity. Can be used by games that included a solar sensor in their cartridges, e.g: the Boktai series.", "",
				{{"0", "0"}, {"5", "5"}, {"10", "10"}}, "0"},
			{"mgba_allow_opposing_directions", "Allow Opposing Directional Input", "Enabling this will allow pressing / quickly alternating / holding both left and right (or up and down) directions at the same time.", "Input and Auxiliary",
				{{"no", "Disabled"}, {"yes", "Enabled"}}, "no"},
			{"mgba_gb_model", "Game Boy Model", "Runs loaded content with a specific Game Boy model. 'Autodetect' will select the most appropriate model for the current game.", "System",
				{{"Autodetect", "Autodetect"}, {"Game Boy", "Game Boy"}, {"Super Game Boy", "Super Game Boy"}, {"Game Boy Color", "Game Boy Color"}, {"Game Boy Advance", "Game Boy Advance"}}, "Autodetect"},
			{"mgba_use_bios", "Use BIOS File if Found", "Use official BIOS/bootloader for emulated hardware, if present in RetroArch's system directory.", "System",
				{{"ON", "Enabled"}, {"OFF", "Disabled"}}, "ON"},
			{"mgba_color_correction", "Color Correction", "Adjusts output colors to match the display of real GBA/GBC hardware.", "Video",
				{{"OFF", "Disabled"}, {"GBA", "Game Boy Advance"}, {"GBC", "Game Boy Color"}, {"Auto", "Auto"}}, "OFF"},
			{"mgba_frameskip", "Frameskip", "Skip frames to improve performance at the expense of visual smoothness.", "Video",
				{{"disabled", "Disabled"}, {"auto", "Auto"}, {"auto_threshold", "Threshold"}, {"fixed_interval", "Fixed Interval"}}, "disabled"},
		};
		return options;
	}
}

namespace ps5shots
{
	// PREVIEW_SHOT: a picture to try Orbit's background with
	std::string Snap(int, const ps5emu::Game&)
	{
		const char* shot = std::getenv("PREVIEW_SHOT");
		return shot ? shot : "";
	}
}

// ES-DE's download (app/esde_get.cpp): never under way in the preview
namespace ps5esdeget
{
	void Start() {}
	Status GetStatus() { return {}; }
	bool Busy() { return false; }
	bool Cancel() { return false; }
	bool Remove(std::string&) { return true; }
}

namespace ps5esde
{
	bool Available() { return std::getenv("PREVIEW_ESDE") != nullptr; } // PREVIEW_ESDE=1: an app with ES-DE
	void BackToFrontend(const ps5settings::Launcher&, const std::string&) {}
}
