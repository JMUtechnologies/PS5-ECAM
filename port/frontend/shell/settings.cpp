Warning: truncated output (original token count: 42388)
Total output lines: 3328

// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Settings: what the side you are on can change, found at a
// glance. One short list of pages with icons down the left, the side's own and the few every side
// shares, and nothing of another side; the page's rows across the rest of the screen, each a control
// (a toggle, a slider, pips, segments, swatches), the focused one with a line saying what it does, its
// longer help behind Triangle. Game settings are the same pages, scoped to one game: only what a game
// can have its own of, each value its side's (Default) until changed, Square back to Default.

#include "shell_internal.h"
#include "../actions.h"
#include "../sound.h"
#include "../../app/achievements.h"
#include "../../app/boxart.h"
#include "../../app/mods.h"
#include "../../app/pack_updates.h"
#include "../../app/paths.h"
#include "../../app/updates.h"
#include "../../app/usb_devices.h"
#include "../../azahar/azahar.h"
#include "../../azahar/controls.h"
#include "../../azahar/library.h"
#include "../../eden/eden.h"
#include "../../eden/library.h"
#include "../../eden/rooms.h"
#include "../../eden/save_transfer.h"
#include "../../melonds/library.h"
#include "../../melonds/melonds.h"
#include "../../ps5/privilege.h"
#include "../../ps5/display.h"
#include "../../ps5/log.h"
#include "../../ps5/pad.h"
#include "../../stream/stream.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		// tr: the 3DS borders' names (artwork around the screens)
		constexpr const char* kBorderThemes[] = {TrMarkC("border", "None"), TrMarkC("border", "Midnight"), TrMarkC("border", "Waves"),
			TrMarkC("border", "Aurora"), TrMarkC("border", "Shell"), "PS5-ECAM"};
		constexpr const char* kRegions[] = {TrMark("Automatic"), TrMark("Japan"), TrMark("USA"), TrMark("Europe"), TrMark("Australia"),
			TrMark("China"), TrMark("Korea"), TrMark("Taiwan")};
		// tr: the languages the emulated 3DS can be set to
		constexpr const char* kLanguages[] = {TrMark("Automatic"), TrMark("Japanese"), TrMark("English"), TrMark("French"), TrMark("German"),
			TrMark("Italian"), TrMark("Spanish"), TrMark("Chinese (simplified)"), TrMark("Korean"), TrMark("Dutch"), TrMark("Portuguese"),
			TrMark("Russian"), TrMark("Chinese (traditional)")};
		constexpr const char* kResolutions[] = {"1x (400 × 240)", "2x (800 × 480)", "3x (1200 × 720)", "4x (1600 × 960)", "5x (2000 × 1200)",
			"6x (2400 × 1440)", "7x (2800 × 1680)", "8x (3200 × 1920)", "9x (3600 × 2160)", "10x (4000 × 2400)"};
		// tr: the 3DS's screen layouts
		constexpr const char* kLayouts[] = {TrMark("Top above bottom"), TrMark("Top screen only"), TrMark("Large top screen"), TrMark("Side by side")};
		// tr: texture filters: None, then the filters' own names
		constexpr const char* kTextureFilters[] = {TrMarkC("texture filter", "None"), "Anime4K", TrMark("Bicubic"), "ScaleForce", "xBRZ", "MMPX"};
		// the ones the list offers, by Azahar's number: Anime4K is its OpenGL renderer's only (the PS5 runs
		// its Vulkan one), so it is left out, and a setting that has it shows as None
		constexpr int kOfferedFilters[] = {0, 2, 3, 4, 5};

		int FilterIndex(int filter)
		{
			for (int i = 0; i < (int)std::size(kOfferedFilters); i++)
				if (kOfferedFilters[i] == filter)
					return i;
			return 0;
		}
		// tr: how a game's picture is scaled: the methods' names
		constexpr const char* kUpscaleFilters[] = {TrMark("Bilinear"), TrMark("Bicubic"), TrMark("Bicubic Hermite"), TrMark("Nearest neighbour"), TrMark("AMD FSR 1")};
		constexpr const char* kMusicNames[] = {TrMark("Setup theme"), TrMark("Off")};

		// The launcher music's choices, as ps5cemu.json names them: its own piece, each of the player's
		// files (ps5sound::MusicFolder), all of them shuffled when there are two or more, none. The folder
		// is read again at most every 3 s: the rows are built each frame Settings draws them.
		std::vector<std::string> MusicValues(std::vector<std::string>* names)
		{
			static std::vector<std::string> s_files;
			static auto s_listed = std::chrono::steady_clock::time_point{};
			const auto now = std::chrono::steady_clock::now();
			if (now - s_listed > std::chrono::seconds(3))
			{
				s_files = ps5sound::MusicFiles();
				s_listed = now;
			}
			std::vector<std::string> values{"setup"};
			if (names)
				names->assign({Tr(kMusicNames[0])});
			for (const std::string& file : s_files)
			{
				values.push_back("file:" + file);
				if (names)
					names->push_back(std::filesystem::path(file).stem().string());
			}
			if (s_files.size() > 1)
			{
				values.push_back("shuffle");
				if (names)
					// tr: Launcher music's choice that plays every file of the player's, in a random order
					names->push_back(Tr("Shuffle your music"));
			}
			values.push_back("off");
			if (names)
				names->push_back(Tr(kMusicNames[1]));
			return values;
		}
		constexpr int kHolds[] = {400, 600, 800, 1000, 1200, 1500};
		constexpr int kClocks[] = {25, 50, 75, 100, 125, 150, 200, 300, 400}; // the 3DS's CPU clock, as its in-game menu has them
		// tr: how the DS's screens are scaled to the TV: the filters' names
		constexpr const char* kDsFilters[] = {TrMark("Sharp"), TrMark("Smooth"), TrMark("Square pixels")};
		// tr: the DS's two screens turned together, a quarter turn at a time, for the games held on their side:
		// upright, a quarter turn clockwise, upside down, a quarter turn counterclockwise
		constexpr const char* kDsRotations[] = {TrMark("Upright"), TrMark("90° right"), TrMark("Upside down"), TrMark("90° left")};
		// the Switch's internal resolutions (Eden's), times the game's own 720p handheld or 1080p docked
		constexpr const char* kNxResolutions[] = {"0.5x", "0.75x", "1x", "1.5x", "2x", "3x", "4x"};
		// Eden's upscaling filters, in its order (bilinear, AMD FSR, bicubic, nearest), by kUpscaleFilters' names
		constexpr int kNxFilters[] = {0, 4, 1, 3};
		// AMD FSR 1's sharpening on the Switch side, Eden's slider: each step halves it, shown as the share
		// left of the sharpest
		constexpr int kNxSharpening[] = {0, 25, 50, 100, 200};
		// tr: the Switch's controller types, as a game sees the DualSense (Automatic: what the game takes)
		constexpr const char* kNxControllers[] = {TrMark("Automatic"), TrMark("Pro Controller"), TrMark("Handheld"), TrMark("Joy-Con pair"),
			TrMark("Left Joy-Con"), TrMark("Right Joy-Con")};

		int NxSharpeningIndex(int value)
		{
			int best = 0;
			for (int i = 1; i < (int)std::size(kNxSharpening); i++)
				if (std::abs(kNxSharpening[i] - value) < std::abs(kNxSharpening[best] - value))
					best = i;
			return best;
		}

		// whether a room's address is one Eden takes: a name or an IPv4 address
		bool RoomHost(const std::string& host)
		{
			return !host.empty() && host.size() <= 253 && std::all_of(host.begin(), host.end(), [](char c) {
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
			});
		}

		// the DualSense buttons a turbo can be on, by the names the settings keep them by (ps5pad::ButtonNamed)
		constexpr const char* kTurboInputs[] = {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Create"};

		std::vector<std::string> TurboOptions(const std::string& turbo, int& index)
		{
			// tr: the turbo button's first choice: no button is one
			std::vector<std::string> options{TrC("turbo", "None")};
			index = 0;
			for (const char* input : kTurboInputs)
			{
				if (turbo == input)
					index = (int)options.size();
				options.push_back(ButtonName(input));
			}
			return options;
		}

		// the built-in themes' names and lines (ui/theme.cpp), for the translations
		[[maybe_unused]] constexpr const char* kThemeWords[] = {TrMark("Light"), TrMark("Link Jack"), TrMark("Classic (3.0)"), TrMark("Midnight"),
			TrMark("Eden"), TrMark("Frosted glass over each side's colours."),
			TrMark("3.0.0's launcher: deep blue panels, flatter, a little squarer."),
			TrMark("True black behind everything and nothing moving: for OLED screens."),
			TrMark("Dark frosted glass and rounder corners, the side's light drifting and motes of light rising."),
			TrMark("A light background with dark text.")};

		int ClockIndex(int clock)
		{
			for (int i = 0; i < (int)std::size(kClocks); i++)
				if (kClocks[i] >= clock)
					return i;
			return (int)std::size(kClocks) - 1;
		}

		// a table of names, in the language
		template<size_t N>
		std::vector<std::string> Options(const char* const (&list)[N], const char* context = nullptr)
		{
			std::vector<std::string> options;
			for (const char* name : list)
				options.push_back(context ? TrC(context, name) : Tr(name));
			return options;
		}

		std::vector<std::string> TextureFilterNames()
		{
			std::vector<std::string> names;
			for (int filter : kOfferedFilters)
				names.push_back(filter == 0 ? TrC("texture filter", kTextureFilters[filter]) : Tr(kTextureFilters[filter]));
			return names;
		}

		using ps5lang::Percent;
	}

	std::vector<SettingsPage> Shell::SettingsPages() const
	{
		// the side's pages; the shared ones on every side
		std::vector<SettingsPage> pages;
		// the menus' language first, with a globe, which reads in any language
		pages.push_back({"ui-language", Tr("Language"), Tr("The language of the menus: the PS5's own, unless you choose another."), Icon::Globe});
		const char* every = IsWiiU() ? Tr("For every Wii U game.") :
			Is3ds()					 ? Tr("For every 3DS game.") :
			IsSwitch()				 ? Tr("For every Switch game.") :
									   Tr("For every DS game.");
		pages.push_back({"graphics", Tr("Graphics"), every, Icon::Monitor});
		if (Is3ds() || IsDs())
			pages.push_back({"screens", Tr("Screens"), every, Icon::Screens});
		// the remote touch screen is on the 3DS's and the DS's Controls, behind a row (on the Wii U's System; a
		// Switch game runs in Eden's program, which has none)
		pages.push_back({"controls", Tr("Controls"), IsWiiU() ? Tr("What each player's DualSense is to the Wii U.") :
				Is3ds()										  ? Tr("The DualSense as the 3DS, and the remote touch screen.") :
				IsSwitch()									  ? Tr("The DualSense as the Switch.") :
																Tr("The DualSense as the DS, and the remote touch screen."),
			Icon::Pad});
		// the emulated console and the sound, one page (the owner's, 2026-10-27: fewer pages down the left); the
		// Wii U's has its USB portals and the remote touch screen behind rows
		pages.push_back({"system", Tr("System"), IsWiiU() ? Tr("The sound, Cemu's USB portals and the remote touch screen.") :
													Is3ds() ? Tr("The emulated 3DS's region, language and speed, and the sound.") :
													IsSwitch() ? Tr("The emulated Switch, docked or handheld, its speed and language, and the sound.") :
																 Tr("The emulated DS's language, BIOS, microphone and Slot-2, and the sound."),
			Icon::Chip});
		// ProsperoEden's Settings > Performance, as its launcher has it
		if (IsSwitch())
			pages.push_back({"performance", Tr("Performance"), Tr("Speed against accuracy, and the emulated Switch's memory."), Icon::Pulse});
		pages.push_back({"files", Tr("Game files"), IsWiiU() ? Tr("Where your Wii U games are, and installs into the Wii U's storage.") :
										Is3ds()		 ? Tr("Where your 3DS games are, and installs into the 3DS's storage.") :
										IsSwitch()	 ? Tr("Where your Switch games are, with its keys and firmware.") :
													   Tr("Where your DS games are, and where their saves go."),
			Icon::Folder});
		// the Wii U's accounts and every side's saves (6.13); a handheld has one player, so its page is saves alone.
		// A Switch game's saves are Eden's, in its own folder, each of Eden's players with their own
		pages.push_back({"saves",
			IsWiiU() ? Tr("Accounts and saves") :
				// tr: Settings' page of the Switch side's players (Eden's user profiles, the people) and every side's saves
				IsSwitch() ? Tr("Players and saves") :
							 Tr("Saves"),
			IsWiiU()	 ? Tr("Each account has its own saves for every game. Backups hold every side's saves.") :
				IsSwitch() ? std::string(Tr("Who plays, each with saves of their own.")) + " " + Tr("Backups hold every side's saves.") :
							 Tr("Backups hold every side's saves."),
			Icon::Save});
		// with the Switch's rooms (port/eden/rooms.h) and the 3DS's Artic Base, what goes over the network
		pages.push_back({"online", Tr("Online"), IsWiiU() ? Tr("Box art, graphic packs and PS5-ECAM's updates.") :
										   IsDs()	 ? Tr("Box art, RetroAchievements and PS5-ECAM's updates.") :
										   Is3ds()	 ? Tr("Box art, Artic Base and PS5-ECAM's updates.") :
													   Tr("Box art, multiplayer rooms and PS5-ECAM's updates."),
			Icon::Globe});
		pages.push_back({"launcher", Tr("Launcher"), Tr("Theme, sides, language and text."), Icon::Sparkle});
		pages.push_back({"playtime", Tr("Play time"), Tr("How long you have played, each side and the most played games."), Icon::Play});
		pages.push_back({"diagnostics", Tr("Diagnostics"), Tr("The Setup check, logs, caches and this session's details."), Icon::Pulse});
		// the version always in sight, whatever the credits' length in a language
		pages.push_back({"about", TrC("app", "About"), std::string(Tr("Who made what.")) + " · " + Tr("Version") + " " + ps5update::Readable(PS5CEMU_VERSION),
			Icon::Info});
		if (IsRetro())
		{
			std::vector<SettingsPage> selected;
			if (InGameSettings()) return {{"runtime", Tr("Emulator settings"), Tr("Controls, settings and saves are managed by the bundled emulator."), Icon::Info}};
			selected.push_back({"runtime", Tr("Emulator settings"), Tr("Controls, settings and saves are managed by the bundled emulator."), Icon::Info});
			for (const auto& page : pages) if (page.id == "files" || page.id == "launcher" || page.id == "playtime" || page.id == "diagnostics" || page.id == "about") selected.push_back(page);
			return selected;
		}
		if (!InGameSettings())
			return pages;
		// a game's settings: only the pages with something a game can have its own of (SideRows' rows
		// with a setting: Graphics, Screens, System and the Switch's Performance; the Switch's Picture effects,
		// Eden's own, are behind a row on its Graphics)
		std::vector<SettingsPage> game;
		for (const SettingsPage& page : pages)
			if (page.id == "graphics" || page.id == "screens" || page.id == "system" || page.id == "performance")
				game.push_back({page.id, page.title, TrF("For {0} only. Its other settings follow its side's.", m_gameName), page.icon});
		// its saves alone (6.15), whatever settings it has of its own (a Switch game's are Eden's)
		game.push_back({"gamesaves", Tr("Saves"), TrF("{0}'s saves only. Settings has every side's.", m_gameName), Icon::Save});
		if (IsSwitch())
		{
			game.push_back({"nxmods", Tr("Mods"), Tr("Its mods, each on or off."), Icon::Folder});
			game.push_back({"nxcheats", Tr("Cheats"), Tr("Its cheats, each on or off."), Icon::Folder});
		}
		return game;
	}

	void Shell::SettingsOpen(const std::string& page, bool onRows, int row)
	{
		m_switchExtrasKey.clear();
		if (InGameSettings())
			GameSettingsClose();
		m_page = page;
		m_subPage.clear();
		m_onRows = onRows;
		m_settingRow = row;
		m_onBar = false;
		m_tab = ScreenId::Settings;
		m_holdProgress = 0;
		m_message.clear();
		Show(ScreenId::Settings);
	}

	std::string Shell::PacksStatus() const
	{
		using State = ps5packs::Status::State;
		const auto status = ps5packs::GetStatus();
		const std::string installed = ps5packs::InstalledVersion();
		switch (status.state)
		{
		case State::Checking: return Tr("Checking GitHub…");
		case State::Downloading:
			return status.total ? TrF("Downloading: {0}", Percent((int)(status.received * 100 / status.total))) :
								  TrF("Downloading: {0} MB", status.received >> 20);
		case State::Installing: return Tr("Installing…");
		case State::UpToDate: return TrF("Up to date: {0}", installed);
		case State::Done: return TrF("Installed: {0}", status.version);
		case State::Failed: return status.message;
		case State::Idle: break;
		}
		return installed.empty() ? std::string(Tr("Cross to download")) : installed;
	}

	std::string Shell::AppUpdateStatus()
	{
		using State = ps5update::Status::State;
		const auto status = ps5update::GetStatus();
		switch (status.state)
		{
		case State::Checking: return Tr("Checking GitHub…");
		case State::UpToDate: return TrF("Up to date: {0}", ps5update::Readable(PS5CEMU_VERSION));
		// tr: {0} is a version, as in 3.5.0
		case State::Available: return TrF("{0}: Cross to install", ps5update::Readable(status.latest));
		case State::Downloading:
			return status.total ? TrF("Downloading: {0}", Percent((int)(status.received * 100 / status.total))) : std::string(Tr("Downloading…"));
		case State::Verifying: return Tr("Checking the download…");
		case State::Installing: return Tr("Installing…");
		case State::Ready: return Tr("Installed: Cross to restart");
		case State::Failed: return status.message;
		case State::Idle: break;
		}
		return Tr("Cross to check");
	}

	void Shell::PollPacks()
	{
		if (ps5packs::GetStatus().state == ps5packs::Status::State::Done && !m_packsReloaded && m_status.coreReady && IsWiiU())
		{
			m_packsReloaded = true;
			ps5emu::ReloadGraphicPacks();
		}
	}

	std::string Shell::ModsStatus() const
	{
		using State = ps5mods::Status::State;
		const auto status = ps5mods::GetStatus();
		switch (status.state)
		{
		case State::Copying:
			// tr: {0} is a mod's name, {1} how much of all the mods is copied, as in 40%
			return status.total ? TrF("Copying {0}: {1}", status.current, Percent((int)(status.copied * 100 / status.total))) :
								  std::string(Tr("Looking for mods…"));
		case State::Done: return status.mods ? TrP(status.mods, "{0} mod added", "{0} mods added") : std::string(Tr("No mods found"));
		case State::Failed: return status.message;
		case State::Idle: break;
		}
		return Tr("Cross to look on your drives");
	}

	void Shell::AddMods(uint64_t titleId, const std::string& name)
	{
		const std::vector<std::string> drives = ConnectedDrives();
		if (ps5mods::Folders(drives).empty())
		{
			m_feedback.Play(ui::Cue::Denied);
			OpenHelp(Tr("No mods on your drives"),
				Tr("Copy each mod's folder into a ps5cemu/mods folder on a USB drive and plug it in. A mod is a Cemu graphic pack (a "
				   "folder with a rules.txt), or a folder with content and aoc folders in it for the files it replaces. The guide on "
				   "GitHub, docs/GUIDE.md, has more."));
			return;
		}
		ps5mods::Start(drives, titleId, name);
		m_modsToastAt = m_now;
		m_feedback.Play(ui::Cue::Select);
		Toast(Tr("Copying mods from your drives…"));
	}

	void Shell::PollMods()
	{
		const ps5mods::Status status = ps5mods::GetStatus();
		if (status.state == ps5mods::Status::State::Copying)
		{
			// a large mod takes minutes: how far it is, now and then
			if (m_now - m_modsToastAt > 8)
			{
				m_modsToastAt = m_now;
				Toast(ModsStatus());
			}
			return;
		}
		if (status.finished == m_modsFinished)
			return;
		m_modsFinished = status.finished;
		// the mods copied, failure or not, are Cemu's packs from now
		if (status.mods > 0 && m_status.coreReady)
			ps5emu::ReloadGraphicPacks();
		const std::string skipped = status.skipped ?
			"\n\n" + TrP(status.skipped, "{0} folder had no rules.txt and no game: put a mod like that in a folder named by its game's title ID, "
										 "or add it from its game's menu.",
				"{0} folders had no rules.txt and no game: put a mod like that in a folder named by its game's title ID, or add it from "
				"its game's menu.") :
			std::string();
		if (status.state == ps5mods::Status::State::Failed)
		{
			m_feedback.Play(ui::Cue::Denied);
			OpenHelp(Tr("The mods could not all be copied"),
				status.message + (status.mods ? "\n\n" + TrP(status.mods, "{0} mod was added.", "{0} mods were added.") : std::string()) + skipped);
			return;
		}
		if (status.mods == 0)
		{
			m_feedback.Play(ui::Cue::Denied);
			OpenHelp(Tr("No mods found"), Tr("The ps5cemu/mods folders on your drives hold no folder with a rules.txt, nor with content, aoc "
											 "or code folders.") + skipped);
			return;
		}
		if (!skipped.empty())
			OpenHelp(TrP(status.mods, "{0} mod added", "{0} mods added"), Tr("Turn them on in their games' Graphic packs.") + skipped);
		else
			Toast(TrP(status.mods, "{0} mod added: turn it on in its game's Graphic packs", "{0} mods added: turn them on in their games' Graphic packs"));
	}

	// -- the rows -------------------------------------------------------------------------------------

	std::vector<Row> Shell::SideRows(const std::string& page, const ps5settings::Launcher& settings)
	{
		using Kind = Row::Kind;
		Row existingFiles; existingFiles.id = "existing-emulator-files"; existingFiles.kind = Kind::Action;
		existingFiles.label = Tr("Existing emulator files");
		existingFiles.description = Tr("Find updates, DLC, firmware, BIOS and cheats in matching standalone folders. Existing files stay in place.");
		if (IsRetro() && (page == "files" || page == "runtime" || page == "graphics" || page == "system" || page == "screens" || page == "gamesaves"))
		{
			std::vector<Row> rows;
			const int backend = settings.retro[SideIndex() - 4].retroBackend;
			if ((SideIndex() == 6 || SideIndex() == 7) && page == "runtime" && !InGameSettings())
			{
				Row runtime; runtime.id = "retro-backend"; runtime.kind = Kind::Choice; runtime.label = Tr("Emulator runtime");
				runtime.options = SideIndex() == 6 ? std::vector<std::string>{"N64XY", "Mupen64Plus (experimental)"} : std::vector<std::string>{"NESYX", "FCEUmm PS5 (experimental)"};
				runtime.index = backend; runtime.value = runtime.options[backend];
				runtime.description = Tr("Changes the next launch. Each emulator keeps separate settings and saves. Build and package the selected runtime first."); rows.push_back(runtime);
			}
			if (page == "files")
			{
				rows.push_back(existingFiles);
				Row automatic; automatic.id = "auto-game-folders"; automatic.kind = Kind::Toggle; automatic.label = Tr("Automatically find games");
				automatic.on = settings.retro[SideIndex() - 4].autoGameFolders; automatic.value = automatic.on ? Tr("On") : Tr("Off");
				automatic.description = Tr("Search existing libraries on internal storage and connected USB drives, SSDs and HDDs.");
				automatic.help = Tr("Includes standalone emulator game folders and standard ROM folders. Games stay where they are. Turn this off to use only the selected games folder. Connected drives are checked while the library is open; Look for games now also rescans changed files."); rows.push_back(automatic);
				Row folder; folder.id = "folder"; folder.kind = Kind::Link; folder.label = Tr("Games folder"); folder.value = GamesFolder(m_side); folder.description = Tr("Select this system's games folder. Existing games and saves stay in place."); rows.push_back(folder);
				Row scan; scan.id = "rescan"; scan.kind = Kind::Action; scan.label = Tr("Look for games now"); rows.push_back(scan);
			}
			else
			{
				Row info; info.id = "retro-library"; info.kind = Kind::Action; info.dimmed = !ps5retro::Available(SideIndex(), backend); info.label = Tr("Open emulator settings and saves");
				info.description = Tr("The bundled emulator keeps its own settings, controls and save formats. Open its native frontend to change settings. Close that frontend to return to ECAM. Existing standalone data is retained."); rows.push_back(info);
			}
			if (page == "gamesaves") { Row backup; backup.id = "backup"; backup.kind = Kind::Action; backup.label = Tr("Back up to USB"); backup.description = Tr("Backs up all systems, including both N64 and NES backends. Save states remain specific to their emulator."); rows.push_back(backup); }
			Row diagnostic; diagnostic.id = "retro-diagnostics"; diagnostic.kind = Kind::Action; diagnostic.label = Tr("Emulator diagnostics"); rows.push_back(diagnostic);
			return rows;
		}
		const ps5settings::N3ds& hand =…30388 tokens truncated…ve (6.12)
		else if (id == "installcia")
		{
			m_filesSide = System::N3ds;
			m_pageFrom = ScreenId::Settings;
			FilesOpen(2);
		}
		else if (id == "buttons")
		{
			m_pageFrom = ScreenId::Settings;
			MappingOpen();
		}
		else if (id == "artic")
		{
			m_pageFrom = ScreenId::Settings;
			ArticOpen();
		}
		else if (id == "nx-room")
		{
			// offline again
			if (hand.nxRoomHost.empty())
				return;
			hand.nxRoomHost.clear();
			hand.nxRoomName.clear();
			hand.nxRoomPassword.clear();
			SaveSettings();
			Toast(Tr("Switch games play offline"));
		}
		else if (id == "nx-nickname")
		{
			OpenTextKeyboard(Tr("Your nickname in rooms"), hand.nxNickname, 20, false, [this](const std::string& name) {
				if (name.empty())
					return;
				if (!ps5rooms::ValidNickname(name))
				{
					Toast(Tr("A nickname is 4 to 20 letters, digits, spaces, dots, dashes or underscores."));
					return;
				}
				Handheld(System::Switch).nxNickname = name;
				SaveSettings();
				// tr: the nickname shown in Switch rooms ({0})
				Toast(TrF("In rooms you are {0}", name));
			});
		}
		else if (id == "nx-refresh")
			ps5rooms::Refresh();
		else if (id == "nx-public")
		{
			const ps5rooms::Status rooms = ps5rooms::Get();
			if (row.index < 0 || row.index >= (int)rooms.rooms.size())
				return;
			const ps5rooms::Room room = rooms.rooms[row.index];
			if (!room.compatible)
			{
				m_feedback.Play(ui::Cue::Denied);
				Toast(Tr("This room is for another version of Eden."));
				return;
			}
			auto choose = [this, room](const std::string& password) {
				ps5settings::N3ds& nx = Handheld(System::Switch);
				nx.nxRoomHost = room.address;
				nx.nxRoomPort = room.port;
				nx.nxRoomName = room.name;
				nx.nxRoomPassword = password;
				SaveSettings();
				// tr: the room ({0}) Switch games join from now on
				Toast(TrF("Switch games join {0} as they start", room.name));
			};
			if (room.password)
				// tr: the password of a room ({0}: its name)
				OpenTextKeyboard(TrF("{0}'s password", room.name), "", 128, true, [choose](const std::string& password) {
					if (!password.empty())
						choose(password);
				});
			else
				choose("");
		}
		else if (id == "nx-private")
		{
			// its address, its port, then its password (none: empty)
			const std::string host = hand.nxRoomName == hand.nxRoomHost ? hand.nxRoomHost : std::string();
			OpenTextKeyboard(Tr("The room's address"), host, 253, false, [this](const std::string& host) {
				if (host.empty())
					return;
				if (!RoomHost(host))
				{
					Toast(Tr("An address is a name or an IPv4 address: letters, digits, dots and dashes."));
					return;
				}
				OpenTextKeyboard(Tr("Its UDP port"), std::to_string(ps5rooms::kDefaultPort), 5, false, [this, host](const std::string& port) {
					const int number = std::atoi(port.c_str());
					if (number <= 0 || number > 65535 || port.find_first_not_of("0123456789") != std::string::npos)
					{
						Toast(Tr("A port is a number from 1 to 65535."));
						return;
					}
					OpenTextKeyboard(Tr("Its password (empty: none)"), "", 128, true, [this, host, number](const std::string& password) {
						ps5settings::N3ds& nx = Handheld(System::Switch);
						nx.nxRoomHost = host;
						nx.nxRoomPort = number;
						nx.nxRoomName = host;
						nx.nxRoomPassword = password;
						SaveSettings();
						Toast(TrF("Switch games join {0} as they start", host));
					});
				});
			});
		}
		else if (id == "homemenu")
		{
			// the 3DS Home Menu, from the console's files Artic Setup copied
			ps5emu::Game home;
			if (ps5azahar::HomeMenu(hand.region, home))
				LaunchGame(home, Tr("Starting the 3DS"), {760, 240, 400, 400});
			else
				m_feedback.Play(ui::Cue::Denied);
		}
		else if (id == "cheevos")
		{
			// RetroAchievements (6.16): the user name, then the password; only the answer's token is kept
			if (ps5cheevos::Get().busy)
				return;
			OpenTextKeyboard(Tr("Your RetroAchievements user name"), "", 32, false, [this](const std::string& user) {
				if (user.empty())
					return;
				OpenTextKeyboard(TrF("{0}'s RetroAchievements password", user), "", 64, true, [this, user](const std::string& password) {
					if (!password.empty() && ps5cheevos::BeginSignIn(user, password))
						Toast(Tr("Signing in to RetroAchievements…"));
				});
			});
		}
		else if (id == "makeityours")
			WelcomeOpen(true);
		else if (id == "copylogs")
			m_diagnosticsDone[0] = CopyLogsToUsb();
		else if (id == "setupcheck")
			SetupOpen(false);
	}

	void Shell::HoldDone(const std::string& id)
	{
		if (id == "nxextra:removeprofile" && IsSwitch())
		{
			const auto context = SwitchExtrasContext();
			const auto view = ps5eden::extras::Read(context);
			std::string error;
			if (!view.error.empty()) Toast(view.error);
			else if (!view.profiles.empty())
			{
				if (ps5eden::extras::RemoveProfile(context, view.profiles[view.profile].id, error)) Toast(Tr("Player removed: their saves and settings are kept"));
				else Toast(error);
			}
			m_switchExtrasKey.clear();
			return;
		}
		m_feedback.Play(ui::Cue::Hold);
		if (id == "clearcaches")
			m_diagnosticsDone[Is3ds() ? 2 : 1] = ClearShaderCaches(Is3ds());
		else if (id == "clearunpacked")
		{
			const uint64_t bytes = ps5archive::UnpackedBytes();
			const int copies = ps5archive::ClearUnpacked();
			// tr: what clearing the unpacked games did; {1} is the room it made, as in 2.4 GB
			m_unpackedDone = copies ? TrP(copies, "{0} game deleted ({1})", "{0} games deleted ({1})", SizeWords(bytes)) : std::string(Tr("None to delete"));
			m_unpackedAt = -100;
		}
		else if (id == "boxartall")
		{
			ChangeSetting(Row{Row::Kind::Action, "boxartall"}, 0, true);
			// tr: a held row's result: the covers are on their way
			m_message = Tr("Downloading in the background");
		}
		else if (id == "separate" || id == "putback")
		{
			// a folder for each system, or back as before: written down, and made as the app starts again
			if (!m_layout.blocked.empty())
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			const auto request = id == "separate" ? ps5reorganize::Request::Separate : ps5reorganize::Request::PutBack;
			if (!ps5reorganize::Ask(ps5paths::Root(), request))
			{
				m_message = TrF("PS5-ECAM cannot write to {0}. Is the HEN loaded?", ps5paths::Root());
				return;
			}
			ps5log::Line("[reorganize] {} asked for in Settings: as the app starts again", id == "separate" ? "a folder for each system" : "putting back");
			ps5update::Restart();
			m_message = ps5update::GetStatus().message; // back only when the PS5 did not start it again
			if (m_message.empty())
				m_message = Tr("Close PS5-ECAM and start it again from the home screen: the folders move as it starts.");
		}
		else if (id == "goback")
		{
			// the release the last update replaced back in place, and not offered again at start; not while
			// the systems are in folders of their own, which a version from before would not find
			if (!m_layout.separated.empty())
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			std::string error;
			if (ps5update::GoBack(error))
			{
				m_settings.ui.skipVersion = ps5update::Readable(PS5CEMU_VERSION);
				SaveSettings();
				ps5update::Restart();
				error = ps5update::GetStatus().message; // back only when the PS5 did not start it again
			}
			m_message = error;
		}
		else if (id == "resetcontrols")
		{
			ps5azahar::ResetControls(Handheld(m_side));
			if (m_side == System::Switch)
				Handheld(m_side).nxDeadzone = ps5settings::N3ds{}.nxDeadzone;
			SaveSettings();
			// tr: what a held row says once it has done its work
			m_message = TrC("finished", "Done");
		}
		else if (id == "deleteaccount")
		{
			const uint32_t current = ps5emu::CurrentAccount();
			std::vector<ps5emu::WiiuAccount> others;
			for (const auto& account : ps5emu::ListAccounts())
				if (account.id != current)
					others.push_back(account);
			const int which = m_holdChoice["deleteaccount"];
			if (which < (int)others.size())
			{
				std::string error;
				if (ps5emu::DeleteAccount(others[which].id, error))
					Toast(TrF("{0} is deleted; its saves stay", others[which].name));
				else
					Toast(error);
				m_holdChoice["deleteaccount"] = 0;
			}
		}
		else if (id == "restore")
		{
			const int which = m_holdChoice["restore"];
			std::string error;
			if (which < (int)m_backups.size() && ps5saves::StartRestore(m_backups[which], error))
			{
				m_savesMessage.clear();
				Toast(TrF("Restoring the saves of {0}…", m_backups[which].when));
			}
			else if (!error.empty())
				Toast(error);
		}
		else if (id == "cheevos")
		{
			ps5cheevos::SignOut();
			Toast(Tr("Signed out of RetroAchievements"));
		}
		else if (id == "nximport" && IsSwitch())
		{
			const auto view = ps5eden::extras::Read(SwitchExtrasContext());
			const int selected = m_holdChoice["nximport"];
			std::string error;
			if (!view.error.empty()) error = view.error;
			else if (view.profiles.empty()) error = Tr("Choose a player first, in Settings > Players and saves.");
			else if (selected >= 0 && selected < (int)m_switchImports.size() &&
				ps5saves::StartSwitchImport(m_gameRef, view.profiles[view.profile].id, m_switchImports[selected].folder, error, m_switchImports[selected].accountSaveId)) m_savesMessage = Tr("Taking the save…");
			if (!error.empty()) Toast(error);
		}
		else if (id == "gamerestore")
		{
			const int which = m_holdChoice["gamerestore"];
			std::string error;
			if (which < (int)m_gameBackups.size() && ps5saves::StartGameRestore(m_gameBackups[which], m_gameRef, error))
			{
				m_savesMessage.clear();
				Toast(TrF("Restoring {0}'s saves of {1}…", m_gameName, m_gameBackups[which].when));
			}
			else if (!error.empty())
				Toast(error);
		}
		else if (id.starts_with("gamesave:"))
		{
			const size_t which = (size_t)std::strtoul(id.c_str() + 9, nullptr, 10);
			if (which < m_gameSaves.size())
			{
				std::string error;
				const std::string label = SaveLabel(m_gameSaves[which]);
				if (!ps5saves::MoveAside(m_gameSaves[which], error).empty())
					// tr: one of a game's saves moved out of the way ({0}: which, as its row names it)
					Toast(TrF("{0}: moved to saves-before", label));
				else
					Toast(error);
				m_gameSavesAt = -100;
			}
		}
		else if (id == "resetgame")
		{
			m_settings.games.erase(ps5settings::GameKey(ps5launcher::SideName(m_side), m_gameTitle));
			SaveSettings();
			m_gameView = ps5settings::ForGame(m_settings, ps5launcher::SideName(m_side), m_gameTitle);
			m_message = Tr("Done: back to its side's settings");
		}
	}

	void Shell::SettingsUpdate(const ui::Press& press, const ui::Actions& actions)
	{
		const auto pages = SettingsPages();
		if (pages.empty())
		{
			if (InGameSettings() && press.button == Button::Circle)
				GameSettingsClose();
			return;
		}
		int page = 0;
		for (int i = 0; i < (int)pages.size(); i++)
			if (pages[i].id == m_page)
				page = i;
		m_page = pages[page].id;
		// a › page is open only while its page's rows have the focus, and while it has rows
		if (!m_onRows)
			m_subPage.clear();
		auto rows = SettingRows(m_page);
		if (!m_subPage.empty() && rows.empty())
		{
			m_subPage.clear();
			m_settingRow = m_subFrom;
			rows = SettingRows(m_page);
		}
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		// what is held to confirm
		if (m_onRows && !rows.empty() && rows[m_settingRow].kind == Row::Kind::Hold)
		{
			const float seconds = m_settings.ui.holdMs / 1000.0f;
			m_holdProgress = actions.Held(Button::Cross) ? actions.HeldFor(Button::Cross) / seconds : 0.0f;
			if (m_holdProgress >= 1 && !m_holdFired)
			{
				m_holdFired = true;
				HoldDone(rows[m_settingRow].id);
			}
			if (!actions.Held(Button::Cross))
				m_holdFired = false;
		}
		else
			m_holdProgress = 0;
		const Button b = press.button;
		if (b == Button::Count)
			return;
		if (!m_onRows)
		{
			switch (b)
			{
			case Button::Up:
				if (page == 0)
					m_feedback.Play(ui::Cue::Edge, press.repeat); // a dialog: the bar is under its dim
				else
				{
					m_page = pages[page - 1].id;
					m_feedback.Play(ui::Cue::Focus);
				}
				break;
			case Button::Down:
				if (page + 1 >= (int)pages.size())
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
				{
					m_page = pages[page + 1].id;
					m_feedback.Play(ui::Cue::Focus);
				}
				break;
			case Button::L2:
			case Button::R2:
				m_page = b == Button::L2 ? pages.front().id : pages.back().id;
				m_feedback.Play(ui::Cue::Focus);
				break;
			case Button::Right:
			case Button::Cross:
				if (!rows.empty())
				{
					m_onRows = true;
					m_settingRow = 0;
					m_message.clear();
					m_feedback.Play(ui::Cue::Select);
				}
				else
					m_feedback.Play(ui::Cue::Edge);
				break;
			case Button::Circle:
				m_feedback.Play(ui::Cue::Back);
				if (InGameSettings())
					GameSettingsClose();
				else
				{
					ShowTab(ScreenId::Library);
					m_barFocus = kSides;
				}
				break;
			default: break;
			}
			return;
		}
		if (rows.empty())
		{
			m_onRows = false;
			return;
		}
		const Row& row = rows[m_settingRow];
		switch (b)
		{
		case Button::Circle:
			// a › page: back to its row; else back to the pages
			if (!m_subPage.empty())
				CloseSubPage();
			else
			{
				m_onRows = false;
				m_feedback.Play(ui::Cue::Back);
			}
			break;
		case Button::Up:
		case Button::Down:
		{
			const int next = m_settingRow + (b == Button::Down ? 1 : -1);
			if (next < 0 || next >= (int)rows.size())
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_settingRow = next;
				m_message.clear();
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Triangle: OpenHelp(row.label, row.help); break;
		case Button::Square:
			if (InGameSettings())
				ResetGameSetting(row);
			break;
		case Button::Left:
			if (row.kind == Row::Kind::Link || row.kind == Row::Kind::Action || row.kind == Row::Kind::Hold)
			{
				if (!m_subPage.empty())
					CloseSubPage();
				else
				{
					m_onRows = false;
					m_feedback.Play(ui::Cue::Back);
				}
			}
			else
				ChangeSetting(row, -1, false);
			break;
		case Button::Right:
		case Button::Cross:
			// a › row opens its page, Right as Cross
			if (!row.opens.empty())
				OpenSubPage(row);
			else if (b == Button::Right)
				ChangeSetting(row, 1, false);
			else
				ChangeSetting(row, 0, true);
			break;
		case Button::L2:
		case Button::R2:
			m_settingRow = b == Button::L2 ? 0 : (int)rows.size() - 1;
			m_feedback.Play(ui::Cue::Focus);
			break;
		default: break;
		}
	}

	// The remote touch screen's card (6.5): the QR code on white, the address in large type, the state in
	// a chip, one line of what to know
	void Shell::DrawRemoteScreen(Canvas& canvas, const Box& card)
	{
		const ps5stream::State state = ps5stream::Current();
		canvas.Rect(card, kRadiusCard, Surface());
		canvas.Ring(card, kRadiusCard, 1.5f, kGlassEdge);
		const float pad = 36, qr = 300;
		float tx = card.x + pad;
		if (!state.address.empty())
		{
			auto code = m_qrCodes.find(state.address);
			if (code == m_qrCodes.end())
				code = m_qrCodes.emplace(state.address, ui::QrCode(state.address)).first;
			ui::DrawQr(canvas, code->second, {card.x + pad, card.y + (card.h - qr) * 0.5f, qr, qr}, 18);
			tx += qr + 44;
		}
		const float tw = card.Right() - pad - tx;
		const bool working = state.kind == ps5stream::State::Kind::Waiting || state.kind == ps5stream::State::Kind::Showing;
		const ui::TextBlock overline = m_fonts.Layout(Style(kOverlineStyle), working ? Tr("Scan it, or open") : Tr("Remote touch screen"), tw, 1);
		const ui::TextBlock address = m_fonts.Layout(Style({46, ui::Weight::SemiBold, 1.1f}), state.address, tw, 1);
		const std::string what = ps5stream::Describe(state.kind);
		const ui::TextStyle chipStyle = Style({22, ui::Weight::Medium, 1.0f});
		const float chipW = m_fonts.Width(chipStyle, what) + 60, chipH = 44;
		const ui::TextBlock note = m_fonts.Layout(Style({21, ui::Weight::Regular, 1.5f}),
			working ? Tr("Same network as the PS5. Nothing to install: the browser is enough. Sound stays on the PS5. Any browser on the network can "
					  "see it and touch.") :
					  Tr("Turn it off and on again once that is fixed."),
			std::min(tw, 660.0f), 3);
		const float height = overline.height + 14 + (address.lines.empty() ? 0 : address.height + 22) + chipH + 22 + note.height;
		float y = card.y + (card.h - height) * 0.5f;
		canvas.Text(overline, tx, y, Tertiary());
		y += overline.height + 14;
		if (!state.address.empty())
		{
			canvas.Text(address, tx, y, kText);
			y += address.height + 22;
		}
		const uint32_t dot = state.kind == ps5stream::State::Kind::Showing ? kGood : working ? kWarn : kBad;
		canvas.Rect({tx, y, chipW, chipH}, chipH * 0.5f, Wash(0.08f));
		canvas.Rect({tx + 18, y + chipH * 0.5f - 11, 22, 22}, 11, ui::SetAlpha(dot, 0.2f));
		canvas.Rect({tx + 23, y + chipH * 0.5f - 6, 12, 12}, 6, dot);
		const ui::TextBlock chip = m_fonts.Layout(chipStyle, what, chipW, 1);
		canvas.Text(chip, tx + 48, y + (chipH - chip.height) * 0.5f, kText);
		y += chipH + 22;
		canvas.Text(note, tx, y, Secondary());
	}

	// About's card (6.5): the developer's PayPal.me as a QR code on white, the address under it in words,
	// one line saying what it is; never shown anywhere else
	void Shell::DrawSupport(Canvas& canvas, const Box& card)
	{
		static constexpr const char* kSupport = "https://www.paypal.com/paypalme/empthe";
		canvas.Rect(card, kRadiusCard, Surface());
		canvas.Ring(card, kRadiusCard, 1.5f, kGlassEdge);
		const float pad = 32, qr = std::min(card.w - 2 * pad, 260.0f);
		float y = card.y + pad;
		canvas.Text(Style(kOverlineStyle), card.x + pad, y, Tr("Support PS5-ECAM"), Accent(), card.w - 2 * pad, 1);
		y += 40;
		auto code = m_qrCodes.find(kSupport);
		if (code == m_qrCodes.end())
			code = m_qrCodes.emplace(kSupport, ui::QrCode(kSupport)).first;
		ui::DrawQr(canvas, code->second, {card.CentreX() - qr * 0.5f, y, qr, qr}, 16);
		y += qr + 20;
		const ui::TextBlock address = canvas.Text(Style({26, ui::Weight::SemiBold, 1.1f}), card.x + pad, y, "paypal.me/empthe", kText, card.w - 2 * pad, 1,
			ui::Align::Centre);
		y += address.height + 14;
		canvas.Text(Style({20, ui::Weight::Regular, 1.45f}), card.x + pad, y,
			Tr("PS5-ECAM is free. If it brings your games back, you can support its work here."), Secondary(), card.w - 2 * pad, 4,
			ui::Align::Centre);
	}

	void Shell::SettingsDraw(Canvas& canvas)
	{
		// a dialog over the screen it was opened from, on every side and in every theme (6.5): that screen as
		// it was, dimmed, taking no input and asking for no focus
		m_behind = true;
		if (InGameSettings() && m_gameFrom == ScreenId::Hub)
			HubDraw(canvas);
		else
			LibraryDraw(canvas);
		m_behind = false;
		const float appear = Enter(0);
		Scrim(canvas, appear);
		const auto pages = SettingsPages();
		if (pages.empty())
			return;
		int page = 0;
		for (int i = 0; i < (int)pages.size(); i++)
			if (pages[i].id == m_page)
				page = i;
		m_page = pages[page].id;

		// the dialog: rising a little as it appears
		const Box dialog = SettingsDialog().Offset(0, (1 - appear) * 24);
		canvas.PushAlpha(appear);
		canvas.Shadow(dialog, kRadiusSheet, 40, 120, 0xb3000000);
		canvas.Rect(dialog, kRadiusSheet, m_settings.ui.highContrast ? kInk2 : ui::SetAlpha(ui::Mix(kInk1, kInk2, 0.5f), 0.97f));
		canvas.Ring(dialog, kRadiusSheet, 1.5f, kGlassEdge);
		const float navW = 380;
		canvas.PushClip(dialog, kRadiusSheet);
		canvas.Rect({dialog.x, dialog.y, navW, dialog.h}, 0, Wash(0.03f));
		canvas.Rect({dialog.x + navW, dialog.y, 1.5f, dialog.h}, 0, kLine);
		canvas.PopClip();

		// the pages down its left: one short list, each with its icon; above them whose settings they are
		const float navX = dialog.x + 20, navTop = dialog.y + 30;
		// tr: above Settings' pages: whose settings they are ({0}: Wii U, Nintendo 3DS or Nintendo DS)
		const std::string head = InGameSettings() ? m_gameName : TrF("{0} settings", SystemName(m_side));
		canvas.Text(Style(kOverlineStyle), navX + 22, navTop, head, Tertiary(), navW - 64, 1);
		const float railTop = navTop + 40, railBottom = dialog.Bottom() - 20, step = 56;
		const float listHeight = pages.size() * step;
		m_railScroll.target = std::clamp(page * step - (railBottom - railTop) * 0.5f, 0.0f, std::max(0.0f, listHeight - (railBottom - railTop)));
		m_railScroll.omega = kScrollOmega;
		m_railScroll.Update(m_dt);
		canvas.PushClip({dialog.x, railTop - 6, navW, railBottom - railTop + 6});
		for (int i = 0; i < (int)pages.size(); i++)
		{
			const float ry = railTop + i * step - m_railScroll.value;
			const bool selected = i == page;
			const Box item{navX, ry, navW - 40, step - 4};
			if (selected)
			{
				canvas.Rect(item, 14, m_onRows ? Wash(0.06f) : Surface2());
				if (!m_onRows)
					Focus(item, 14);
			}
			canvas.Draw(pages[i].icon, {item.x + 22, item.CentreY() - 14, 28, 28}, selected ? Accent() : Secondary());
			// the pages' names all one size (#76): a long one takes two lines rather than a smaller size
			ui::TextStyle style = Style({24, selected ? ui::Weight::SemiBold : ui::Weight::Medium, 1.0f});
			style.minScale = 1;
			const ui::TextBlock text = m_fonts.Layout(style, pages[i].title, item.w - 86, 2);
			canvas.Text(text, item.x + 68, item.CentreY() - text.height * 0.5f, selected ? kText : Secondary());
		}
		canvas.PopClip();

		// the page: its title, what it is for, its rows filling the rest of the dialog; a › page's, its
		// row's label and line, the page it is on still marked in the rail
		const SettingsPage& current = pages[page];
		const bool sub = !m_subPage.empty();
		const float px = dialog.x + navW + 48, pw = dialog.Right() - 48 - px;
		float ty = dialog.y + 36;
		canvas.Text(Style(kTitleStyle), px, ty, sub ? m_subTitle : current.title, kText, pw, 1);
		canvas.Text(Style({22, ui::Weight::Regular, 1.3f}), px, ty + 62, sub ? current.title + "  ›  " + m_subLine : current.subtitle, Secondary(), pw, 1);
		auto rows = SettingRows(m_page);
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		const float ry = ty + 112;
		const float rowH = 96;
		// rows past the dialog's height scroll with the focus
		const float available = dialog.Bottom() - 28 - ry;
		const float scroll = std::max(0.0f, (m_settingRow + 1) * (rowH + 12) - available);
		canvas.PushClip({px - 20, ry - 12, pw + 40, available + 24});
		const float labelScale = LabelScale(rows, pw); // every row's label the same size (#76)
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const Box box{px, ry + i * (rowH + 12) - scroll, pw, rowH};
			const bool focused = m_onRows && i == m_settingRow;
			DrawRow(canvas, rows[i], box, focused, labelScale);
			if (focused && rows[i].kind == Row::Kind::Hold)
				HoldRing(canvas, box, m_holdProgress);
		}
		// the remote touch screen, while it is on: the code to scan, the address, what is happening (its › page's,
		// on Controls or the Wii U's System)
		const float under = ry + rows.size() * (rowH + 12) - scroll;
		const std::string& shown = sub ? m_subPage : m_page;
		if (shown == "remote" && m_settings.remoteScreen && !InGameSettings())
			DrawRemoteScreen(canvas, {px, under + 18, pw, 372});
		else
		{
			// what the page (or the › page: the Wii U's USB devices, the remote touch screen) says under its rows,
			// in as many lines as the dialog has room for; About's beside its support card
			const std::string panel = PanelText(shown);
			float textW = pw;
			if (m_page == "about")
			{
				const float cardW = 380;
				DrawSupport(canvas, {px + pw - cardW, under, cardW, std::min(560.0f, dialog.Bottom() - 28 - under)});
				textW = pw - cardW - 40;
			}
			if (!panel.empty())
			{
				const ui::TextStyle style = Style({m_page == "about" ? 20.0f : 22.0f, ui::Weight::Regular, 1.45f});
				const float top = under + (rows.empty() ? 0 : 8);
				const int lines = std::max(1, (int)((dialog.Bottom() - 28 - top) / (style.size * style.lineHeight)));
				canvas.Text(style, px, top, panel, Secondary(), textW, lines);
			}
		}
		canvas.PopClip();
		canvas.PopAlpha();
	}
}
