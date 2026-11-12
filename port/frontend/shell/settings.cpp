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
#include "../../app/reports.h"
#include "../../app/achievements.h"
#include "../../app/boxart.h"
#include "../../app/ingame3ds.h"
#include "../../app/mods.h"
#include "../../app/pack_updates.h"
#include "../../app/paths.h"
#include "../../app/updates.h"
#include "../../app/esde_get.h"
#include "../../app/usb_devices.h"
#include "../../azahar/azahar.h"
#include "../../azahar/controls.h"
#include "../../azahar/library.h"
#include "../../eden/eden.h"
#include "../../eden/library.h"
#include "../../eden/rooms.h"
#include "../../eglantine/launch.h"
#include "../../eden/save_transfer.h"
#include "../../esde/esde.h"
#include "../../libretro/game.h"
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
			TrMarkC("border", "Aurora"), TrMarkC("border", "Shell"), "Playtendoh"};
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
		[[maybe_unused]] constexpr const char* kThemeWords[] = {TrMark("Light"), TrMark("Link Jack"), TrMark("Deep blue"), TrMark("Midnight"),
			TrMark("Eden"), TrMark("Frosted glass over each system's colours."),
			TrMark("Deep blue panels, flatter and a little squarer."),
			TrMark("True black and no motion, for OLED screens."),
			TrMark("Dark frosted glass, rounder corners and drifting light."),
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
		// a game's settings: only the pages with something a game can have its own of
		if (InGameSettings())
			return GamePages();
		// the side's own pages first, its emulator's and its files; then, under a line, Playtendoh's, the same on
		// every side (the owner's, 2026-11-11: "a separate cut-off Playtendoh settings that stays consistent")
		std::vector<SettingsPage> pages = SidePages();
		// the menus' language first, with a globe, which reads in any language
		pages.push_back({"ui-language", Tr("Language"), Tr("The menu language. Uses the PS5's unless you choose another."), Icon::Globe, true});
		pages.push_back({"launcher", Tr("Launcher"), Tr("Theme, layout, systems, music and text."), Icon::Sparkle, true});
		// RetroAchievements, reports and the app's updates: one account and one app for every side (and the
		// cheats' downloads, when the app has them)
		pages.push_back({"accounts", Tr("Accounts and updates"), Tr("RetroAchievements, reports and Playtendoh's updates."), Icon::Download, true});
		pages.push_back({"backups", Tr("Backups"), Tr("Every system's saves, to a USB drive and back."), Icon::Save, true});
		pages.push_back({"playtime", Tr("Play time"), Tr("Time played per system, and your most played games."), Icon::Play, true});
		pages.push_back({"diagnostics", Tr("Diagnostics"), Tr("Setup check, logs, caches and session details."), Icon::Pulse, true});
		// the version always in sight, whatever the credits' length in a language
		pages.push_back({"about", TrC("app", "About"), std::string(Tr("Who made what.")) + " · " + Tr("Version") + " " + ps5update::Readable(PS5CEMU_VERSION),
			Icon::Info, true});
		return pages;
	}

	std::vector<SettingsPage> Shell::SidePages() const
	{
		std::vector<SettingsPage> pages;
		const std::string every = IsWiiU() ? Tr("For every Wii U game.") :
			Is3ds()						   ? Tr("For every 3DS game.") :
			IsSwitch()					   ? Tr("For every Switch game.") :
			IsDs()						   ? Tr("For every DS game.") :
											 // tr: a libretro system's Settings page's line: {0} is the system (NES, Game Boy...)
											 TrF("For every {0} game.", SystemName(m_side));
		if (IsPorpoise())
		{
			// the GameCube and Wii sides: Porpoise's settings, on the pages the other sides have (porpoise_rows.cpp)
			const bool wiiSide = ps5retro::Is(SideIndex(), "wii");
			pages.push_back({"graphics", Tr("Graphics"), Tr("For every GameCube and Wii game."), Icon::Monitor});
			pages.push_back({"controls", Tr("Controls"),
				wiiSide ? Tr("The DualSense as the Wii Remote, or as the GameCube controller.") : Tr("The DualSense as the GameCube controller."), Icon::Pad});
			pages.push_back({"sound", Tr("Sound"), Tr("Game sound, and how it handles slowdowns."), Icon::Chip});
			if (!wiiSide)
				pages.push_back({"gba", Tr("GBA"), Tr("A Game Boy Advance on a GameCube port: set one in Controls, under Ports."), Icon::Pad});
			pages.push_back({"system", Tr("System"), Tr("The emulated console, and how it runs."), Icon::Chip});
			pages.push_back({"files", Tr("Game files"), Tr("Where this system's games are."), Icon::Folder});
			pages.push_back({"porpoisesaves", wiiSide ? Tr("Saves") : Tr("Memory cards and saves"),
				wiiSide ? Tr("Copy each game's save to a USB drive, or move it aside.") : Tr("Copy saves on your cards to a USB drive, or move them aside."), Icon::Save});
			pages.push_back({"online", Tr("Online"), Tr("Box art for the library."), Icon::Globe});
			return pages;
		}
		if (IsRetro())
		{
			// a side on a core in the app: every option its core has, first, as the other sides' Graphics are
			if (ps5libretro::Plays(SideIndex()) || ps5retro::AlternativeOf(SideIndex()))
				pages.push_back({"core", Tr("Emulator"),
					ps5libretro::Plays(SideIndex()) ? TrF("All of {0}'s options, for every game on this system.", ps5libretro::CoreTitle(SideIndex())) :
													  std::string(Tr("Which emulator plays this system's games.")),
					Icon::Chip});
			// its picture and sound, as its in-game menu has them, which a game can have its own of
			if (OnCore())
				pages.push_back({"picture", Tr("Picture and sound"), every, Icon::Monitor});
			pages.push_back({"files", Tr("Game files"), Tr("Where this system's games are."), Icon::Folder});
			pages.push_back({"online", Tr("Online"), Tr("Box art for the library."), Icon::Globe});
			return pages;
		}
		pages.push_back({"graphics", Tr("Graphics"), every, Icon::Monitor});
		if (Is3ds() || IsDs())
			pages.push_back({"screens", Tr("Screens"), every, Icon::Screens});
		// the remote touch screen is on the 3DS's and the DS's Controls, behind a row (on the Wii U's System; a
		// Switch game runs in Eden's program, which has none)
		pages.push_back({"controls", Tr("Controls"), IsWiiU() ? Tr("Each player's controller for the Wii U.") :
				Is3ds()										  ? Tr("The DualSense as the 3DS, and the remote touch screen.") :
				IsSwitch()									  ? Tr("The DualSense as the Switch.") :
																Tr("The DualSense as the DS, and the remote touch screen."),
			Icon::Pad});
		// the emulated console and the sound, one page (the owner's, 2026-10-27: fewer pages down the left); the
		// Wii U's has its USB portals and the remote touch screen behind rows
		pages.push_back({"system", Tr("System"), IsWiiU() ? Tr("Sound, USB portals and the remote touch screen.") :
													Is3ds() ? Tr("Region, language, speed and sound.") :
													IsSwitch() ? Tr("Docked or handheld, speed, language and sound.") :
																 Tr("Language, BIOS, microphone, Slot-2 and sound."),
			Icon::Chip});
		// ProsperoEden's Settings > Performance, as its launcher has it
		if (IsSwitch())
			pages.push_back({"performance", Tr("Performance"), Tr("Speed, accuracy and memory."), Icon::Pulse});
		pages.push_back({"files", Tr("Game files"), IsWiiU() ? Tr("Where your Wii U games are, and installs into the Wii U's storage.") :
										Is3ds()		 ? Tr("Where your 3DS games are, and installs into the 3DS's storage.") :
										IsSwitch2()	 ? Tr("Where your Switch 2 games are, with their keys and firmware.") :
										IsSwitch()	 ? Tr("Where your Switch games are, with its keys and firmware.") :
										// tr: {0} is a system (Game Boy Advance)
										IsRetro()	 ? TrF("Where your {0} games are.", SystemName(m_side)) :
													   Tr("Where your DS games are, and where their saves go."),
			Icon::Folder});
		// the Wii U's accounts and the Switch's players (Eden's), each with their own saves; every side's backups
		// are Playtendoh's, below
		if (IsWiiU())
			pages.push_back({"saves", Tr("Wii U accounts"), Tr("Each account has its own saves."), Icon::Save});
		else if (IsSwitch())
			// tr: Settings' page of the Switch side's players (Eden's user profiles, the people)
			pages.push_back({"saves", TrC("switch", "Players"), Tr("Players, each with their own saves."), Icon::Save});
		// with the Switch's rooms (port/eden/rooms.h) and the 3DS's Artic Base, what this side has over the network
		pages.push_back({"online", Tr("Online"), IsWiiU() ? Tr("Box art and community graphic packs.") :
										   Is3ds()	 ? Tr("Box art and Artic Base.") :
										   IsSwitch() ? Tr("Box art and multiplayer rooms.") :
														Tr("Box art for the library."),
			Icon::Globe});
		return pages;
	}

	std::vector<SettingsPage> Shell::GamePages() const
	{
		const std::string game = TrF("For {0} only. Other settings follow the system's.", m_gameName);
		std::vector<SettingsPage> pages;
		if (IsPorpoise())
		{
			// a GameCube or Wii game's: those it can have its own of, in its file of Porpoise's (porpoise_rows.cpp)
			const bool wiiSide = ps5retro::Is(SideIndex(), "wii");
			pages.push_back({"graphics", Tr("Graphics"), game, Icon::Monitor});
			pages.push_back({"controls", Tr("Controls"), game, Icon::Pad});
			pages.push_back({"sound", Tr("Sound"), game, Icon::Chip});
			if (!wiiSide)
				pages.push_back({"gba", Tr("GBA"), game, Icon::Pad});
			pages.push_back({"system", Tr("System"), game, Icon::Chip});
			pages.push_back({"porpoisesetups", Tr("Setups"), game, Icon::Monitor});
			pages.push_back({"porpoisecheats", Tr("Cheats and patches"), game, Icon::Chip});
			pages.push_back({"porpoiseboost", Tr("Auto Boost"), game, Icon::Chip});
			return pages;
		}
		if (IsRetro())
		{
			// a game on a core in the app: its own core options, picture and sound, each the side's until changed
			if (OnCore() && !ps5libretro::CoreOptions(SideIndex()).empty())
				pages.push_back({"core", Tr("Emulator"), game, Icon::Chip});
			if (OnCore())
				pages.push_back({"picture", Tr("Picture and sound"), game, Icon::Monitor});
			// a game's cheats, where its core takes them (cheat_rows.cpp)
			if (ps5cheats::Supported(SideIndex()))
				pages.push_back({"cheats", Tr("Cheats"), Tr("Get this game's cheats, and turn each on or off."), Icon::Chip});
			return pages;
		}
		// SideRows' rows with a setting: Graphics, Screens, System and the Switch's Performance; the Switch's
		// Picture effects, Eden's own, are behind a row on its Graphics
		for (const SettingsPage& page : SidePages())
			if (page.id == "graphics" || page.id == "screens" || page.id == "system" || page.id == "performance")
				pages.push_back({page.id, page.title, game, page.icon});
		// its saves alone (6.15), whatever settings it has of its own (a Switch game's are Eden's)
		pages.push_back({"gamesaves", Tr("Saves"), TrF("{0}'s saves only. Settings has every system's.", m_gameName), Icon::Save});
		if (Is3ds() || IsDs())
			pages.push_back({"cheats", Tr("Cheats"), Tr("Get this game's cheats, and turn each on or off."), Icon::Chip});
		if (IsSwitch())
		{
			pages.push_back({"nxmods", Tr("Mods"), Tr("Turn this game's mods on or off."), Icon::Folder});
			pages.push_back({"nxcheats", Tr("Cheats"), Tr("Turn this game's cheats on or off."), Icon::Folder});
		}
		return pages;
	}

	void Shell::SettingsOpen(const std::string& page, bool onRows, int row)
	{
		m_switchExtrasKey.clear();
		m_porpoise.read = false; // Porpoise may have changed its files since (its game, its own menus)
		m_porpoiseSavesRead = false;
		if (InGameSettings())
			GameSettingsClose();
		m_page = page;
		m_subPage.clear();
		m_subParents.clear();
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
				Tr("Copy each mod's folder into ps5cemu/mods on a USB drive and plug it in. A mod is a graphic pack (a folder with rules.txt), or a folder with content and aoc folders. See docs/GUIDE.md on GitHub."));
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
			"\n\n" + TrP(status.skipped, "{0} folder had no rules.txt and no game. Name the folder with its game's title ID, or add it from the game's menu.",
				"{0} folders had no rules.txt and no game. Name each folder with its game's title ID, or add it from the game's menu.") :
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
			OpenHelp(Tr("No mods found"), Tr("No mods found in ps5cemu/mods on your drives.") + skipped);
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
		if (IsPorpoise() && (page == "graphics" || page == "controls" || page == "sound" || page == "gba" || page == "system"))
		{
			std::vector<Row> rows = PorpoiseRows(page);
			if (page == "system" && !InGameSettings() && m_subPage.empty())
			{
				Row reset;
				reset.kind = Row::Kind::Hold;
				reset.id = "porpoiseresetall";
				reset.label = Tr("Reset all settings");
				reset.value = Tr("Hold Cross");
				reset.description = Tr("Puts every setting back to default. Games, folders and saves are kept.");
				reset.help = reset.description;
				rows.push_back(std::move(reset));
			}
			return rows;
		}
		if (IsPorpoise() && page == "porpoisesaves")
			return PorpoiseSavesRows();
		if (IsPorpoise() && page == "porpoisesetups")
			return PorpoiseSetupRows();
		if (IsPorpoise() && page == "porpoisecheats")
			return PorpoiseCheatRows();
		if (IsPorpoise() && page == "porpoiseboost")
			return PorpoiseBoostRows();
		if (IsPorpoise() && page == "online")
			return PorpoiseOnlineRows(settings);
		if (IsRetro() && page == "core")
			return CoreOptionRows(settings);
		if (IsRetro() && (page == "files" || page == "runtime" || page == "graphics" || page == "system" || page == "screens" || page == "gamesaves"))
		{
			std::vector<Row> rows;
			const int backend = settings.retro[ps5retro::Slot(SideIndex())].retroBackend;
			// a side with a second emulator (ps5retro::kAlternatives): which of the two plays its games
			if (const ps5retro::Alternative* other = ps5retro::AlternativeOf(SideIndex()); other && page == "runtime" && !InGameSettings())
			{
				Row runtime; runtime.id = "retro-backend"; runtime.kind = Kind::Choice; runtime.label = Tr("Emulator");
				// the first: the side's core in the app while its own program is not in this copy (Mesen, ParaLLEl-N64)
				runtime.options = {ps5libretro::Plays(SideIndex()) ? std::string(ps5libretro::CoreTitle(SideIndex())) : std::string(other->first),
					other->backend.name};
				runtime.index = backend; runtime.value = runtime.options[backend];
				runtime.description = Tr("Which emulator plays this system's games, from the next game. Each keeps its own settings and saves."); rows.push_back(runtime);
			}
			if (page == "files")
			{
				Row folder; folder.id = "folder"; folder.kind = Kind::Link; folder.label = Tr("Games folder"); folder.value = GamesFolder(m_side); folder.description = Tr("Where this system's games are. Changing it doesn't move any games or saves."); rows.push_back(folder);
				Row scan; scan.id = "rescan"; scan.kind = Kind::Action; scan.label = Tr("Look for games now"); rows.push_back(scan);
			}
			// a side on its core in the app has no emulator of its own to open: the in-game menu is its
			else if (!ps5libretro::Plays(SideIndex()))
			{
				Row info; info.id = "retro-library"; info.kind = Kind::Action; info.dimmed = !ps5retro::Available(SideIndex(), backend); info.label = Tr("Open emulator settings and saves");
				info.description = Tr("Opens this emulator's own menus for its settings, controls and saves. Quit them to come back here.");
				// the GameCube and Wii sides: what is not on their pages yet (porpoise_rows.cpp)
				if (IsPorpoise())
					info.description = Tr("Porpoise's other settings, your button layouts and Wii Remote setup. Circle at the top comes back here.");
				rows.push_back(info);
			}
			if (page == "gamesaves") { Row backup; backup.id = "backup"; backup.kind = Kind::Action; backup.label = Tr("Back up to USB"); backup.description = Tr("Every system's saves. A save state only loads in the emulator that made it."); rows.push_back(backup); }
			Row diagnostic; diagnostic.id = "retro-diagnostics"; diagnostic.kind = Kind::Action; diagnostic.label = Tr("Emulator diagnostics"); rows.push_back(diagnostic);
			return rows;
		}
		const ps5settings::N3ds& hand = IsRetro() ? settings.retro[ps5retro::Slot(SideIndex())] : m_side == System::Nds ? settings.nds : m_side == System::Switch ? settings.nx : settings.n3ds;
		std::vector<Row> rows;
		std::string group; // the › page the rows made now are on (sub)
		auto toggle = [&](const char* id, const std::string& label, bool on, const std::string& description, const std::string& help) {
			Row row;
			row.kind = Kind::Toggle;
			row.id = id;
			row.label = label;
			row.on = on;
			row.value = on ? Tr("On") : Tr("Off");
			row.description = description;
			row.help = help;
			row.group = group;
			rows.push_back(row);
			return &rows.back();
		};
		auto choice = [&](const char* id, const std::string& label, std::vector<std::string> options, int index, const std::string& description,
						  const std::string& help, Kind kind = Kind::Choice) {
			Row row;
			row.kind = kind;
			row.id = id;
			row.label = label;
			row.options = std::move(options);
			row.index = std::clamp(index, 0, (int)row.options.size() - 1);
			row.value = row.options[row.index];
			row.description = description;
			row.help = help;
			row.group = group;
			rows.push_back(row);
			return &rows.back();
		};
		auto slider = [&](const char* id, const std::string& label, int value, int most, const std::string& description, const std::string& help) {
			Row row;
			row.kind = Kind::Slider;
			row.id = id;
			row.label = label;
			row.fraction = (float)value / most;
			row.value = Percent(value);
			row.description = description;
			row.help = help;
			row.group = group;
			rows.push_back(row);
			return &rows.back();
		};
		auto action = [&](Kind kind, const char* id, const std::string& label, const std::string& value, const std::string& description,
						  const std::string& help) {
			Row row;
			row.kind = kind;
			row.id = id;
			row.label = label;
			row.value = value;
			row.description = description;
			row.help = help;
			row.group = group;
			rows.push_back(row);
			return &rows.back();
		};
		// a › row: the rows made after it, until the next or endSub, are a page of their own behind it,
		// which Cross opens (SubPageRows); value: what they come to, in a word or two
		auto sub = [&](const char* page, const std::string& label, const std::string& value, const std::string& description, const std::string& help) {
			group.clear();
			Row* row = action(Kind::Link, "sub", label, value, description, help);
			row->opens = page;
			group = page;
			return row;
		};
		auto endSub = [&] { group.clear(); };
		// a Wii U row Cemu's core must be up for
		auto cemu = [&](Row* row) {
			row->needsCore = true;
			if (!m_status.coreReady)
			{
				row->dimmed = true;
				row->description = m_status.coreStarting ? Tr("Still starting. One moment.") : Tr("The Wii U emulator did not start, so this can't be changed.");
			}
			return row;
		};
		const std::string side = ps5launcher::SideName(m_side);
		auto own = [&](Row* row, const char* setting) { row->setting = setting; };

		if (page == "picture" && IsRetro())
		{
			// as its in-game menu's Picture, Graphics and Volume have them (app/ingame3ds.h), each a game can have its own of
			own(choice("retro-filter", Tr("Screen filter"), Options(ps5ingame3ds::kScreenFilters), hand.dsFilter,
					Tr("Sharp, smooth, or a tube TV's lines."),
					Tr("Sharp keeps pixels crisp and even. Smooth blends them. CRT draws the lines and colours of a tube TV, Curved CRT its rounded glass too.")),
				"dsFilter");
			own(choice("retro-size", Tr("Size"), Options(ps5ingame3ds::kPictureSizes), hand.pictureSize, Tr("How big the picture is on the TV."),
					Tr("Fit makes it as large as the screen allows. Whole pixels keeps every pixel the same size, with a frame around it. Fill screen stretches it edge to edge.")),
				"pictureSize");
			own(choice("retro-shape", Tr("Shape"), Options(ps5ingame3ds::kPictureShapes), hand.pictureShape, Tr("The picture's shape."),
					Tr("Original is the console's own. 4:3 is an old TV's. Square pixels shows them as the game draws them.")),
				"pictureShape");
			own(choice("border", Tr("Border"), Options(kBorderThemes), hand.border, Tr("Artwork around the screen."),
					Tr("Artwork around the screen."), Kind::Swatches),
				"border");
			own(slider("volume", Tr("Volume"), hand.volume, 100, Tr("The games' sound."), Tr("The game's sound. Left and Right change it by 10%.")),
				"volume");
		}
		else if (page == "ui-language")
		{
			// Cross, Left or Right opens the list
			int index = 0;
			choice("ui-language", Tr("Language"), LanguageOptions(index), index, Tr("The menu language, and game names where available."),
				Tr("Playtendoh uses the PS5's language unless you choose another. Game names follow it where the game has one in that language, otherwise they are in English. Descriptions from GameTDB are in English."));
		}
		else if (page == "graphics" && IsWiiU())
		{
			own(choice("upscaling", Tr("Upscaling to 4K"), Options(kUpscaleFilters), settings.upscaleFilter, Tr("How the picture is scaled to the TV."),
					Tr("Bicubic is sharp, Bicubic Hermite a little softer, Bilinear softest. Nearest neighbour keeps pixels square. AMD FSR 1 keeps edges clean for games below 4K, at a small GPU cost.")),
				"upscaleFilter");
			// the display's rate and what a game's frames do in it
			std::string rate = settings.highFrameRate ? "120 Hz" : "60 Hz";
			if (settings.framePacing > 1)
				rate += " · " + ps5display::FramePacingName(settings.framePacing, settings.highFrameRate);
			if (settings.frameGeneration)
				rate += std::string(" · ") + Tr("Frame generation");
			// tr: Settings > Graphics: a row that opens the frame rate's settings (120 Hz output, frame pacing, frame generation)
			sub("framerate", Tr("Frame rate"), rate, Tr("120 Hz output, frame pacing and frame generation."),
				Tr("Refresh rate, frame pacing and frame generation. Press Triangle on each for more."));
			own(toggle("highframerate", Tr("120 Hz output"), settings.highFrameRate, Tr("For displays that take 120 Hz."),
					Tr("Uses 120 Hz on displays that support it. Games run at normal speed, and late frames wait less.")),
				"highFrameRate");
			std::vector<std::string> pacing;
			for (int i = 1; i <= 3; i++)
				pacing.push_back(ps5display::FramePacingName(i, settings.highFrameRate));
			own(choice("framepacing", Tr("Frame pacing"), pacing, settings.framePacing - 1, Tr("Holds a game to an even frame rate."),
					Tr("Keeps each frame on screen for the same time, so a game that can't hold full speed runs evenly: 60 or 40 fps with 120 Hz output, 30 or 20 without. Also in the in-game menu."),
					Kind::Segmented),
				"framePacing");
			own(toggle("framegeneration", Tr("Frame generation"), settings.frameGeneration, Tr("Shows 30 fps games at 60."),
					Tr("Adds a frame between each pair, so a 30 fps game shows at 60 (or 60 at 120 with 120 Hz output). Adds a little delay. Also in the in-game menu.")),
				"frameGeneration");
			endSub();
			// ReShade's preset, as a PC's ReShade makes it (port/cemu/ReShadePS5.h)
			own(toggle("reshade", Tr("ReShade"), settings.reshade, TrF("Your ReShade preset and effects, from {0}.", ps5paths::CemuRoot() + "/reshade"),
					TrF("Runs a ReShade preset made on a PC over the game. Put ReShadePreset.ini (or presets/<title ID>.ini for one game) and its Shaders and Textures folders in {0}. Also in the in-game menu.",
						ps5paths::CemuRoot() + "/reshade")),
				"reshade");
			own(toggle("overlay", Tr("Performance overlay"), settings.overlay, Tr("Frame rate, CPU and memory in a corner."),
					Tr("Shows frame rate, CPU and memory use in the top-left corner. Also in the in-game menu.")),
				"overlay");
			// tr: Settings > Graphics: a row that opens the shaders' settings
			sub("shaders", Tr("Shaders"), "", Tr("How new shaders are built, and shader notices."),
				Tr("Async shader compile and shader notices. Press Triangle on each for more."));
			own(toggle("async", Tr("Async shader compile"), settings.asyncShaders, Tr("No stutter while new shaders build."),
					Tr("On: shaders build in the background, so less stutter, but some things may appear late. Off: the game waits for each shader, which stutters but draws everything right.")),
				"asyncShaders");
			toggle("shadernotices", Tr("Shader notices"), settings.shaderNotices, Tr("Shows a notice while shaders build."),
				Tr("A notice in the corner while a game builds shaders. Off hides it; shaders are built either way. Also in the in-game menu."));
			endSub();
		}
		else if (page == "graphics" && Is3ds())
		{
			own(choice("resolution", Tr("Internal resolution"), Options(kResolutions), hand.resolution - 1, Tr("Higher is sharper but needs more power."),
					Tr("How large 3D scenes are drawn before scaling to the TV. Higher is sharper but needs more power, and some games slow down more."),
					Kind::Stepper),
				"resolution");
			std::string textures = TextureFilterNames()[FilterIndex(hand.textureFilter)];
			if (hand.customTextures)
				textures += std::string(" · ") + Tr("Custom textures");
			// tr: Settings > Graphics: a row that opens the textures' settings (the texture filter, custom textures)
			sub("textures", Tr("Textures"), textures, Tr("The texture filter, and texture packs."),
				Tr("Texture filter and your own texture packs. Press Triangle on each for more."));
			own(choice("filter", Tr("Texture filter"), TextureFilterNames(), FilterIndex(hand.textureFilter), Tr("Smooths textures as they are scaled up."),
					Tr("Smooths textures as they are scaled up. None keeps them as the 3DS draws them. At high resolutions this is the heaviest setting: if a game stutters, try None first.")),
				"textureFilter");
			own(toggle("customtextures", Tr("Custom textures"), hand.customTextures, Tr("Texture packs from azahar/load/textures."),
					// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
					TrF("Texture packs in {0}/azahar/load/textures/<title ID>.", ecam_layout::Parent(ps5paths::Azahar()))),
				"customTextures");
			// tr: Settings > Graphics' Shaders row, its value: where the 3DS's vertex shaders run, on the GPU (Hardware) or on the CPU (Software)
			sub("shaders", Tr("Shaders"), hand.hwShaders ? TrC("shaders", "Hardware") : TrC("shaders", "Software"),
				Tr("Where the 3DS's shaders run, and how new ones are built."),
				Tr("Hardware shaders, outline fix and async shader compile. Press Triangle on each for more."));
			own(toggle("hwshaders", Tr("Hardware shaders"), hand.hwShaders, Tr("Off draws 3D exactly on the CPU. Off for Pokémon by default."),
					Tr("On runs 3D on the PS5's GPU, the fastest way. Off runs it on the CPU: slower, but outlines and lighting are exact. Off by default for the 3DS Pokémon games, whose outlines, lighting and shadows glitch otherwise. Applies to the next game.")),
				"hwShaders");
			own(toggle("outlinefix", Tr("Outline fix"), hand.invariantPosition, Tr("Fixes moving black dots on outlined 3D models."),
					Tr("Fixes moving black dots on outlined 3D models with hardware shaders on. For the Pokémon games, turn Hardware shaders off instead. Applies to the next game.")),
				"invariantPosition");
			// off by default, as desktop Azahar: on, a draw whose shader is still building is skipped (Alpha
			// Sapphire's first cutscene drew its Pokemon as their dark outlines alone)
			own(toggle("async-3ds", Tr("Async shader compile"), hand.asyncShaders, Tr("No stutter while new shaders build."),
					Tr("On: shaders build in the background, so less stutter, but some things may appear late. Off: the game waits for each shader, which stutters but draws everything right.")),
				"asyncShaderCompilation");
			endSub();
			own(toggle("framegeneration-3ds", Tr("Frame generation"), hand.frameGeneration, Tr("Shows 30 fps games at 60."),
					Tr("Adds a frame between each pair, so a 30 fps game shows at 60 (or 60 at 120 with 120 Hz output). Adds a little delay. Also in the in-game menu.")),
				"frameGeneration");
			own(toggle("anaglyph", Tr("3D for red and cyan glasses"), hand.anaglyph, Tr("The 3DS's 3D, in red and cyan."),
					Tr("Shows the 3DS's 3D for red and cyan glasses, with the 3D slider all the way up. Colours change with it. Applies to the next game.")),
				"anaglyph");
			toggle("performance-hand", Tr("Performance overlay"), hand.performance, Tr("Shows frame rate and speed in the top-left corner."),
				Tr("Shows frame rate and speed in the top-left corner."));
		}
		else if (page == "graphics" && IsSwitch())
		{
			own(choice("nx-resolution", Tr("Internal resolution"), Options(kNxResolutions), hand.nxResolution, Tr("Higher is sharper but needs more power."),
					IsSwitch2() ? Tr("How large Lichii draws a game before scaling it to the TV. Higher values are sharper and use more graphics memory.") :
					Tr("How large games are drawn before scaling to the TV. 1x is 1080p docked and 720p handheld. Higher is sharper but needs more power and graphics memory. Applies to the next game."),
					Kind::Stepper),
				"nxResolution");
			std::vector<std::string> filters;
			for (int filter : kNxFilters)
				filters.push_back(Tr(kUpscaleFilters[filter]));
			own(choice("nx-filter", Tr("Upscaling to 4K"), filters, hand.nxFilter, Tr("How the picture is scaled to the TV."),
					IsSwitch2() ? Tr("Choose Bilinear, AMD FSR 1, Bicubic or Nearest neighbour. Applies when the next game starts.") :
					Tr("Bilinear is soft, Bicubic sharper, Nearest neighbour keeps pixels square. AMD FSR 1 keeps edges clean for games below 4K, at a small GPU cost.")),
				"nxFilter");
			if (!IsSwitch2())
			{
			// FSR's sharpening, anti-aliasing and Eden's ReShade looks (switch_extras.cpp's, which SettingRows adds
			// behind this row), what is done over the picture
			const std::vector<std::string> antiAliasing{Tr("Off"), "FXAA", "SMAA"};
			// tr: Settings > Graphics on the Switch side: a row that opens FSR sharpening, anti-aliasing and Eden's ReShade looks
			sub("nxpost", Tr("Picture effects"), antiAliasing[std::clamp(hand.nxAntiAliasing, 0, 2)], Tr("Sharpening, anti-aliasing and ReShade."),
				Tr("FSR sharpening, anti-aliasing and ReShade looks (built-in or your own). Press Triangle on each for more."));
			// FSR 1's own setting, while it is the filter
			if (hand.nxFilter == 1)
			{
				std::vector<std::string> levels;
				for (int value : kNxSharpening)
					levels.push_back(Percent((int)std::lround(100.0 * std::pow(2.0, -value / 100.0))));
				own(choice("nx-sharpening", Tr("FSR sharpening"), levels, NxSharpeningIndex(hand.nxSharpening),
						Tr("How much AMD FSR 1 sharpens the picture."),
						Tr("How much AMD FSR 1 sharpens the picture. 100% is the most; each step down halves it. Also in the in-game menu.")),
					"nxSharpening");
			}
			own(choice("nx-aa", Tr("Anti-aliasing"), antiAliasing, hand.nxAntiAliasing, Tr("Smooths the jagged edges of the picture."),
					Tr("FXAA smooths edges lightly, SMAA more cleanly, each at a small GPU cost. Also in the in-game menu."),
					Kind::Segmented),
				"nxAntiAliasing");
			endSub();
			own(choice("nx-astc", Tr("ASTC textures"), {Tr("Compressed"), Tr("Full quality")}, hand.nxAstc,
					Tr("Compressed saves graphics memory; full quality looks best."),
					Tr("Many Switch games use ASTC textures, which must be converted for the PS5. Compressed saves memory; Full quality uses four times as much, for games whose textures look blocky. Applies to the next game."),
					Kind::Segmented),
				"nxAstc");
			}
			// The picture this runtime hands the console, and the TV's refresh rate.
			const char* outputs[] = {"1080p", "1440p", "2160p"};
			std::string output = std::string(outputs[std::clamp(hand.nxOutput, 0, 2)]) + (hand.nxHighRefresh ? " · 120 Hz" : " · 60 Hz");
			if (hand.nxFrameGeneration)
				output += std::string(" · ") + Tr("Frame generation");
			// tr: Settings > Graphics: a row that opens the output resolution, refresh rate and frame generation
			sub("output", Tr("Output to the TV"), output, Tr("Output size, 120 Hz and frame generation."),
				IsSwitch2() ? Tr("Output size, refresh rate and frame generation. Each applies to the next game.") :
				Tr("Output size, 120 Hz and frame generation. Each applies to the next game."));
			own(choice("nx-output", Tr("Output resolution"), {"1080p", "1440p", "2160p"}, std::clamp(hand.nxOutput, 0, 2),
					IsSwitch2() ? Tr("The resolution sent to the TV. Applies to the next game.") :
					Tr("The resolution sent to the TV. Applies to the next game."),
					IsSwitch2() ? Tr("The resolution sent to the TV. Applies to the next game.") :
					Tr("The resolution sent to the TV. Applies to the next game."), Kind::Segmented),
				"nxOutput");
			own(toggle("nx-120hz", Tr("120 Hz"), hand.nxHighRefresh, Tr("For TVs that support 120 Hz. Applies to the next game."),
					Tr("For TVs that support 120 Hz. Applies to the next game.")),
				"nxHighRefresh");
			// This runtime's own frame generation.
			own(toggle("nx-framegeneration", Tr("Frame generation"), hand.nxFrameGeneration, Tr("Shows 30 fps games at 60."),
					IsSwitch2() ? Tr("Frame generation, where supported. Applies to the next game.") :
					// tr: {0} is where Lossless.dll goes: /data/eden/user/lossless/Lossless.dll, or the same on the app's drive
					TrF("Adds a frame between each pair, so a 30 fps game shows at 60. Uses Lossless Scaling if your own Lossless.dll is at {0}. Adds a little delay. Applies to the next game.",
						ps5eden::LosslessFile())),
				"nxFrameGeneration");
			endSub();
			own(toggle("performance-hand", Tr("Performance overlay"), hand.performance, Tr("Shows frame rate and speed in the top-left corner."),
				Tr("Shows frame rate and speed in the top-left corner.")), "performance");
		}
		else if (page == "graphics")
		{
			own(choice("dsfilter", Tr("Screen filter"), Options(kDsFilters), hand.dsFilter, Tr("How the DS's screens are scaled."),
					Tr("Sharp keeps pixels crisp and even at any size. Smooth blends them. Square pixels can look uneven at some sizes. Also in the in-game menu."),
					Kind::Segmented),
				"dsFilter");
			const bool executable = ps5privilege::Current().executable;
			own(toggle("dsjit", Tr("Recompiler"), hand.dsJit,
					executable ? Tr("Much faster than the interpreter.") : Tr("Not available: games run in the slower mode."),
					Tr("Runs games much faster, when the PS5 allows it (Diagnostics shows whether it does). Turn it off for a game that runs wrong. Applies to the next game.")),
				"dsJit");
			toggle("performance-hand", Tr("Performance overlay"), hand.performance, Tr("Shows frame rate and speed in the top-left corner."),
				Tr("Shows frame rate and speed in the top-left corner."));
		}
		else if (page == "screens")
		{
			own(choice("layout", Tr("Screen layout"), Options(kLayouts), hand.layout, Tr("How the two screens fit on the TV."),
					Tr("Stacked, top only, large top with small bottom, or side by side. In a game, touchpad click + R1 changes it.")),
				"layout");
			if (IsDs() || Is3ds())
				own(choice("dsrotation", Tr("Screen rotation"), Options(kDsRotations), hand.dsRotation,
						Tr("For games held sideways, like a book."),
						IsDs() ? Tr("Turns both screens a quarter turn at a time, for games played with the DS held sideways. The touchpad follows the turned screen. Also in the in-game menu.") :
								 Tr("Turns both screens a quarter turn at a time, for games played with the 3DS held sideways. The touchpad follows the turned screen. Also in the in-game menu.")),
					"dsRotation");
			if (IsDs() || Is3ds())
				own(toggle("screengap", Tr("Screen gap"), hand.screenGap, Tr("A gap between the screens, like the hinge."),
						Tr("Adds a gap between the two screens in any layout. Can be set per game, and in the in-game menu.")),
					"screenGap");
			if (IsDs() || Is3ds())
				own(toggle("hiddentouch", Tr("Touch without the bottom screen"), hand.hiddenTouch,
						Tr("Touch the bottom screen while only the top one shows."),
						Tr("With only the top screen shown, the touchpad still touches the bottom screen, with a cursor on the top one. For games that ask for a touch now and then. Can be set per game, and in the in-game menu.")),
					"hiddenTouch");
			own(choice("border", Tr("Border"), Options(kBorderThemes), hand.border, Tr("Artwork around the screens."),
					Tr("Artwork around the screens, in every layout. Also in the in-game menu."),
					Kind::Swatches),
				"border");
			// the DS's touch, where its cursor lands and the right stick as the stylus (on System's page before
			// System took the sound)
			if (IsDs())
			{
				std::vector<std::string> touchOffsets, touchScales;
				for (int value = -20; value <= 20; value++) touchOffsets.push_back(fmt::format("{}%", value));
				for (int value = 50; value <= 150; value++) touchScales.push_back(fmt::format("{}%", value));
				const auto touchHelp = Tr("Adjusts where touchpad touches land, for games where they're off. 0% and 100% leave it unchanged. Doesn't affect the remote touch screen. Applies to the next game.");
				std::string touch;
				if (hand.dsTouchX != 0 || hand.dsTouchY != 0 || hand.dsTouchScale != 100)
					touch = fmt::format("{}% · {}% · {}%", hand.dsTouchX, hand.dsTouchY, hand.dsTouchScale);
				if (hand.dsStylus)
					// tr: Settings > Screens' Touch row, its value: the right stick is the stylus
					touch += (touch.empty() ? "" : " · ") + std::string(Tr("Stick as stylus"));
				// tr: Settings > Screens: a row that opens the touch screen's settings (where the touchpad's cursor lands, the right stick as the stylus)
				sub("touch", Tr("Touch"), touch.empty() ? std::string(Tr("Default")) : touch,
					Tr("Touch position, and the right stick as stylus."),
					Tr("Adjusts where touches land, and lets the right stick drag a touch on the bottom screen. Press Triangle on each for more."));
				own(choice("dstouchx", Tr("Touch, left or right"), touchOffsets, hand.dsTouchX + 20, touchHelp, touchHelp), "dsTouchX");
				own(choice("dstouchy", Tr("Touch, up or down"), touchOffsets, hand.dsTouchY + 20, touchHelp, touchHelp), "dsTouchY");
				own(choice("dstouchscale", Tr("Touch, scale"), touchScales, hand.dsTouchScale - 50, touchHelp, touchHelp), "dsTouchScale");
				// tr: a DS setting: the right stick as the touch screen's stylus (DraStic has it), for Metroid Prime Hunters' aiming
				own(toggle("dsstylus", Tr("The right stick as the stylus"), hand.dsStylus, Tr("Push it to drag a touch on the bottom screen."),
					Tr("Pushing the right stick touches the middle of the bottom screen and drags as you push, for games that aim by touch. Let go to lift. Applies to the next game.")),
					"dsStylus");
				endSub();
			}
		}
		else if (page == "controls")
		{
			toggle("rumble", Tr("Vibration"), settings.rumble, Tr("DualSense rumble, in games and the launcher."),
				Tr("Off: no rumble in games or the launcher. Applies to every system."));
			if (IsSwitch() || IsSwitch2())
			{
				Row* deadzone = slider("nx-deadzone", Tr("Stick deadzone"), hand.nxDeadzone, 30, Tr("How far a stick moves before it counts."),
					Tr("How far each stick moves before the game sees it. Raise it if there is drift; lower it for finer control. Also in the in-game menu."));
				deadzone->value = Percent(hand.nxDeadzone);
				own(deadzone, "nxDeadzone");
			}
			// the turbo button is the app's own, which Eden's program does not have
			auto turbo = [&] {
				int index = 0;
				auto options = TurboOptions(IsWiiU() ? settings.turbo : hand.turbo, index);
				choice("turbo", Tr("Turbo button"), std::move(options), index, Tr("Hold it to press repeatedly."),
					Tr("Holding this button presses it about 15 times a second, for games that make you mash. None turns it off. Applies to every player."));
			};
			// a Switch game's buttons, as Eden maps them (port/eden/eden.h)
			if (IsSwitch())
			{
				const std::string set = TrP((int)ps5eden::ListMappings(hand).size(), "{0} button set", "{0} buttons set");
				// tr: Settings > Controls: a row that opens the buttons' settings (each button, the defaults)
				sub("buttons", Tr("Buttons"), set, Tr("Each button, and the defaults."),
					Tr("Choose the DualSense button for each Switch button, or reset to default."));
				// tr: Settings > Controls > Buttons: the row that opens the list of every button and the DualSense button on it
				action(Kind::Link, "buttons", Tr("Each button"), set, Tr("Choose the DualSense button for each button."),
					Tr("Choose the DualSense button for each Switch button. By default A is on Circle and B on Cross as on the Switch, + is Options and - is touchpad click."));
				action(Kind::Hold, "resetcontrols", Tr("Reset to defaults"), m_message.empty() ? Tr("Hold Cross") : m_message,
					Tr("Default buttons and deadzone."), Tr("Default buttons and deadzone."));
				endSub();
			}
			if (IsWiiU())
			{
				turbo();
				for (int player = 0; player < ps5pad::kMaxPlayers; player++)
				{
					const auto controls = m_status.coreReady ? ps5emu::GetPlayerControls(player) : ps5emu::PlayerControls{};
					Row* row = action(Kind::Link, "player", TrF("Player {0}", player + 1), m_status.coreReady ? TypeName(controls.type) : "",
						controls.connected ? Tr("Cross sets this player's controller, motion and buttons.") :
							ps5emu::IsRealWiiRemote(controls.type) ? Tr("Set to a real Wii Remote, but none found. Cross to change it.") : Tr("No DualSense for this player yet."),
						Tr("This player's controller, motion, vibration, deadzones and buttons. Player 1 is the user who started Playtendoh; other signed-in users are players 2 to 4."));
					row->index = player;
					// a real Wii Remote not found yet still opens: its page is where it's changed back
					row->dimmed = m_status.coreReady && !controls.connected && !ps5emu::IsRealWiiRemote(controls.type);
					cemu(row);
				}
			}
			else if (!IsSwitch())
			{
				const bool aOnCircle = ps5azahar::MappedInput(hand, ps5azahar::Button::A) == ps5emu::PadInput::Circle;
				choice("abbuttons", Tr("A and B"), {Tr("A on Circle"), Tr("A on Cross")}, aOnCircle ? 0 : 1,
					Tr("A on Circle as on the console, or on Cross."),
					Tr("A on Circle and B on Cross as on the console, or A on Cross and B on Circle as on PlayStation. X and Y swap to match. Also in the in-game menu."),
					Kind::Segmented);
				if (Is3ds())
					toggle("motion", Tr("Motion controls"), hand.motion, Tr("Use DualSense motion."),
						Tr("Use DualSense motion for games that aim or steer by tilting the 3DS."));
				slider("deadzone", Tr("Stick deadzone"), hand.deadzone, 50, Tr("How far a stick moves before it counts."),
					Is3ds() ? Tr("How far a stick moves before the game sees it, for the circle pad and C-stick. Raise it if there is drift; lower it for finer control.") :
							  Tr("How far the left stick moves before it presses the D-pad. Raise it if there is drift."))
					->value = Percent(hand.deadzone);
				int mapped = 0;
				if (Is3ds())
				{
					const auto mappings = ps5azahar::ListMappings(hand);
					mapped = (int)std::count_if(mappings.begin(), mappings.end(), [](const ps5emu::ButtonMapping& m) { return !m.input.empty(); });
				}
				else
					for (ps5azahar::Button button : ps5azahar::DsButtons())
						mapped += ps5azahar::MappedInput(hand, button) != ps5emu::PadInput::None;
				const std::string set = TrP(mapped, "{0} button set", "{0} buttons set");
				sub("buttons", Tr("Buttons"), set, Tr("Each button, the turbo button, and the defaults."),
					Tr("Choose the DualSense button for each button, set a turbo button, or reset to default."));
				action(Kind::Link, "buttons", Tr("Each button"), set, Tr("Choose the DualSense button for each button."),
					Is3ds() ? Tr("Choose the DualSense button for each 3DS button. By default A is on Circle and B on Cross as on the 3DS. The circle pad is the left stick, the C-stick the right stick, and the touchpad the touch screen.") :
							  Tr("Choose the DualSense button for each DS button. By default A is on Circle and B on Cross as on the DS. The left stick is a second D-pad, the touchpad is the touch screen, and holding R3 blows into the mic."));
				turbo();
				action(Kind::Hold, "resetcontrols", Tr("Reset to defaults"), m_message.empty() ? Tr("Hold Cross") : m_message,
					Is3ds() ? Tr("Default buttons, motion and deadzone.") : Tr("Default buttons and deadzone."), Tr("The default buttons, motion and deadzone."));
				endSub();
			}
		}
		else if (page == "system" && Is3ds())
		{
			// Azahar looks for it in its own files, which only the 3DS side opens
			ps5emu::Game home;
			const bool homeMenu = m_prepared == System::N3ds && ps5azahar::HomeMenu(hand.region, home);
			own(choice("region", Tr("Region"), Options(kRegions), hand.region + 1, Tr("The emulated 3DS's region."),
					Tr("Automatic uses each game's region. A game from another region may not start. Applies to the next game.")),
				"region");
			own(choice("language", Tr("Language"), Options(kLanguages), hand.language + 1, Tr("The emulated 3DS's language."),
					Tr("The language games use if they follow the console. Applies to the next game.")),
				"language");
			std::vector<std::string> clocks;
			for (int clock : kClocks)
				clocks.push_back(fmt::format("{}%", clock));
			own(choice("cpuclock", Tr("CPU clock"), clocks, ClockIndex(hand.cpuClock), Tr("The emulated 3DS's CPU speed."),
					Tr("Higher can smooth a game that slows down, but needs more power. Lower can speed up some games. 100% is a real 3DS. Also in the in-game menu.")),
				"cpuClock");
			Row* row = action(Kind::Action, "homemenu", Tr("Home Menu"), homeMenu ? Tr("Start") : Tr("Run Artic Setup first"),
				Tr("The 3DS Home Menu, from your console's files."),
				Tr("Starts the 3DS Home Menu. Needs your console's system files, copied with Artic Setup."));
			row->dimmed = !homeMenu;
		}
		else if (page == "system" && IsSwitch())
		{
			// tr: the emulated Switch's console mode: in its dock, or handheld
			own(choice("nx-docked", Tr("Console mode"), {TrC("switch mode", "Docked"), TrC("switch mode", "Handheld")}, hand.nxDocked ? 0 : 1,
					Tr("Docked looks sharpest. Handheld runs lighter."),
					Tr("Docked draws for a TV (1080p for most games). Handheld draws for the console's screen (720p) and runs lighter. A few games differ between them. Applies to the next game."),
					Kind::Segmented),
				"nxDocked");
			std::vector<std::string> controllers;
			for (const char* name : kNxControllers)
				controllers.push_back(Tr(name));
			own(choice("nx-controller", Tr("Controller type"), controllers, hand.nxController + 1, Tr("The controller the game sees."),
					Tr("Automatic gives each game the controller it expects, usually a Pro Controller. If a game doesn't work with it, try a Joy-Con pair. Handheld is player 1 only. Also in the in-game menu.")),
				"nxController");
			// Eden's speed limit, as ProsperoEden's Settings > Performance has it
			std::vector<std::string> speeds;
			for (int percent : {100, 150, 200, 300})
				speeds.push_back(Percent(percent));
			speeds.push_back(TrC("speed limit", "None"));
			if (!IsSwitch2()) own(choice("nx-speedlimit", Tr("Speed limit"), speeds, std::clamp(hand.nxSpeedLimit, 0, 4), Tr("How fast the game may run."),
					Tr("Compared to the Switch's speed. Above 100% can let a 30 fps game reach 60, but games that time by frames play faster. None runs as fast as possible. Applies to the next game.")),
				"nxSpeedLimit");
			// the system language games see, each named in its own language
			std::vector<std::string> languages{Tr("Automatic")};
			for (const auto& language : ps5eden::kLanguages)
				languages.push_back(language.name);
			own(choice("nx-language", Tr("Language"), languages, std::clamp(hand.nxLanguage + 1, 0, (int)languages.size() - 1),
					Tr("The language Switch games use. Automatic matches the menus."),
					Tr("The language Switch games use. Automatic matches the menus.")),
				"nxLanguage");
		}
		else if (page == "performance" && IsSwitch())
		{
			// ProsperoEden's Settings > Performance, each for every Switch game or one game's own
			if (!IsSwitch2()) own(choice("nx-memory", Tr("Memory"), {"4 GB", "6 GB", "8 GB"}, std::clamp(hand.nxMemory, 0, 2),
					Tr("4 GB matches the Switch. More is for mods that need it. Applies to the next game."),
					Tr("4 GB matches the Switch. More is for mods that need it. Applies to the next game."), Kind::Segmented),
				"nxMemory");
			const struct
			{
				const char* id;
				const char* label;
				const char* about;
				bool ps5settings::N3ds::* value;
				const char* field;
			} switches[] = {
				{"nx-compactmemory", TrMark("Compact memory"), TrMark("Saves memory for the biggest games."),
					&ps5settings::N3ds::nxCompactMemory, "nxCompactMemory"},
				{"nx-blocklist", TrMark("Precompile code"), TrMark("Prepares code from past sessions ahead of time, for fewer hitches."),
					&ps5settings::N3ds::nxBlockList, "nxBlockList"},
				// the shortcuts, behind Accuracy
				{"nx-fastgpu", TrMark("Fast GPU"), TrMark("Lowest GPU accuracy. Faster, but some effects may look wrong."),
					&ps5settings::N3ds::nxFastGpu, "nxFastGpu"},
				{"nx-unsafecpu", TrMark("Unsafe CPU"), TrMark("Faster, less exact maths. A few games misbehave."),
					&ps5settings::N3ds::nxUnsafeCpu, "nxUnsafeCpu"},
				{"nx-unsafedma", TrMark("Unsafe DMA"), TrMark("Faster memory transfers. A few games show glitches."),
					&ps5settings::N3ds::nxUnsafeDma, "nxUnsafeDma"},
				{"nx-reactive", TrMark("Reactive flushing"), TrMark("Keeps some effects correct. Off is faster, but some effects break."),
					&ps5settings::N3ds::nxReactiveFlushing, "nxReactiveFlushing"},
				{"nx-skipinvalidation", TrMark("Skip cache invalidation"), TrMark("Faster, and rarely causes problems."),
					&ps5settings::N3ds::nxSkipInvalidation, "nxSkipInvalidation"},
			};
			constexpr int kShortcuts = 2; // switches' first behind Accuracy
			if (!IsSwitch2()) own(toggle(switches[0].id, Tr(switches[0].label), hand.*(switches[0].value), Tr(switches[0].about), Tr(switches[0].about)), switches[0].field);
			own(toggle("nx-async", Tr("Async shader compile"), hand.nxAsyncShaders, Tr("No stutter while new shaders build."),
					Tr("On: shaders build in the background, so less stutter, but some things may appear late. Off: the game waits for each shader, which stutters but draws everything right.")),
				"nxAsyncShaders");
			own(toggle(switches[1].id, Tr(switches[1].label), hand.*(switches[1].value), Tr(switches[1].about), Tr(switches[1].about)),
				switches[1].field);
			// how many shortcuts differ from Eden's own choice (all off, Reactive flushing on)
			const ps5settings::N3ds defaults;
			int changed = 0;
			for (int i = kShortcuts; i < (int)std::size(switches); i++)
				changed += hand.*(switches[i].value) != defaults.*(switches[i].value);
			// tr: Settings > Performance: a row that opens Eden's accuracy shortcuts (Fast GPU, Unsafe CPU, ...); its value: how many are changed
			sub("accuracy", Tr("Accuracy"), changed ? TrP(changed, "{0} changed", "{0} changed") : std::string(Tr("Default")),
				Tr("Speed shortcuts. Faster, but less exact."),
				Tr("These trade accuracy for speed, for every Switch game or just one. Press Triangle on each for more."));
			for (int i = kShortcuts; i < (int)std::size(switches); i++)
				own(toggle(switches[i].id, Tr(switches[i].label), hand.*(switches[i].value), Tr(switches[i].about), Tr(switches[i].about)), switches[i].field);
			endSub();
		}
		else if (page == "system" && IsDs())
		{
			own(choice("language", Tr("Language"), Options(kLanguages), hand.language + 1, Tr("The emulated DS's language."),
					Tr("The DS's language, used by many games. Japanese, English, French, German, Italian, Spanish and Chinese; others use English. Applies to the next game.")),
				"language");
			const bool found = ps5melonds::OwnBiosFound();
			own(toggle("dsownbios", Tr("Your DS's BIOS"), hand.dsOwnBios, found ? Tr("Found in melonds/bios.") : Tr("Not found: the built-in BIOS is used."),
					// tr: {0} is the folder for a DS's own BIOS and firmware dumps
					TrF("Uses bios7.bin, bios9.bin and firmware.bin from your own DS, in {0}. Without them, most games run fine on the built-in BIOS. Applies to the next game.",
						ps5melonds::Root() + "/bios")),
				"dsOwnBios");
			own(choice("dsmic", Tr("Microphone"), {Tr("Noise"), Tr("Silence"), Tr("A recording")}, hand.dsMicSource,
				Tr("What the DS hears while R3 is held. Applies to the next game."),
				// tr: {0} is melonDS's folder, /data/melonds or where the DS's files are; keep <the game's file name> as a placeholder
				TrF("A 16-bit mono WAV (8,000 to 48,000 Hz, up to 16 MB): {0}/microphone/<the game's file name>.wav for one game, or {0}/microphone.wav for all. It loops while R3 is held.", ps5melonds::Root())), "dsMicSource");
			own(toggle("dsslot2", Tr("GBA cartridge in Slot-2"), hand.dsSlot2,
				Tr("For DS games that read a GBA game in the second slot. It does not play GBA games."),
				// tr: {0} is melonDS's folder, /data/melonds or where the DS's files are
				TrF("Put the GBA game in {0}/slot2 named like the DS game, ending .gba, with its .sav if it has one. Its save is then kept in {0}/slot2-saves; your files are not changed. Applies to the next game.", ps5melonds::Root())), "dsSlot2");
		}
		else if (page == "files")
		{
			const std::string& folder = GamesFolder(m_side);
			action(Kind::Link, "folder", Is3ds() ? Tr("3DS games") : IsDs() ? Tr("DS games") : IsSwitch2() ? Tr("Switch 2 games") : IsSwitch() ? Tr("Switch games") : Tr("Wii U games"),
				ShortPath(IsSwitch() && !IsSwitch2() ? ps5eden::ScanFolder(folder) : folder, 34),
				Tr("Where your games are. Cross picks another."),
				// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
				IsWiiU() ? TrF("Wii U games: .wua, .wud, .wux, folders with code, content and meta, or in .zip, .7z or .rar. Encrypted .wud and .wux need keys in {0}/keys.txt.", ps5paths::CemuRoot()) :
				Is3ds()	 ? Tr("3DS games: decrypted .3ds, .cci, .cxi, .3dsx or compressed dumps, or in .zip, .7z or .rar. Install CIA files with Install a CIA file. Encrypted games won't start: decrypt them on your 3DS with GodMode9.") :
				IsSwitch2() ? Tr("Switch 2 game folders need ExeFS and RomFS. Put each game in its own folder; keep updates and DLC where Lichii expects them.") :
				IsSwitch() ? Tr("Switch games: .nsp and .xci files, each alone or in its own folder with its update and DLC.") :
						   Tr("DS games: .nds files, or in .zip, .7z or .rar, here or in subfolders."));
			action(Kind::Action, "rescan", Tr("Look for games now"), m_scanning ? Tr("Looking…") : "", Tr("Search this system's folder again."),
				Tr("Searches this system's folder again for added or removed games."));
			// #75: a large library on a share is a long look at every start
			if (!ps5netfs::shares().empty())
				toggle("lookonshares", Tr("Look on network shares at every start"), settings.lookOnShares,
					Tr("Off shows the games found last time, at once."),
					Tr("Searching a network share each time can be slow with a large library. Off shows the games found last time right away; use Look for games now after adding some. Folders on the PS5 or USB are always searched."));
			if (IsSwitch())
			{
				if (IsSwitch2())
				{
					const bool keys = ps5eglantine::KeysFound(), firmware = ps5eglantine::FirmwareFound();
					const std::string found = keys && firmware ? std::string(Tr("Found")) : keys ? std::string(Tr("Firmware missing")) :
						firmware ? std::string(Tr("Keys missing")) : std::string(Tr("Missing"));
					action(Kind::Action, "nxkeys", Tr("Keys and firmware"), found,
						!keys ? Tr("prod.keys is missing: Lichii needs your own Switch keys") :
						!firmware ? Tr("Firmware is missing: some games need it") : Tr("Lichii found keys and firmware."),
						TrF("Put your own prod.keys in {0}/keys. Neither Playtendoh nor Lichii includes keys.", ps5eglantine::DataFolder()) +
						"\n\n" + TrF("Put your own Switch firmware files in {0}/firmware. Neither app includes firmware.", ps5eglantine::DataFolder()));
				}
				else
				{
				// Eden's own files: the keys and firmware it needs
				const std::string files = ps5eden::GameFilesFolder();
				// the two in one row: what is missing first, Cross saying where each goes
				const bool keys = ps5eden::KeysFound(), firmware = ps5eden::FirmwareFound();
				// tr: Settings > Game files' Keys and firmware row, its value when the firmware is missing and the keys are there
				const std::string found = keys && firmware ? std::string(Tr("Found")) : keys ? std::string(Tr("Firmware missing")) :
					// tr: Settings > Game files' Keys and firmware row, its value when prod.keys is missing and the firmware is there
					firmware ? std::string(Tr("Keys missing")) : std::string(Tr("Missing"));
				// tr: Settings > Game files: one row for the Switch's keys (prod.keys) and its firmware, which Eden needs
				action(Kind::Action, "nxkeys", Tr("Keys and firmware"), found,
					!keys			? Tr("prod.keys is missing: Switch games need it") :
						!firmware	? Tr("No firmware yet: many games need it to start") :
									  Tr("Your keys are ready."),
					TrF("Switch games need your own console's keys: prod.keys (and title.keys) from your Switch, in {0}/keys. Playtendoh does not include keys.",
						files) +
						"\n\n" +
						TrF("Many Switch games need firmware: the .nca files from your Switch, in {0}/firmware. Playtendoh does not include firmware.",
							files));
				}
			}
			else
			{
				// network shares (6.14): a folder on one can be this side's
				const size_t shares = ps5netfs::shares().size();
				action(Kind::Link, "shares", Tr("Network shares"), shares ? TrP((long long)shares, "{0} share", "{0} shares") : std::string(),
					Tr("Games on a computer or a NAS, over SMB or NFS."),
					Tr("Add a folder shared by a computer or NAS, then choose a games folder on it in the folder browser. Nothing is written to it; saves and installs stay on the PS5. Wired is better than Wi-Fi."));
			}
			if (IsWiiU())
			{
				const bool keys = IsFile(ps5paths::DiscKeys());
				action(Kind::Action, "keys", Tr("Wii U disc keys"), keys ? std::string(Tr("keys.txt found")) : std::string(),
					keys ? Tr("Only encrypted .wud and .wux need them.") : Tr("keys.txt is missing: only encrypted .wud and .wux need it"),
					// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
					TrF("Encrypted .wud and .wux need their disc keys in {0}/keys.txt, one per line. Other formats need none.", ps5paths::CemuRoot()));
				// tr: Settings > Game files: a row that opens Updates and DLC and Mods from USB
				sub("installs", Tr("Updates, DLC and mods"), "", Tr("Install from your drives, and add mods."),
					Tr("Install updates and DLC found on your drives, and copy mods from ps5cemu/mods on a USB drive. Press Triangle on each for more."));
				cemu(action(Kind::Link, "install", Tr("Updates and DLC"), "", Tr("Found on your drives. Install each with one press."),
					Tr("Searches USB drives, extended storage, the games folder and /data for updates, DLC and games (folders with code, content and meta), listed by game. Cross installs one, Square installs all new ones. Triangle chooses another folder.")));
				cemu(action(Kind::Action, "mods", Tr("Mods from USB"), ModsStatus(), Tr("Mods from ps5cemu/mods on your drives."),
					Tr("Copies mods from ps5cemu/mods on a USB drive. Each appears, off, on its game's Graphic packs page. A mod is a graphic pack (a folder with rules.txt), or a folder with content and aoc folders inside a folder named with the game's title ID. You can also add mods from a game's menu. See docs/GUIDE.md on GitHub.")));
				endSub();
			}
			else if (Is3ds())
				action(Kind::Link, "installcia", Tr("Install a CIA file"), "", Tr("A game, an update or DLC."),
					Tr("Installs a CIA file. Updates and DLC join their game; games join the library. Circle cancels."));
			// every system's own folder, each of the 20-odd: where its saves, BIOS files and settings are. An
			// install from before is moved into them as the app starts (app/reorganize.h, main_ps5.cpp); what
			// could not be is offered again here, and putting the first four back, which Go back to an earlier
			// version needs
			{
				struct Place
				{
					std::string name, folder;
					const char* system = nullptr; // one the player can move: reorganize.h's name for it
				};
				const std::string& root = ps5paths::Root();
				std::vector<Place> places{{SideTitle(System::WiiU), ps5paths::CemuRoot(), "cemu"}, {SideTitle(System::N3ds), ps5paths::Azahar(), "azahar"},
					{SideTitle(System::Nds), ps5paths::MelonDs(), "melonds"}, {SideTitle(System::Switch), ps5paths::Eden(), "eden"},
					{SideTitle(System::Switch2), ps5eglantine::DataFolder()}};
				for (size_t i = 0; i < ps5retro::kSystems.size(); i++)
					places.push_back({SideTitle((System)(ps5retro::kFirst + i)), ps5retro::kSystems[i].data});
				std::vector<std::string> pending;
				const ps5reorganize::Request asked = ps5reorganize::Asked(root, pending);
				int own = 0;
				for (const Place& place : places)
					own += !place.system || std::find(m_layout.separated.begin(), m_layout.separated.end(), place.system) != m_layout.separated.end();
				// tr: Settings > Game files: a row opening the list of every system's folder; {0} how many have a folder of their own, {1} how many systems
				sub("systemfolders", Tr("A folder for each system"), TrF("{0} of {1} on their own", own, (int)places.size()),
					Tr("Keep a system's files together with the rest, or in a folder of its own."),
					TrF("Saves, BIOS files and settings for every system are kept together in {0}, unless you give a system its own folder. Moving is quick and nothing is deleted. Playtendoh restarts to do it. Some emulators always use their own folder.",
						root));
				for (const Place& place : places)
				{
					if (!place.system)
					{
						// tr: Settings > Game files: a system whose emulator always keeps a folder of its own
						Row* row = action(Kind::Action, "systemfolder", place.name, place.folder, Tr("Its emulator always keeps its own folder."), "");
						row->dimmed = true;
						continue;
					}
					const bool on = std::find(m_layout.separated.begin(), m_layout.separated.end(), place.system) != m_layout.separated.end();
					const bool moving = asked != ps5reorganize::Request::None && std::find(pending.begin(), pending.end(), place.system) != pending.end();
					Row* row = toggle((std::string("ownfolder-") + place.system).c_str(), place.name, on,
						moving ? std::string(Tr("Moves the next time Playtendoh starts.")) : place.folder,
						Tr("On: this system's files get their own folder. Off: kept with the rest."));
					row->value = on ? Tr("Own folder") : Tr("Together");
				}
				if (!m_layout.blocked.empty())
				{
					// tr: {0} is a file or folder already where one would be moved to
					Row* row = action(Kind::Action, "systemfolder", Tr("Cannot move"), m_layout.blocked, TrF("In the way: {0}", m_layout.blocked), "");
					row->dimmed = true;
				}
				endSub();
			}
		}
		else if (page == "saves" || page == "backups")
		{
			if (page == "saves" && IsWiiU())
			{
				const auto accounts = m_status.coreReady ? ps5emu::ListAccounts() : std::vector<ps5emu::WiiuAccount>{};
				const uint32_t current = m_status.coreReady ? ps5emu::CurrentAccount() : 0;
				std::vector<std::string> names;
				int index = 0;
				std::string currentName;
				for (int i = 0; i < (int)accounts.size(); i++)
				{
					names.push_back(accounts[i].name);
					if (accounts[i].id == current)
					{
						index = i;
						currentName = accounts[i].name;
					}
				}
				if (names.empty())
					names.push_back("-");
				Row* account = cemu(choice("account", Tr("Account games start on"), names, index,
					settings.accountPerUser ? Tr("Set by your PS5 user, because One account for each PS5 user is on.") :
											  Tr("The next game uses these saves."),
					Tr("Each Wii U account has its own saves, as on a Wii U. The next game you start uses this one.")));
				if (settings.accountPerUser)
					account->dimmed = true;
				cemu(toggle("accountperuser", Tr("One account for each PS5 user"), settings.accountPerUser,
					Tr("Whoever starts Playtendoh plays on their own saves."),
					Tr("Each PS5 user gets their own Wii U account, named after them, so everyone keeps their own saves.")));
				// tr: Settings > Accounts and saves: a row that opens New account, Rename and Delete an account
				sub("accounts", Tr("Manage accounts"), TrF("{0} of 12", (int)accounts.size()), Tr("Add, rename or delete accounts."),
					Tr("Add an account, rename the current one, or delete another. Saves always stay in the Wii U's storage."));
				// tr: how many Wii U accounts there are, of the twelve a Wii U has at most
				cemu(action(Kind::Link, "newaccount", Tr("New account"), TrF("{0} of 12", (int)accounts.size()),
					Tr("A new account starts with no saves."), Tr("Up to ten letters. Games then start on it.")));
				cemu(action(Kind::Link, "renameaccount", TrF("Rename {0}", currentName), "", Tr("The account games start on."),
					Tr("The name games show, up to ten letters. Its saves are kept.")));
				// deleting: Left and Right choose which, never the one games start on
				std::vector<std::string> others;
				for (const auto& a : accounts)
					if (a.id != current)
						others.push_back(a.name);
				int& which = m_holdChoice["deleteaccount"];
				which = others.empty() ? 0 : std::clamp(which, 0, (int)others.size() - 1);
				Row* remove = cemu(action(Kind::Hold, "deleteaccount", Tr("Delete an account"),
					others.empty() ? std::string(Tr("Only one")) : others[which] + "  ·  " + Tr("Hold Cross"),
					others.empty() ? Tr("There is only one account, and games start on it.") : Tr("Left and Right choose which. Its saves stay in the Wii U's storage."),
					Tr("Removes the account. Its saves stay in the Wii U's storage and backups. You can't delete the account games start on.")));
				remove->options = others;
				remove->index = which;
				if (others.empty())
					remove->dimmed = true;
				endSub();
			}
			// every side's saves, to a USB drive and back (Playtendoh's Backups)
			if (page == "backups")
			{
				if (m_now - m_backupsAt > 3)
				{
					m_backups = ps5saves::FindBackups();
					m_backupsAt = m_now;
				}
				const ps5saves::Status saves = ps5saves::Current();
				const bool running = saves.state == ps5saves::Status::State::Running;
				if (saves.finished != m_savesFinished)
				{
					// a backup or restore has ended: said once, and what it changed read again
					m_savesFinished = saves.finished;
					m_backupsAt = -100;
					if (saves.state == ps5saves::Status::State::Done)
					{
						// tr: a backup or restore of the saves, finished ({0}: how many files)
						m_savesMessage = TrP(saves.files, "Done: {0} file", "Done: {0} files");
						if (saves.restoring && m_status.coreReady)
							ps5emu::ReloadAccounts();
						// the settings came back too: the launcher holds the old ones, so the app starts again on
						// the restored ones, before anything saves the old over them
						if (saves.restoring && saves.settingsRestored)
						{
							ps5log::Line("[saves] the settings were restored: starting over to use them");
							ps5emu::RestartToLibrary();
						}
					}
					else
						m_savesMessage = saves.message;
				}
				std::string backupValue = m_savesMessage;
				if (running && !saves.restoring)
					// tr: a backup of the saves, as it copies ({0}: a percentage)
					backupValue = TrF("Copying: {0}", Percent(saves.total ? (int)(saves.copied * 100 / saves.total) : 0));
				else if (backupValue.empty() && !m_backups.empty())
					// tr: the newest backup of the saves: when, and on which drive
					backupValue = TrF("Last: {0} · {1}", m_backups.front().when, DriveName(m_backups.front().drive));
				action(Kind::Action, "backup", Tr("Back up to USB"), backupValue, Tr("Every system's saves, save states and Wii U accounts."),
					Tr("Copies every system's saves, save states and Wii U accounts, plus your settings, to PS5CEMU-HAR-saves on the first USB drive, in a dated folder. Games are not copied, so backups stay small."));
				std::vector<std::string> backups;
				for (const auto& backup : m_backups)
					backups.push_back(backup.when + " · " + DriveName(backup.drive));
				int& chosen = m_holdChoice["restore"];
				chosen = backups.empty() ? 0 : std::clamp(chosen, 0, (int)backups.size() - 1);
				std::string restoreValue = backups.empty() ? std::string(Tr("No backups found")) : backups[chosen] + "  ·  " + Tr("Hold Cross");
				if (running && saves.restoring)
					restoreValue = TrF("Copying: {0}", Percent(saves.total ? (int)(saves.copied * 100 / saves.total) : 0));
				Row* restore = action(Kind::Hold, "restore", Tr("Restore from USB"), restoreValue,
					backups.empty() ? Tr("Back up first, or plug in the drive with your backups.") : Tr("Left and Right choose which backup, the newest first."),
					// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
					TrF("Restores the chosen backup's saves and settings. Current saves are moved to {0}/saves-before first, so nothing is lost. Playtendoh then restarts.",
						ps5paths::Root()));
				restore->options = backups;
				restore->index = chosen;
				if (backups.empty())
					restore->dimmed = true;
			}
		}
		else if (page == "online" || page == "accounts")
		{
			// downloading the covers now: the missing ones, or every one again
			// (a libretro side's come by each game file's name, from libretro's thumbnails: every game can have one)
			const bool byName = IsRetro() && !IsPorpoise();
			int missing = 0;
			for (const Game& game : m_games)
				missing += (byName || !game.entry.game.gameId.empty()) && game.boxArt.empty();
			const size_t pending = byName ? ps5boxart::RetroPending() : ps5boxart::Pending();
			const std::string status = pending ? TrP((long long)pending, "Downloading: {0} left", "Downloading: {0} left") :
				!byName && ps5boxart::Answered() == 0 ? std::string(Tr("GameTDB did not answer: is the PS5 online?")) :
				missing				   ? TrP(missing, "{0} game without box art", "{0} games without box art") :
										 std::string(Tr("Every game has box art"));
			if (page == "online")
			{
				// tr: Settings > Online: a row that opens the box art's settings (GameTDB on or off, downloading the covers again)
				sub("boxart", Tr("Box art"), settings.boxArt || pending ? status : std::string(Tr("Off")),
					byName ? Tr("Covers for the library, from libretro's thumbnails.") : Tr("Covers for the library, from GameTDB."),
					Tr("Download covers from GameTDB, or download missing or all covers again. For Switch, choose box art or game icons."));
				// (a Switch game's ID is the game GameTDB has by its name: port/eden/library.h)
				toggle("boxart", Tr("Box art from GameTDB"), settings.boxArt, Tr("Covers for the library, downloaded once."),
					Tr("Downloads each new game's cover from GameTDB. Off: no covers are downloaded."));
				// the Switch side's covers: GameTDB's box art, or each game's own icon, as the Switch's home menu has them
				if (IsSwitch())
					choice("nx-covers", Tr("Switch covers"), {Tr("Box art"), Tr("Game icons")}, hand.nxCovers, Tr("GameTDB's box art, or each game's own icon."),
						Tr("Box art comes from GameTDB. Game icons come from the games, as on the Switch home menu. A Game.png or Game.jpg next to a game is always used."));
				action(Kind::Action, "boxartmissing", Tr("Download missing box art"), status, Tr("Tries again for every game without a cover."),
					Tr("Tries again for every game on this system without a cover. Covers download in the background."));
				action(Kind::Hold, "boxartall", Tr("Download all box art again"), m_message.empty() ? Tr("Hold Cross") : m_message,
					Tr("Downloads every cover for this system again."),
					Tr("Downloads every cover on this system again. Useful if a cover is wrong or broken."));
				endSub();
				// every game's cheats (app/cheats.h): the app's, on the sides whose emulators have cheats
				if (ps5cheats::Supported(SideIndex()))
					rows.push_back(CheatsAutoRow(settings));
				// Artic Base's page (pages.cpp): a game played from a 3DS on the network
				if (Is3ds())
					action(Kind::Link, "artic", Tr("Artic Base"), settings.n3ds.articAddress, Tr("Play from your 3DS, or copy its system files."),
						Tr("Artic Base plays a game from your 3DS over the network; saves stay on the 3DS. Artic Setup copies your 3DS's system files, for the Home Menu and games that need them."));
				// ProsperoEden's rooms (port/eden/rooms.h), behind a row; the public list downloaded once, as their page
				// first shows, and again by Cross on its row
				if (IsSwitch())
				{
					const bool joined = !hand.nxRoomHost.empty();
					const std::string about = Tr("A room lets Switch games play local wireless together online. The room chosen here is joined when a Switch game starts; then choose Local Wireless (or local play) in the game.");
					// tr: Settings > Online on the Switch side: a row that opens the rooms for the games' local wireless play
					sub("rooms", Tr("Multiplayer"), joined ? hand.nxRoomName : std::string(Tr("None: games play offline")),
						Tr("Play local wireless games online."), about);
					if (m_subPage == "rooms" && !ps5rooms::Asked())
						ps5rooms::Refresh();
					const ps5rooms::Status rooms = ps5rooms::Get();
					Row* room = action(Kind::Action, "nx-room", Tr("Room"), joined ? hand.nxRoomName : std::string(Tr("None: games play offline")),
						joined ? Tr("Joined when a Switch game starts. Cross goes back to offline.") : Tr("Choose a public room below, or a private one."),
						about);
					room->dimmed = !joined;
					action(Kind::Action, "nx-nickname", Tr("Nickname"), ps5eden::Nickname(hand), Tr("Your name in the room."),
						Tr("What others in the room see. 4 to 20 letters, digits, spaces, dots, dashes or underscores."));
					const bool privateRoom = joined && hand.nxRoomName == hand.nxRoomHost;
					action(Kind::Action, "nx-private", Tr("Private room"),
						privateRoom ? fmt::format("{}:{}", hand.nxRoomHost, hand.nxRoomPort) : std::string(Tr("Cross to enter one")),
						Tr("Join a room by address, port and password."),
						Tr("For a room someone shared with you: its address (name or IPv4), UDP port (usually 24872) and password, if any."));
					const std::string listed = rooms.busy ? std::string(Tr("Downloading…")) :
						!rooms.error.empty()			   ? rooms.error :
						rooms.rooms.empty()				   ? std::string(Tr("No public rooms right now")) :
															 TrP((long long)rooms.rooms.size(), "{0} room", "{0} rooms");
					action(Kind::Action, "nx-refresh", Tr("Public rooms"), listed, Tr("Cross downloads the list again."),
						Tr("Featured rooms first, then the busiest. Rooms for another version can't be joined.") + std::string("\n\n") + about);
					// the rooms themselves, ProsperoEden's own first (the most a page lists)
					for (size_t i = 0; i < rooms.rooms.size() && i < 80; i++)
					{
						const ps5rooms::Room& r = rooms.rooms[i];
						std::string value = fmt::format("{}/{}", r.players, r.capacity);
						if (r.password)
							value += std::string(" · ") + Tr("Password");
						if (r.featured)
							// tr: one of ProsperoEden's own rooms, which its list puts first
							value = std::string(Tr("Featured")) + " · " + value;
						if (!r.compatible)
							value = Tr("Another version");
						const bool chosen = joined && hand.nxRoomHost == r.address && hand.nxRoomPort == r.port;
						Row* row = action(Kind::Action, "nx-public", chosen ? TrF("{0} (chosen)", r.name) : r.name, value,
							r.game.empty() ? std::string(Tr("Any game")) : r.game, about);
						row->index = (int)i;
						row->dimmed = !r.compatible;
					}
					endSub();
				}
				if (IsWiiU())
					action(Kind::Action, "packs", Tr("Community graphic packs"), PacksStatus(), Tr("Cross checks GitHub for newer ones."),
						Tr("Community graphic packs for Wii U games, downloaded from GitHub. They download in the background the first time the Wii U opens."));
			}
			// RetroAchievements: one account for every console with sets (the DS, the libretro sides, and
			// Porpoise X's GameCube and Wii, which signs in with it), on every side's Accounts and updates
			if (page == "accounts")
			{
				const ps5cheevos::Account account = ps5cheevos::Get();
				const char* about = Tr("Earn achievements in DS, GameCube, Wii, N64, SNES, NES, Game Boy, Virtual Boy and Pokémon mini games. "
									   "Only a sign-in token is kept on the PS5, never your password. Save states, cheats and speed still work.");
				if (account.signedIn)
					// tr: the RetroAchievements account signed in: its name ({1}) and softcore points ({0})
					action(Kind::Hold, "cheevos", Tr("RetroAchievements"),
						TrP((long long)account.points, "{1} · {0} point", "{1} · {0} points", account.user) + "  ·  " + Tr("Hold Cross to sign out"),
						Tr("Signed in on every console that has achievements."), about);
				else
					action(Kind::Action, "cheevos", Tr("RetroAchievements"),
						account.busy ? std::string(Tr("Signing in…")) : !account.message.empty() ? account.message : std::string(Tr("Cross to sign in")),
						Tr("Sign in once for every console that has achievements."), about);
				Row* notices = toggle("cheevosnotices", Tr("Unlock notices"), account.notices, Tr("Show unlocks as PS5 notifications."),
					Tr("Shows unlocks, and your progress when a game starts, as PS5 notifications. Off: see them in the in-game menu."));
				if (!account.signedIn)
					notices->dimmed = true;
				// reports to the project (app/reports.h): the first start's question, changed here
				if (ps5reports::Available())
					toggle("reports", Tr("Send reports"), settings.ui.reports, Tr("How games run, with nothing about you."),
						// tr: {0} is the address of the page that lists every field a report has
						TrF("When a game ends, a short report is sent: the game, how it ended, its settings, the PS5's firmware and model, and the end of the log with addresses and folder names removed. Never your PSN name, serial, addresses, accounts or saves. Nothing is sent while you play. Off deletes reports waiting to be sent. Every field: {0}",
							ps5reports::kExplained));
				// the wide picture shown as a game starts (app/boxart.h): the app's own key, or the player's
				action(Kind::Action, "steamgriddb", Tr("SteamGridDB key"),
					ps5boxart::PlayerSteamGridKey() ? Tr("Yours") : ps5boxart::HasSteamGridKey() ? Tr("Built in") : Tr("None"),
					Tr("Wide art shown as a game starts. Your own key from steamgriddb.com is used first; leave it empty to remove it."),
					Tr("Wide art shown as a game starts. Your own key from steamgriddb.com is used first; leave it empty to remove it."));
				// (the cheats session: downloading cheats for every system's games goes here, a row of its own)
				action(Kind::Action, "appupdate", Tr("Playtendoh updates"), AppUpdateStatus(), Tr("Cross checks GitHub for a newer version."),
					Tr("Playtendoh checks for updates each time it starts. Cross checks again or installs the new version, then restarts. Your games, saves and settings are kept."));
				// the release the last update replaced, kept until the next (ps5update::PreviousVersion)
				if (const std::string previous = ps5update::PreviousVersion(); !previous.empty())
				{
					// tr: {0} is the version the last update replaced, as in 5.0.0
					Row* goback = action(Kind::Hold, "goback", TrF("Go back to {0}", ps5update::Readable(previous)), m_message.empty() ? Tr("Hold Cross") : m_message,
						Tr("The version before the last update."),
						Tr("Hold Cross to go back to the version before the last update. Playtendoh restarts. Your games, saves and settings are kept."));
					// a version from before a folder for each system looks for the files where they were (app/reorganize.h)
					if (!m_layout.separated.empty())
					{
						goback->dimmed = true;
						goback->description = Tr("Put each system's files back first: Settings > Game files > A folder for each system.");
					}
				}
			}
		}
		else if (page == "launcher")
		{
			{
				// the themes: the built-in ones, then your own (themes/*.json)
				std::vector<std::string> names;
				int index = 0;
				for (int i = 0; i < (int)m_themes.size(); i++)
				{
					const ui::Theme& theme = m_themes[i];
					names.push_back(theme.id.rfind("user:", 0) == 0 ? theme.name : std::string(Tr(theme.name.c_str())));
					if (theme.id == ui::CurrentTheme().id) // (an id from before 5.1 is the one it now names)
						index = i;
				}
				const ui::Theme& current = m_themes.empty() ? ui::BuiltInThemes().front() : m_themes[std::min(index, (int)m_themes.size() - 1)];
				choice("theme", Tr("Theme"), names, index,
					current.id.rfind("user:", 0) == 0 ? current.description : std::string(Tr(current.description.c_str())),
					TrF("The launcher's colours, corners and background. Make your own as a JSON file in {0}/themes; the text below this page says how. Games and in-game menus don't change.",
						ps5paths::Root()));
			}
			// EmulationStation the third once the app has it: its download is the row below, never a choice
			// here, which changes as focus passes over it (app/esde_get.h)
			const bool esde = ps5esde::Available();
			std::vector<std::string> layouts{Tr("Classic"), Tr("Orbit")};
			if (esde)
				layouts.push_back(Tr("EmulationStation"));
			choice("launcher", Tr("Layout"), layouts, settings.ui.emulationStation && esde ? 2 : std::clamp(settings.ui.layout, 0, 1),
				Tr("How the Library is laid out."),
				Tr("Classic shows one console at a time, with Recently played on top. Orbit is a dial for picking a console, with recent games in the centre. EmulationStation (ES-DE) is a separate launcher: download it on the row below to choose it here. Your games, settings and saves stay the same."));
			{
				// ES-DE in the app or not: its download, how it goes, and taking it out again
				using State = ps5esdeget::Status::State;
				const ps5esdeget::Status status = ps5esdeget::GetStatus();
				const std::string about = TrF("EmulationStation (ES-DE) shows the same games with its own themes. It's a separate download of about {0} MB and stays through updates. Games still play here with the same settings, saves and in-game menu. Hold L1 while Playtendoh starts to use this launcher instead.", ps5esdeget::kAboutBytes >> 20);
				std::string value;
				switch (status.state)
				{
				case State::Finding: value = Tr("Asking GitHub…"); break;
				case State::Downloading:
					value = status.total ? TrF("Downloading: {0}", Percent((int)(status.received * 100 / status.total))) : std::string(Tr("Downloading…"));
					break;
				case State::Verifying: value = Tr("Checking the download…"); break;
				case State::Installing: value = Tr("Installing…"); break;
				case State::Failed: value = status.message; break;
				default: value = esde ? std::string(Tr("Hold Cross to remove")) : TrF("Hold Cross to download · {0} MB", ps5esdeget::kAboutBytes >> 20);
				}
				if (status.state == State::Failed)
					value += "  ·  " + std::string(esde ? Tr("Hold Cross to remove") : Tr("Hold Cross to try again"));
				// held, so passing over it or a press by mistake starts nothing; Cross stops a download under way
				action(ps5esdeget::Busy() ? Kind::Action : Kind::Hold, "esde-get", Tr("EmulationStation"), value,
					esde ? Tr("Installed. Choose it as the Layout above.") : Tr("A separate launcher, as a download."), about);
			}
			// the sides the bar offers: any mix, at least one, in its order
			std::string shown;
			for (System side : kBarOrder)
				if (SideShown(side))
					shown += (shown.empty() ? "" : " · ") + std::string(SideTitle(side));
			// tr: Settings > Launcher: a row that opens which sides the bar shows (Wii U, Switch, DS, 3DS)
			sub("sides", Tr("Systems"), shown, Tr("Which systems the bar shows."),
				Tr("Choose which systems the bar shows. Hidden systems keep their games and settings. At least one stays on."));
			for (int index = ps5retro::kFirst; index < kSides; index++)
			{
				const auto extra = (System)index;
				toggle((std::string("side-") + ps5launcher::SideName(extra)).c_str(), SideTitle(extra), SideShown(extra), Tr("This system's games, in the bar."), Tr("Show this system in the bar. Hidden systems keep their games and settings. At least one stays on."));
			}
			toggle("side-wiiu", Tr("Wii U"), SideShown(System::WiiU), Tr("Wii U games, in the bar."),
				Tr("Show the Wii U in the bar. Hidden systems keep their games and settings. At least one stays on."));
			// in the bar's order: Wii U, Switch, DS, 3DS
			toggle("side-switch", Tr("Switch"), SideShown(System::Switch), Tr("Switch games, in the bar."),
				Tr("Show the Switch in the bar. Hidden systems keep their games and settings. At least one stays on."));
			toggle("side-ds", Tr("DS"), SideShown(System::Nds), Tr("DS games, in the bar."),
				Tr("Show the DS in the bar. Hidden systems keep their games and settings. At least one stays on."));
			toggle("side-3ds", Tr("3DS"), SideShown(System::N3ds), Tr("3DS games, in the bar."),
				Tr("Show the 3DS in the bar. Hidden systems keep their games and settings. At least one stays on."));
			endSub();
			toggle("boxart3d", Tr("3D box art"), settings.ui.boxArt3d, Tr("Covers as boxes, with a spine."),
				Tr("Each cover drawn as its box, turned a little, with its spine. Off: flat covers."));
			toggle("pictures", Tr("Game pictures behind menus"), settings.ui.gamePictures, Tr("The selected game's cover, blurred, behind the menus."),
				Tr("The selected game's picture fills the background, blurred and dimmed. Off: just the game's colours."));
			toggle("filters", Tr("Library filters"), settings.ui.libraryFilters, Tr("A row of filters above the list."),
				Tr("Filters above the list: All, Recently added (last 30 days), Favourites, and for Wii U, Graphic packs on. Off: just Sort and Search."));
			// tr: Settings > Launcher: a game's badge, Playable, Issues or Not playable, from players' reports
			toggle("compatbadges", Tr("How games run"), settings.ui.compatBadges, Tr("Playable, Issues or Not playable on each game."),
				Tr("A badge on each game that has been reported: Playable, Issues or Not playable, from the compatibility list and players' reports, updated when the app starts. Off: no badges."));
			int hold = 2;
			for (int i = 0; i < 6; i++)
				if (kHolds[i] <= settings.ui.holdMs)
					hold = i;
			const std::vector<std::string> holds = Options({"0.4 s", "0.6 s", "0.8 s", "1.0 s", "1.2 s", "1.5 s"});
			// what is on, and the hold when it is not the 0.8 s it starts at
			std::string easier;
			for (const auto& [on, name] : {std::pair{settings.ui.largerText, Tr("Larger text")}, std::pair{settings.ui.highContrast, Tr("High contrast")},
					 std::pair{settings.ui.reduceMotion, Tr("Reduce motion")}})
				if (on)
					easier += (easier.empty() ? "" : " · ") + std::string(name);
			if (hold != 2)
				easier += (easier.empty() ? "" : " · ") + holds[hold];
			// tr: Settings > Launcher: a row that opens Larger text, High contrast, Reduce motion and Hold to confirm
			sub("accessibility", Tr("Accessibility"), easier.empty() ? std::string(Tr("Default")) : easier,
				Tr("Larger text, high contrast, reduced motion and hold to confirm."),
				Tr("Text size, contrast, motion, and how long to hold Cross to confirm. Press Triangle on each for more."));
			toggle("largertext", Tr("Larger text"), settings.ui.largerText, Tr("Makes all text a step larger."),
				Tr("Makes every text size one step larger."));
			toggle("highcontrast", Tr("High contrast"), settings.ui.highContrast, Tr("Opaque panels, full-strength text, a thicker ring."),
				Tr("Opaque panels instead of glass, all text at full strength, a thicker focus ring and no grain or pictures behind the menus."));
			toggle("reducemotion", Tr("Reduce motion"), settings.ui.reduceMotion, Tr("Fades instead of motion."),
				Tr("Animations become short fades, and the background stays still."));
			choice("holdms", Tr("Hold to confirm"), holds, hold, Tr("How long to hold Cross for actions that can't be undone."),
				Tr("Actions like clearing caches or resetting controls need Cross held to confirm. This sets how long."), Kind::Stepper);
			endSub();
			// the launcher's music and menu sounds, the same on every side
			std::vector<std::string> musicNames;
			const std::vector<std::string> musicValues = MusicValues(&musicNames);
			const auto chosen = std::find(musicValues.begin(), musicValues.end(), settings.music);
			// a file no longer there plays the launcher's own piece: the row says so too
			const int music = chosen == musicValues.end() ? 0 : (int)(chosen - musicValues.begin());
			// tr: Settings > Launcher: a row that opens the launcher's music and menu sounds
			sub("launchersound", Tr("Launcher sound"), musicNames[music], Tr("The menus' music and sounds."),
				Tr("Menu music, its volume, and menu sounds. The same for every system."));
			choice("music", Tr("Launcher music"), musicNames, music, Tr("Music that plays in the menus."),
				TrF("Built-in music, your own, or none. For your own, put MP3 or WAV files in {0}.",
					ps5sound::MusicFolder()));
			slider("musicvolume", Tr("Music volume"), settings.musicVolume, 100, Tr("Menu music volume."),
				Tr("How loud the launcher's music is. Left and Right change it by 10%."));
			toggle("menusounds", Tr("Menu sounds"), settings.menuSounds, Tr("The sounds of moving and choosing."),
				Tr("The launcher's sounds as you move, choose and go back."));
			endSub();
			action(Kind::Link, "makeityours", Tr("Make it yours"), "", Tr("Run first-time setup again."),
				Tr("Shows the first-time setup again: language, systems, theme, buttons, music, vibration and game pictures."));
		}
		else if (page == "diagnostics")
		{
			action(Kind::Link, "setupcheck", Tr("Setup check"), "", Tr("Checks storage, performance, games, keys and box art."),
				Tr("Each check shows its status and what to do. It also appears on first start, and when a check fails."));
			action(Kind::Action, "copylogs", Tr("Copy logs to USB"), m_diagnosticsDone[0], Tr("Copy logs to a USB drive for a bug report."),
				Tr("Copies the last five sessions' logs and the settings into a dated folder on a USB drive, to attach to a report."));
			// the 3DS's and the Wii U's games unpacked from archives (app/game_archives.h): one cache for both, on
			// every side's Diagnostics
			{
				if (m_now - m_unpackedAt > 3)
				{
					m_unpackedBytes = ps5archive::UnpackedBytes();
					m_unpackedAt = m_now;
				}
				// tr: Settings > Diagnostics: the copies of games unpacked from their archives (.zip, .7z...), kept for their next start
				action(Kind::Hold, "clearunpacked", Tr("Unpacked games"),
					!m_unpackedDone.empty() ? m_unpackedDone :
					m_unpackedBytes			? SizeWords(m_unpackedBytes) + "  ·  " + Tr("Hold Cross") :
											  std::string(Tr("None to delete")),
					Tr("Games unpacked from archives, kept for faster starts."),
					// tr: {0} is the folder they are kept in
					TrF("Deletes unpacked copies of archived games in {0}. They unpack again next time. Old copies are removed automatically when space runs low.",
						ps5archive::UnpackedRoot()));
			}
		}
		// the rows of the pages that went into others (the owner's, 2026-10-27: fewer pages down the left), each
		// after the rows of the page it is on: the sound on System, after the emulated console's; the Wii U's
		// USB portals there too; and the remote touch screen on the 3DS's and the DS's Controls, the Wii U's
		// System
		if (page == "system")
		{
			own(slider("volume", IsWiiU() ? Tr("Wii U games' volume") : Is3ds() ? Tr("3DS games' volume") : IsSwitch() ? Tr("Switch games' volume") : Tr("DS games' volume"),
					IsWiiU() ? settings.volume : hand.volume, 100, Tr("The games' sound."), Tr("Game volume for this system. Left and Right change it by 10%.")),
				"volume");
			if (IsWiiU())
				toggle("gamepadspeaker", Tr("GamePad speaker"), settings.gamePadSpeaker, Tr("GamePad sound from the DualSense speaker."),
					Tr("Plays GamePad speaker sounds from player 1's DualSense. Many games send all their sound there too, so it's off by default. Applies to the next game."));
		}
		// the shader caches, the Wii U's and the 3DS's emulators' own (Diagnostics is Playtendoh's)
		if (page == "files")
		{
			endSub();
			if (IsWiiU())
				action(Kind::Hold, "clearcaches", Tr("Clear shader caches"), m_diagnosticsDone[1].empty() ? Tr("Hold Cross") : m_diagnosticsDone[1],
					Tr("Try this if a game crashes."), Tr("Deletes the Wii U shader caches. Games rebuild them, so the first few minutes may stutter."));
			else if (Is3ds())
				action(Kind::Hold, "clearcaches", Tr("Clear shader caches"), m_diagnosticsDone[2].empty() ? Tr("Hold Cross") : m_diagnosticsDone[2],
					Tr("Try this if a game crashes."),
					Tr("Deletes the 3DS shader caches. Games rebuild them, so the first few minutes may stutter."));
		}
		if (page == "system" && IsWiiU())
		{
			int plugged = 0;
			for (ps5usb::Device device : ps5usb::kDevices)
				plugged += m_status.coreReady && ps5usb::Enabled(device);
			// tr: Settings > System on the Wii U side: a row that opens Cemu's emulated portals (Skylanders, Disney Infinity, LEGO Dimensions)
			sub("usb", Tr("USB devices"), plugged ? TrP(plugged, "{0} on", "{0} on") : std::string(Tr("Off")), Tr("Emulated toy portals."),
				PanelText("usb"));
			for (ps5usb::Device device : ps5usb::kDevices)
			{
				Row* row = toggle("usb", ps5usb::Name(device), m_status.coreReady && ps5usb::Enabled(device),
					device == ps5usb::Device::Skylanders ? Tr("For the Skylanders games.") :
					device == ps5usb::Device::Infinity	 ? Tr("For Disney Infinity 3.0.") :
														   Tr("For LEGO Dimensions."),
					TrF("An emulated portal, connected when a game starts. Place figures from the in-game menu under USB devices; dumps go in {0}. Real portals are not supported.",
						ps5usb::Folder(device)));
				row->index = (int)device;
				cemu(row);
			}
			endSub();
		}
		// the remote touch screen is this program's: a Switch game runs in Eden's
		if ((page == "system" && IsWiiU()) || (page == "controls" && (Is3ds() || IsDs())))
		{
			const char* screen = IsWiiU() ? Tr("The GamePad's screen") : Is3ds() ? Tr("The bottom screen") : Tr("The touch screen");
			const std::string about = TrF("Shows {0} in a browser on a phone, tablet or computer on your network, with touch. Scan the code or open the address. Buttons and sound stay on the PS5.\nWhile a browser is open, games may run a little slower. Anyone on your network can open it, so turn it off on shared networks.\nApplies to every system, and is also in the in-game menu.",
				screen);
			// tr: a row that opens the remote touch screen's page: the game's second screen in a phone's browser, and its code to scan
			sub("remote", Tr("Remote touch screen"), settings.remoteScreen ? Tr("On") : Tr("Off"), TrF("Show {0} on a phone or tablet, with touch.", screen),
				about);
			toggle("remote", Tr("Remote touch screen"), settings.remoteScreen, TrF("Show {0} on a phone or tablet, with touch.", screen), about);
			endSub();
		}
		(void)side;
		return rows;
	}

	std::vector<Row> Shell::SettingRows(const std::string& page)
	{
		if (InGameSettings() && page == "cheats" && ps5cheats::Supported(SideIndex()))
			return CheatRows();
		if (IsSwitch() && page == "nxcheats")
		{
			// Get cheats first (a game with a title ID), then Eden's list
			CheatsRefresh();
			std::vector<Row> rows = SwitchExtrasRows(page);
			if (InGameSettings() && m_gameTitle && !ps5eden::IsMadeId(m_gameTitle))
				rows.insert(rows.begin(), CheatsGetRow());
			return rows;
		}
		if (IsSwitch() && page == "nxmods")
			return SwitchExtrasRows(page);
		// the Switch's rows from Eden's own files (switch_extras.cpp), behind a row of the page they are on: its
		// ReShade looks behind Graphics' Picture effects, after FSR sharpening and anti-aliasing; its players
		// behind a row of their own, first on Players and saves
		auto withExtras = [&](std::vector<Row> rows) {
			if (!IsSwitch() || (page != "graphics" && (page != "saves" || InGameSettings())))
				return rows;
			const std::string group = page == "graphics" ? "nxpost" : "nxprofiles";
			std::vector<Row> added = SwitchExtrasRows(group);
			std::string look;
			for (Row& row : added)
			{
				row.group = group;
				// a game's own look is its own (game settings' chip on the row it is behind)
				row.fromSide = row.id != "nxextra:preset" || !(InGameSettings() && m_switchExtras.ownPreset);
				// (not Default, the side's, nor Off)
				if (row.id == "nxextra:preset" && row.index > 0 && row.value != Tr("Off"))
					look = row.value;
			}
			if (group == "nxpost")
			{
				// the look chosen, then the anti-aliasing
				for (Row& row : rows)
					if (row.opens == group && !look.empty())
						row.value = row.value == Tr("Off") ? look : look + " · " + row.value;
			}
			else
			{
				const auto& view = m_switchExtras;
				Row players;
				players.kind = Row::Kind::Link;
				players.id = "sub";
				players.opens = group;
				// tr: the Switch side's players, the people (Eden's user profiles), each with saves of their own; not a number of players
				players.label = TrC("switch", "Players");
				players.value = view.error.empty() && view.profile >= 0 && view.profile < (int)view.profiles.size() ? view.profiles[view.profile].name : "";
				players.description = Tr("Players, each with their own saves.");
				players.help = Tr("Remembered for this PS5 user. Each player has their own saves.");
				rows.insert(rows.begin(), std::move(players));
			}
			rows.insert(rows.end(), std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
			return rows;
		};
		if (!InGameSettings())
			return SubPageRows(withExtras(SideRows(page, m_settings)));
		if (page == "gamesaves" && !IsRetro())
			return GameSaveRows();
		// a game's: its rows only, each its own value or the side's (Default), and the › rows they are behind
		const std::string side = ps5launcher::SideName(m_side);
		const auto found = m_settings.games.find(ps5settings::GameKey(side, m_gameTitle));
		std::vector<Row> rows;
		for (Row& row : SideRows(page, m_gameView))
		{
			if (row.setting.empty() && row.opens.empty())
				continue;
			// (Porpoise's: whether its own file has it, PorpoiseRows says; a core option's, the game's own options)
			if (row.setting.starts_with("core:"))
			{
				const auto options = m_settings.gameCoreOptions.find(ps5settings::GameKey(side, m_gameTitle));
				row.fromSide = options == m_settings.gameCoreOptions.end() || !options->second.count(row.setting.substr(5));
			}
			else if (!IsPorpoise())
				row.fromSide = found == m_settings.games.end() || !found->second.count(row.setting);
			rows.push_back(std::move(row));
		}
		rows = SubPageRows(withExtras(std::move(rows)));
		if (!rows.empty() && m_subPage.empty())
		{
			Row reset;
			reset.kind = Row::Kind::Hold;
			reset.id = "resetgame";
			reset.label = Tr("Back to defaults");
			reset.value = m_message.empty() ? Tr("Hold Cross") : m_message;
			reset.description = Tr("This game goes back to the system's settings.");
			reset.help = Tr("Clears this game's own settings so it uses the system's settings again.");
			rows.push_back(reset);
		}
		return rows;
	}

	// Settings > Emulator on a side whose games a core in the app plays: which emulator (where the side has
	// two), then each of its core's options as a choice, those the core groups behind a › row for each
	// group, and a way back to the core's own values. A value the player picks is kept for the side
	// (ps5settings::N3ds::coreOptions) and given to the core at every game's start (libretro/game.cpp).
	std::vector<Row> Shell::CoreOptionRows(const ps5settings::Launcher& settings)
	{
		std::vector<Row> rows;
		const int side = SideIndex();
		const ps5settings::N3ds& own = settings.retro[ps5retro::Slot(side)];
		if (const ps5retro::Alternative* other = ps5retro::AlternativeOf(side); other && !InGameSettings())
		{
			Row runtime; runtime.id = "retro-backend"; runtime.kind = Row::Kind::Segmented; runtime.label = Tr("Emulator");
			runtime.options = {ps5libretro::Plays(side) ? std::string(ps5libretro::CoreTitle(side)) : std::string(other->first), other->backend.name};
			runtime.index = std::clamp(own.retroBackend, 0, 1); runtime.value = runtime.options[runtime.index];
			runtime.description = Tr("Which emulator plays this system's games, from the next game. Each keeps its own settings and saves.");
			rows.push_back(runtime);
		}
		const std::vector<ps5libretro::CoreOption>& options = ps5libretro::CoreOptions(side);
		if (options.empty() || own.retroBackend != 0)
		{
			if (!ps5libretro::Plays(side) || own.retroBackend != 0)
			{
				Row info; info.id = "retro-library"; info.kind = Row::Kind::Action; info.label = Tr("Open emulator settings and saves");
				info.description = Tr("Opens this emulator's own menus for its settings, controls and saves. Quit them to come back here.");
				rows.push_back(info);
			}
			return rows;
		}
		// one › row a group, in the order the core gives its first option; the rest on the page itself
		std::vector<std::string> groups;
		for (const auto& option : options)
			if (!option.category.empty() && std::find(groups.begin(), groups.end(), option.category) == groups.end())
				groups.push_back(option.category);
		auto add = [&](const ps5libretro::CoreOption& option, const std::string& group) {
			Row row;
			row.kind = Row::Kind::Choice;
			row.id = "core:" + option.key;
			row.label = option.label;
			row.group = group;
			if (InGameSettings())
				row.setting = row.id; // a game can have its own (ps5settings::Launcher::gameCoreOptions)
			const auto chosen = own.coreOptions.find(option.key);
			const std::string& value = chosen != own.coreOptions.end() ? chosen->second : option.defaultValue;
			for (size_t i = 0; i < option.values.size(); i++)
			{
				row.options.push_back(option.values[i].second);
				if (option.values[i].first == value)
					row.index = (int)i;
			}
			row.value = row.options[std::clamp(row.index, 0, (int)row.options.size() - 1)];
			// the core's words for it, its first sentence under the row and all of it on Triangle
			const size_t stop = option.info.find(". ");
			row.description = stop == std::string::npos ? option.info : option.info.substr(0, stop + 1);
			row.help = option.info;
			rows.push_back(std::move(row));
		};
		for (const auto& option : options)
			if (option.category.empty())
				add(option, "");
		for (size_t g = 0; g < groups.size(); g++)
		{
			int count = 0, changed = 0;
			for (const auto& option : options)
				if (option.category == groups[g])
				{
					count++;
					changed += own.coreOptions.count(option.key) ? 1 : 0;
				}
			Row link;
			link.kind = Row::Kind::Link;
			link.id = "sub";
			link.label = groups[g];
			link.opens = "core-group-" + std::to_string(g);
			// tr: a group of an emulator's options in Settings: how many it holds, and how many are not the emulator's own values
			link.value = changed ? TrP(changed, "{0} changed", "{0} changed") : TrP(count, "{0} option", "{0} options");
			rows.push_back(link);
			for (const auto& option : options)
				if (option.category == groups[g])
					add(option, link.opens);
		}
		Row reset;
		reset.kind = Row::Kind::Hold;
		reset.id = "core-reset";
		reset.label = Tr("Back to the emulator's own values");
		reset.value = own.coreOptions.empty() ? Tr("Nothing changed") : Tr("Hold Cross");
		reset.dimmed = own.coreOptions.empty();
		reset.description = TrF("Resets every option above to {0}'s default, for every game on this system.", ps5libretro::CoreTitle(side));
		rows.push_back(reset);
		return rows;
	}

	std::vector<Row> Shell::SubPageRows(const std::vector<Row>& rows) const
	{
		std::vector<Row> shown;
		for (const Row& row : rows)
		{
			if (row.group != m_subPage)
				continue;
			if (!row.opens.empty())
			{
				// a › row with nothing behind it goes (a game's settings, which have none of its rows); in a
				// game's, it says whether the game has its own of any
				bool any = false, fromSide = true;
				for (const Row& in : rows)
					if (in.group == row.opens)
					{
						any = true;
						fromSide = fromSide && in.fromSide;
					}
				if (!any)
					continue;
				shown.push_back(row);
				shown.back().fromSide = fromSide;
				continue;
			}
			shown.push_back(row);
		}
		return shown;
	}

	void Shell::OpenSubPage(const Row& row)
	{
		// a › row on a › page: Back comes to this one, not all the way out
		if (!m_subPage.empty())
			m_subParents.push_back({m_subPage, m_subTitle, m_subLine, m_subFrom});
		m_subPage = row.opens;
		m_subFrom = m_settingRow;
		m_subTitle = row.label;
		m_subLine = row.description;
		m_settingRow = 0;
		m_message.clear();
		m_feedback.Play(ui::Cue::Select);
	}

	void Shell::CloseSubPage()
	{
		m_subPage.clear();
		m_settingRow = m_subFrom;
		if (!m_subParents.empty())
		{
			m_subPage = m_subParents.back().page;
			m_subTitle = m_subParents.back().title;
			m_subLine = m_subParents.back().line;
			m_subFrom = m_subParents.back().from;
			m_subParents.pop_back();
		}
		m_message.clear();
		m_feedback.Play(ui::Cue::Back);
	}

	std::string Shell::SaveLabel(const ps5saves::GameSave& save) const
	{
		using Kind = ps5saves::GameSave::Kind;
		switch (save.kind)
		{
		case Kind::Account:
			if (m_status.coreReady)
				for (const auto& account : ps5emu::ListAccounts())
					if (account.id == save.account)
						return account.name;
			// tr: a Wii U account by its number, when its name cannot be read
			return TrF("Account {0}", fmt::format("{:08x}", save.account));
		// tr: a Wii U game's save that all its accounts share
		case Kind::Common: return Tr("Every account's");
		case Kind::Data: return Tr("Save data");
		// tr: a 3DS game's extra data (its extdata), which some games keep beside their saves
		case Kind::ExtData: return Tr("Extra data");
		case Kind::States: return TrP((int)save.paths.size(), "{0} save state", "{0} save states");
		case Kind::Slot2: return Tr("The GBA cartridge's save");
		}
		return {};
	}

	// A game's saves (6.15): a row for each, held Cross to move it aside; this game's alone backed up
	// and restored
	std::vector<Row> Shell::GameSaveRows()
	{
		using Kind = Row::Kind;
		if (m_now - m_gameSavesAt > 3)
		{
			m_gameSaves = ps5saves::FindGameSaves(m_gameRef);
			m_gameBackups = ps5saves::FindGameBackups(m_gameRef);
			m_switchImports.clear();
			if (IsSwitch() && !ps5eden::IsMadeId(m_gameTitle))
			{
				// each of standalone ProsperoEden's players with a save of the game, on this PS5 (#104); never
				// this app's own Eden folder
				if (ps5eden::DataFolder() + "/user" != ps5eden::transfer::kStandaloneUser)
				{
					std::string why;
					for (const auto& choice : ps5eden::transfer::Choices(ps5eden::transfer::kStandaloneUser, m_gameTitle, why))
						m_switchImports.push_back({choice.folder.empty() ? ps5eden::transfer::kStandaloneUser : choice.folder,
							"ProsperoEden · " + choice.label, choice.accountSaveId});
				}
				for (int drive = 0; drive < 8; drive++)
					for (const char* name : {"PS5-ECAM-imports", "Ryujinx"})
					{
						const std::string folder = fmt::format("/mnt/usb{}/{}", drive, name);
						std::string why;
						for (const auto& choice : ps5eden::transfer::Choices(folder, m_gameTitle, why))
							m_switchImports.push_back({folder, fmt::format("USB {} · {} · {}", drive, name, choice.label), choice.accountSaveId});
					}
			}
			m_gameSavesAt = m_now;
		}
		const ps5saves::Status status = ps5saves::Current();
		const bool running = status.state == ps5saves::Status::State::Running;
		if (status.finished != m_savesFinished)
		{
			m_savesFinished = status.finished;
			m_gameSavesAt = -100;
			m_savesMessage = status.state == ps5saves::Status::State::Done ?
				(status.message.empty() ? TrP(status.files, "Done: {0} file", "Done: {0} files") : status.message) : status.message;
		}
		std::vector<Row> rows;
		for (size_t i = 0; i < m_gameSaves.size(); i++)
		{
			const ps5saves::GameSave& save = m_gameSaves[i];
			Row row;
			row.kind = Kind::Hold;
			row.id = fmt::format("gamesave:{}", i);
			row.label = SaveLabel(save);
			std::string when;
			const std::time_t changed = (std::time_t)save.changed;
			std::tm local{};
			if (save.changed > 0 && localtime_r(&changed, &local))
				when = ps5lang::ShortDateTime(local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
			row.value = SizeWords(save.bytes) + (when.empty() ? "" : " · " + when) + "  ·  " + Tr("Hold Cross");
			row.description = Tr("Hold to move to saves-before on the PS5. You can restore it later.");
			// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
			row.help = TrF("Hold Cross to move these files to {0}/saves-before, in a dated folder. The game starts without them next time. Nothing is deleted.",
				ps5paths::Root());
			rows.push_back(std::move(row));
		}
		if (m_gameSaves.empty())
		{
			Row none;
			none.kind = Kind::Action;
			none.id = "gamenosaves";
			none.label = Tr("No saves yet");
			none.description = Tr("They appear here once the game has saved.");
			none.dimmed = true;
			rows.push_back(std::move(none));
		}
		Row backup;
		backup.kind = Kind::Action;
		backup.id = "gamebackup";
		backup.label = Tr("Back up to USB");
		backup.value = m_savesMessage;
		if (running && !status.restoring)
			backup.value = TrF("Copying: {0}", Percent(status.total ? (int)(status.copied * 100 / status.total) : 0));
		else if (backup.value.empty() && !m_gameBackups.empty())
			backup.value = TrF("Last: {0} · {1}", m_gameBackups.front().when, DriveName(m_gameBackups.front().drive));
		backup.description = Tr("Copy this game's saves to a USB drive.");
		backup.help = Tr("Copies this game's saves and save states to PS5CEMU-HAR-saves on the first USB drive, in a dated folder.");
		if (m_gameSaves.empty())
			backup.dimmed = true;
		rows.push_back(std::move(backup));
		std::vector<std::string> backups;
		for (const auto& found : m_gameBackups)
			backups.push_back(found.when + " · " + DriveName(found.drive));
		int& chosen = m_holdChoice["gamerestore"];
		chosen = backups.empty() ? 0 : std::clamp(chosen, 0, (int)backups.size() - 1);
		Row restore;
		restore.kind = Kind::Hold;
		restore.id = "gamerestore";
		restore.label = Tr("Restore from USB");
		restore.value = backups.empty() ? std::string(Tr("No backups found")) : backups[chosen] + "  ·  " + Tr("Hold Cross");
		if (running && status.restoring)
			restore.value = TrF("Copying: {0}", Percent(status.total ? (int)(status.copied * 100 / status.total) : 0));
		restore.description = backups.empty() ? Tr("No backup with this game's saves was found.") : Tr("Left and Right choose which backup, the newest first.");
		// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
		restore.help = TrF("Restores only this game's saves from the chosen backup. Current saves are moved to {0}/saves-before first, so nothing is lost.", ps5paths::Root());
		restore.options = backups;
		restore.index = chosen;
		if (backups.empty())
			restore.dimmed = true;
		rows.push_back(std::move(restore));
		if (IsSwitch() && !ps5eden::IsMadeId(m_gameTitle))
		{
			Row exportSave;
			exportSave.kind = Kind::Action; exportSave.id = "nxexport"; exportSave.label = Tr("Copy the save to USB");
			exportSave.description = Tr("Copies the chosen player's save to USB/PS5-ECAM-exports, for use in another emulator.");
			exportSave.value = m_savesMessage; exportSave.dimmed = running;
			rows.push_back(std::move(exportSave));
			Row importSave;
			// tr: a Switch game's save taken from standalone ProsperoEden on the PS5 or from a USB drive
			importSave.kind = Kind::Hold; importSave.id = "nximport"; importSave.label = Tr("Import a save");
			for (const auto& source : m_switchImports) importSave.options.push_back(source.label);
			int& chosen = m_holdChoice["nximport"];
			chosen = importSave.options.empty() ? 0 : std::clamp(chosen, 0, (int)importSave.options.size() - 1);
			importSave.index = chosen;
			// tr: no save of this Switch game in standalone ProsperoEden or on a USB drive
			importSave.value = importSave.options.empty() ? Tr("No saves found to import") : importSave.options[chosen] + " · " + Tr("Hold Cross");
			importSave.description = Tr("Import from another Switch emulator on this PS5, USB/PS5-ECAM-imports/<title ID>, or a Ryujinx folder in USB/Ryujinx. The replaced save is kept.");
			importSave.help = Tr("Choose the player first in Settings > Players and saves > Players. Files are checked before replacing, and the old save is kept in save-transfer-recovery.");
			importSave.dimmed = running || importSave.options.empty(); rows.push_back(std::move(importSave));
		}
		return rows;
	}

	namespace
	{
		// a play time as hours and minutes
		std::string Duration(uint64_t minutes)
		{
			// tr: a play time under an hour
			if (minutes < 60)
				return TrF("{0} min", minutes);
			// tr: a play time: hours, then minutes
			return TrF("{0} h {1} min", minutes / 60, minutes % 60);
		}
	}

	// Settings > Play time: each side's minutes and how many of its games were played, all sides',
	// and the most played games (the catalogue's counts: ps5catalog::Ended)
	std::string Shell::PlayTimeText() const
	{
		struct Played
		{
			std::string name;
			const char* side;
			uint32_t minutes;
		};
		const char* sides[kSides] = {"Wii U", "3DS", "DS", "Switch", "GameCube", "Wii", "N64", "NES", "GB", "GBC", "GBA", "SNES", "Virtual Boy", "Pokémon mini"};
		std::vector<Played> games;
		std::string text;
		uint64_t all = 0;
		for (int side = 0; side < kSides; side++)
		{
			uint64_t minutes = 0;
			int played = 0;
			for (const ps5catalog::Entry& entry : ps5catalog::Games((ps5catalog::System)side))
				if (entry.minutesPlayed > 0)
				{
					minutes += entry.minutesPlayed;
					played++;
					games.push_back({entry.game.name, sides[side], entry.minutesPlayed});
				}
			all += minutes;
			if (played > 0)
				// tr: Settings > Play time, a side's line: {1} the side (Wii U, 3DS, DS, Switch), {2} its play time, {0} how many games
				text += TrP(played, "{1}: {2}, {0} game", "{1}: {2}, {0} games", sides[side], Duration(minutes)) + "\n";
		}
		if (games.empty())
			return Tr("Nothing played yet. Play time shows here and on each game's page.");
		std::stable_sort(games.begin(), games.end(), [](const Played& a, const Played& b) { return a.minutes > b.minutes; });
		// tr: Settings > Play time: every side's play time added up
		text += TrF("In all: {0}", Duration(all)) + "\n\n" + Tr("Most played") + "\n";
		for (size_t i = 0; i < games.size() && i < 10; i++)
			text += fmt::format("{}. {}  ·  {}  ·  {}\n", i + 1, games[i].name, Duration(games[i].minutes), games[i].side);
		return text;
	}

	std::string Shell::PanelText(const std::string& page) const
	{
		if (InGameSettings())
			return {};
		if (page == "playtime")
			return PlayTimeText();
		if (page == "diagnostics")
		{
			std::string text;
			for (const auto& line : m_status.diagnostics)
				text += line + "\n";
			return text;
		}
		if (page == "launcher")
			return TrF("Your own theme: put a file such as sunset.json in {0}/themes. It appears under Theme by its name:\n{{\"name\": \"Sunset\", \"base\": \"midnight\", \"colours\": {{\"wiiu\": \"#ff7a59\", \"ink-0\": \"#120806\"}}, \"corners\": 0.5, \"motif\": \"none\"}}\nbase: a theme to start from (light, link-jack, classic, midnight, eden). colours: ink-0 to ink-2 (the background), text, wiiu, n3ds, nds and nx (each system's), glass, line, good, warn, bad and focus, as #rrggbb. corners: 0 square to 1.5 round. motif: side, none or eden.",
				ps5paths::Root());
		if (page == "files" && IsSwitch())
			// tr: {0} is Eden's data folder; {1} its game files folder (the same one unless another was chosen)
			return TrF("Saves: {0}/user/nand/user/save. Updates and DLC: beside a game, in its own folder, or in {1}/updates. Mods and cheats: {1}/mods. Covers and names: {0}/covers.",
				ps5eden::DataFolder(), ps5eden::GameFilesFolder());
		if (page == "files" && IsDs())
			// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
			return TrF("Saves: {0}/melonds/saves (a .sav next to the game is used the first time). Save states: melonds/states. Cheats: melonds/cheats/<game file>.mch. Slot-2 GBA games: melonds/slot2/<game file>.gba. DSi games and DS wireless are not supported.",
				ecam_layout::Parent(ps5paths::MelonDs()));
		if (page == "remote" && !m_settings.remoteScreen)
			return Tr("On: scan the QR code here to open the second screen in a phone's browser. The phone must be on the PS5's network.");
		if (page == "usb")
			// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
			return TrF("Put figure dumps in {0}/figures (skylanders, infinity and dimensions). Turn a portal on here, start the game, then place figures from the in-game menu (touchpad click + Options > USB devices).",
				ps5paths::Root());
		if (page == "about")
			// tr: premohq, JMUtechnologies and Phononenom are GitHub names: keep them as they are
			return std::string(Tr("Playtendoh is made by its co-founders, premohq, JMUtechnologies and Phononenom.")) + "\n" +
				   // tr: RetroArch, libretro and XMB Monochrome are names: keep them as they are
				   Tr("The console icons: RetroArch's XMB Monochrome, by the libretro team (CC BY 4.0).") + "\n" +
				   // tr: {0} is the app's version; {1} its data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on; {2} Eden's game files folder; {3} Cemu's selected data folder; {4} its default games folder; {5} Azahar's selected folder; {6} melonDS's selected folder
				   TrF("Cemu, the Wii U emulator, by the Cemu team and its contributors; RADV on the PS5 by Mihawk-99 and mpereiraesaa; the "
				   "community graphic packs' authors.\nAzahar, the 3DS emulator, by the Azahar team and the Citra contributors before them; "
				   "Mihawk-99's PS5 port of it and dynarmic.\nmelonDS, the DS emulator, by Arisotura and the melonDS team.\nEden, the Switch "
				   "emulator, by the Eden team; ProsperoEden, its PS5 port and launcher, by Sword, BlackBearReloaded, bawsdeep, Rockiu0 and its "
				   "contributors.\nBox art and game information: GameTDB. The font: Lexend. The remote touch screen: dolphin78's idea and "
				   "prototype.\nAn unofficial port, not affiliated with the Cemu, Azahar, melonDS or Eden teams, Nintendo or Sony.\n\nWii U "
				   "games: {4}, storage and saves: {3}/mlc01, keys: {3}/keys.txt\n3DS games: {5}/games, storage: "
				   "{5}/sdmc\nDS games: {6}/games, saves: {6}/saves\nSwitch games: {2}/roms, keys: {2}/keys, firmware: "
				   "{2}/firmware\n\nVersion {0}",
				ps5update::Readable(PS5CEMU_VERSION), ps5paths::Root(), ps5eden::GameFilesFolder(), ps5paths::CemuRoot(), ps5paths::Games(), ps5paths::Azahar(), ps5paths::MelonDs());
		return {};
	}

	// -- the menus' language ----------------------------------------------------------------------------

	std::vector<std::string> Shell::LanguageOptions(int& index) const
	{
		// tr: the first choice in the language list: the PS5's own ({0}, in its own words)
		std::vector<std::string> options{TrF("The PS5's language: {0}", ps5lang::NameOf(ps5lang::SystemLanguage()))};
		index = 0;
		for (const ps5lang::Language& language : ps5lang::Languages())
		{
			if (m_settings.ui.language == language.code)
				index = (int)options.size();
			options.push_back(language.name);
		}
		return options;
	}

	void Shell::OpenLanguagePicker(const std::string& kicker)
	{
		int index = 0;
		std::vector<std::string> options = LanguageOptions(index);
		OpenPicker(kicker, Tr("Language"), std::move(options), index, [this](int choice) { SetLanguage(choice); });
	}

	void Shell::SetLanguage(int index)
	{
		// the first choice is the PS5's own; the rest are ps5lang's, in its order
		const auto& languages = ps5lang::Languages();
		m_settings.ui.language = index > 0 && index <= (int)languages.size() ? languages[index - 1].code : "";
		SaveSettings();
		ApplyLanguage();
		Toast(TrF("Language: {0}", ps5lang::NameOf(ps5lang::Current())));
	}

	// -- changing a row ---------------------------------------------------------------------------------

	void Shell::SetRowIndex(ps5settings::Launcher& settings, const std::string& id, int index)
	{
		ps5settings::N3ds& hand = IsRetro() ? settings.retro[ps5retro::Slot(SideIndex())] : m_side == System::Nds ? settings.nds : m_side == System::Switch ? settings.nx : settings.n3ds;
		if (id == "retro-backend" && IsRetro()) settings.retro[ps5retro::Slot(SideIndex())].retroBackend = std::clamp(index, 0, 1);
		else if (id.rfind("core:", 0) == 0 && IsRetro())
		{
			// a core option: kept only where it is not the core's own value, so a core's new default reaches it
			const std::string key = id.substr(5);
			auto& chosen = settings.retro[ps5retro::Slot(SideIndex())].coreOptions;
			for (const auto& option : ps5libretro::CoreOptions(SideIndex()))
				if (option.key == key && index >= 0 && index < (int)option.values.size())
				{
					if (option.values[index].first == option.defaultValue)
						chosen.erase(key);
					else
						chosen[key] = option.values[index].first;
				}
		}
		else if (id == "music")
		{
			const std::vector<std::string> values = MusicValues(nullptr);
			settings.music = values[std::clamp(index, 0, (int)values.size() - 1)];
		}
		else if (id == "holdms")
			settings.ui.holdMs = kHolds[std::clamp(index, 0, 5)];
		else if (id == "upscaling")
			settings.upscaleFilter = std::clamp(index, 0, 4);
		else if (id == "framepacing")
			settings.framePacing = std::clamp(index + 1, 1, 3);
		else if (id == "resolution")
			hand.resolution = std::clamp(index + 1, 1, 10);
		else if (id == "filter")
			hand.textureFilter = kOfferedFilters[std::clamp(index, 0, (int)std::size(kOfferedFilters) - 1)];
		else if (id == "layout")
			hand.layout = std::clamp(index, 0, 3);
		else if (id == "border")
			hand.border = std::clamp(index, 0, 5);
		else if (id == "region")
			hand.region = std::clamp(index - 1, -1, 6);
		else if (id == "language")
			hand.language = std::clamp(index - 1, -1, 11);
		else if (id == "cpuclock")
			hand.cpuClock = kClocks[std::clamp(index, 0, (int)std::size(kClocks) - 1)];
		else if (id == "dstouchx") hand.dsTouchX = std::clamp(index - 20, -20, 20);
		else if (id == "dstouchy") hand.dsTouchY = std::clamp(index - 20, -20, 20);
		else if (id == "dstouchscale") hand.dsTouchScale = std::clamp(index + 50, 50, 150);
		else if (id == "dsmic")
			hand.dsMicSource = std::clamp(index, 0, 2);
		else if (id == "dsfilter")
			hand.dsFilter = std::clamp(index, 0, 2);
		else if (id == "retro-filter")
			hand.dsFilter = std::clamp(index, 0, ps5ingame3ds::kScreenFilterCount - 1);
		else if (id == "retro-size")
			hand.pictureSize = std::clamp(index, 0, ps5ingame3ds::kPictureSizeCount - 1);
		else if (id == "retro-shape")
			hand.pictureShape = std::clamp(index, 0, ps5ingame3ds::kPictureShapeCount - 1);
		else if (id == "dsrotation")
			hand.dsRotation = std::clamp(index, 0, 3);
		else if (id == "nx-resolution")
			hand.nxResolution = std::clamp(index, 0, 6);
		else if (id == "nx-filter")
			hand.nxFilter = std::clamp(index, 0, 3);
		else if (id == "nx-astc")
			hand.nxAstc = std::clamp(index, 0, 1);
		else if (id == "nx-docked")
			hand.nxDocked = index == 0;
		else if (id == "nx-speedlimit")
			hand.nxSpeedLimit = std::clamp(index, 0, 4);
		else if (id == "nx-covers")
			hand.nxCovers = std::clamp(index, 0, 1);
		else if (id == "nx-sharpening")
			hand.nxSharpening = kNxSharpening[std::clamp(index, 0, (int)std::size(kNxSharpening) - 1)];
		else if (id == "nx-aa")
			hand.nxAntiAliasing = std::clamp(index, 0, 2);
		else if (id == "nx-controller")
			hand.nxController = std::clamp(index - 1, -1, 4);
		else if (id == "nx-output")
			hand.nxOutput = std::clamp(index, 0, 2);
		else if (id == "nx-language")
			hand.nxLanguage = std::clamp(index - 1, -1, (int)std::size(ps5eden::kLanguages) - 1);
		else if (id == "nx-memory")
			hand.nxMemory = std::clamp(index, 0, 2);
		else if (id == "theme")
			settings.ui.theme = index >= 0 && index < (int)m_themes.size() ? m_themes[index].id : "";
		else if (id == "launcher")
		{
			// EmulationStation from the app's next start, offered only once the app has it (its download is
			// the row below: a choice changes as focus passes, so it never starts one); Classic or Orbit now
			// (the row is "launcher": "layout" is the handhelds' screen layout, caught above)
			settings.ui.emulationStation = index == 2 && ps5esde::Available();
			if (index < 2)
				settings.ui.layout = std::clamp(index, 0, 1);
			else if (settings.ui.emulationStation)
				Toast(Tr("EmulationStation opens next time Playtendoh starts. Hold L1 while it starts to use this launcher instead."));
			settings.ui.layoutChosen = true;
		}
		else if (id == "abeverywhere")
		{
			// A and B on every side: the 3DS's and the DS's now, the Wii U players' now when Cemu is up, else
			// as the Wii U side next opens (OpenSide)
			using ps5azahar::Button;
			using ps5emu::PadInput;
			const bool aOnCircle = index == 0;
			for (ps5settings::N3ds* handheld : {&settings.n3ds, &settings.nds})
			{
				const bool ds = handheld == &settings.nds;
				ps5azahar::SetMapping(*handheld, (size_t)Button::A, aOnCircle ? PadInput::Circle : PadInput::Cross, ds);
				ps5azahar::SetMapping(*handheld, (size_t)Button::B, aOnCircle ? PadInput::Cross : PadInput::Circle, ds);
				ps5azahar::SetMapping(*handheld, (size_t)Button::X, aOnCircle ? PadInput::Triangle : PadInput::Square, ds);
				ps5azahar::SetMapping(*handheld, (size_t)Button::Y, aOnCircle ? PadInput::Square : PadInput::Triangle, ds);
			}
			settings.wiiuFaceLayout = aOnCircle ? 1 : 2;
			if (m_status.coreReady)
			{
				for (int player = 0; player < ps5pad::kMaxPlayers; player++)
					ps5emu::SetFaceLayout(player, aOnCircle ? ps5emu::FaceLayout::Nintendo : ps5emu::FaceLayout::PlayStation);
				settings.wiiuFaceLayout = 0;
			}
		}
		else if (id == "turbo")
			(IsWiiU() ? settings.turbo : hand.turbo) = index <= 0 || index > (int)std::size(kTurboInputs) ? "" : kTurboInputs[index - 1];
		else if (id == "abbuttons")
		{
			using ps5azahar::Button;
			using ps5emu::PadInput;
			const bool aOnCircle = index == 0, ds = m_side == System::Nds;
			ps5azahar::SetMapping(hand, (size_t)Button::A, aOnCircle ? PadInput::Circle : PadInput::Cross, ds);
			ps5azahar::SetMapping(hand, (size_t)Button::B, aOnCircle ? PadInput::Cross : PadInput::Circle, ds);
			ps5azahar::SetMapping(hand, (size_t)Button::X, aOnCircle ? PadInput::Triangle : PadInput::Square, ds);
			ps5azahar::SetMapping(hand, (size_t)Button::Y, aOnCircle ? PadInput::Square : PadInput::Triangle, ds);
		}
	}

	bool Shell::ApplyRow(ps5settings::Launcher& settings, const Row& row, int step, bool cross)
	{
		ps5settings::N3ds& hand = IsRetro() ? settings.retro[ps5retro::Slot(SideIndex())] : m_side == System::Nds ? settings.nds : m_side == System::Switch ? settings.nx : settings.n3ds;
		const std::string& id = row.id;
		switch (row.kind)
		{
		case Row::Kind::Toggle:
		{
			if (!cross && step != 0 && ((step > 0) == row.on))
			{
				m_feedback.Play(ui::Cue::Edge);
				return false;
			}
			const bool on = !row.on;
			if (id.rfind("ownfolder-", 0) == 0)
			{
				// one system into a folder of its own, or back with the rest: written down, made as the app starts again
				const std::string system = id.substr(10);
				if (!ps5reorganize::Ask(ps5paths::Root(), on ? ps5reorganize::Request::Separate : ps5reorganize::Request::PutBack, {system}))
				{
					Toast(TrF("Playtendoh cannot write to {0}", ps5paths::Root()));
					return false;
				}
				ps5log::Line("[reorganize] {} {} asked for in Settings", system, on ? "into its own folder" : "back with the rest");
				Toast(Tr("Playtendoh restarts to move the files"));
				m_reorganize.restartAt = m_now;
				return true;
			}
			if (id.rfind("side-", 0) == 0)
			{
				const int bit = 1 << (int)ps5launcher::SideNamed(id.substr(5));
				const int sides = on ? settings.ui.sides | bit : settings.ui.sides & ~bit;
				if (sides == 0)
				{
					m_feedback.Play(ui::Cue::Denied);
					Toast(Tr("At least one system must stay on"));
					return false;
				}
				settings.ui.sides = sides;
			}
			else if (id == "pictures")
				settings.ui.gamePictures = on;
			else if (id == "boxart3d")
			{
				settings.ui.boxArt3d = on;
				ps5boxart::SetSpines(on);
			}
			else if (id == "filters")
				settings.ui.libraryFilters = on;
			else if (id == "compatbadges")
				settings.ui.compatBadges = on;
			else if (id == "emulationstation")
				settings.ui.emulationStation = on;
			else if (id == "lookonshares")
				settings.lookOnShares = on;
			else if (id == "menusounds")
				settings.menuSounds = on;
			else if (id == "gamepadspeaker")
				settings.gamePadSpeaker = on;
			else if (id == "remote")
				settings.remoteScreen = on;
			else if (id == "rumble")
				settings.rumble = on;
			else if (id == "boxart")
				settings.boxArt = on;
			else if (id == "cheatsauto")
				settings.cheats = on;
			else if (id == "reports")
			{
				settings.ui.reports = on;
				settings.ui.reportsAsked = true;
			}
			else if (id == "accountperuser")
				settings.accountPerUser = on;
			else if (id == "largertext")
				settings.ui.largerText = on;
			else if (id == "highcontrast")
				settings.ui.highContrast = on;
			else if (id == "reducemotion")
				settings.ui.reduceMotion = on;
			else if (id == "highframerate")
				settings.highFrameRate = on;
			else if (id == "framegeneration")
				settings.frameGeneration = on;
			else if (id == "framegeneration-3ds")
				hand.frameGeneration = on;
			else if (id == "overlay")
				settings.overlay = on;
			else if (id == "async")
				settings.asyncShaders = on;
			else if (id == "shadernotices")
				settings.shaderNotices = on;
			else if (id == "customtextures")
				hand.customTextures = on;
			else if (id == "async-3ds")
				hand.asyncShaders = on;
			else if (id == "outlinefix")
				hand.invariantPosition = on;
			else if (id == "hwshaders")
				hand.hwShaders = on;
			else if (id == "anaglyph")
				hand.anaglyph = on;
			else if (id == "motion")
				hand.motion = on;
			else if (id == "dsjit")
				hand.dsJit = on;
			else if (id == "dsslot2")
				hand.dsSlot2 = on;
			else if (id == "dsstylus")
				hand.dsStylus = on;
			else if (id == "performance-hand")
				hand.performance = on;
			else if (id == "nx-framegeneration")
				hand.nxFrameGeneration = on;
			else if (id == "reshade")
				settings.reshade = on;
			else if (id == "nx-120hz")
				hand.nxHighRefresh = on;
			else if (id == "nx-async")
				hand.nxAsyncShaders = on;
			else if (id == "nx-fastgpu")
				hand.nxFastGpu = on;
			else if (id == "nx-unsafecpu")
				hand.nxUnsafeCpu = on;
			else if (id == "nx-unsafedma")
				hand.nxUnsafeDma = on;
			else if (id == "nx-reactive")
				hand.nxReactiveFlushing = on;
			else if (id == "nx-skipinvalidation")
				hand.nxSkipInvalidation = on;
			else if (id == "nx-blocklist")
				hand.nxBlockList = on;
			else if (id == "nx-compactmemory")
				hand.nxCompactMemory = on;
			else if (id == "hiddentouch")
				hand.hiddenTouch = on;
			else if (id == "screengap")
				hand.screenGap = on;
			else if (id == "dsownbios")
				hand.dsOwnBios = on;
			else
				return false;
			m_feedback.Play(ui::Cue::Toggle);
			return true;
		}
		case Row::Kind::Choice:
		case Row::Kind::Stepper:
		case Row::Kind::Segmented:
		case Row::Kind::Swatches:
		{
			const int count = (int)row.options.size();
			// the language: its list, whichever way it is asked for (stepping would change every word at each press)
			if (row.kind == Row::Kind::Choice && (cross || id == "ui-language"))
			{
				// the dropdown: its choice is made where the row's value lives (the side's, or the game's)
				const Row chosen = row;
				OpenPicker(InGameSettings() ? m_gameName : SystemName(m_side), row.label, row.options, row.index,
					[this, chosen](int index) { ChooseIndex(chosen, index); });
				return false;
			}
			int index = row.index + (cross ? 1 : step);
			if (row.kind == Row::Kind::Choice || cross)
				index = (index + count) % count; // round
			else if (index < 0 || index >= count)
			{
				m_feedback.Play(ui::Cue::Edge);
				return false;
			}
			SetRowIndex(settings, id, index);
			m_feedback.Play(ui::Cue::Focus);
			return true;
		}
		case Row::Kind::Slider:
		{
			int* value = id == "musicvolume" ? &settings.musicVolume :
				id == "volume"				 ? (IsWiiU() ? &settings.volume : &hand.volume) :
				id == "deadzone"			 ? &hand.deadzone :
				id == "nx-deadzone"			 ? &hand.nxDeadzone :
											   nullptr;
			if (!value)
				return false;
			const int most = id == "deadzone" ? 50 : id == "nx-deadzone" ? 30 : 100, by = id == "deadzone" ? 5 : id == "nx-deadzone" ? 2 : 10;
			int next = *value + (cross ? by : step * by);
			if (cross && next > most)
				next = 0;
			next = std::clamp(next, 0, most);
			if (next == *value)
			{
				m_feedback.Play(ui::Cue::Edge);
				return false;
			}
			*value = next;
			m_feedback.Play(ui::Cue::Focus);
			return true;
		}
		default: return false;
		}
	}

	// What a changed setting changes at once: the launcher's sound, the DualSense's rumble, box art
	void Shell::UseOwnAccount()
	{
		if (!m_status.coreReady || !m_settings.accountPerUser)
			return;
		const int32_t user = ps5pad::UserId(0);
		if (user < 0)
			return;
		const uint32_t id = ps5emu::AccountForUser(fmt::format("{:#010x}", user), ps5pad::UserName(0), m_settings.accountsByUser);
		SaveSettings();
		for (const auto& account : ps5emu::ListAccounts())
			if (account.id == id)
				Toast(TrF("Games start on {0}'s saves", account.name));
	}

	void Shell::SettingsChanged(const std::string& id)
	{
		ps5sound::SetMusic(m_settings.music, m_settings.musicVolume);
		ps5sound::SetMenuSounds(m_settings.menuSounds);
		ps5pad::SetVibrationEnabled(m_settings.rumble);
		if (id == "theme" || id == "launcher")
		{
			ApplyTheme();
			m_feedback.SetLight(Accent());
			m_orbitHome = false;
		}
		if (id.rfind("side-", 0) == 0)
			SidesChanged();
		if (id == "filters")
		{
			m_libraryIndex = 0;
			LibraryRefresh(); // every game again, or the side's filter
		}
		if (id == "accountperuser" && m_settings.accountPerUser)
			UseOwnAccount();
		if (id == "remote")
			ps5stream::Configure(m_settings.remoteScreen);
		if (id == "reports")
			ps5reports::SetEnabled(ps5reports::Available() && m_settings.ui.reports); // off: what waits is deleted now
		if (id == "boxart")
		{
			ps5boxart::SetEnabled(m_settings.boxArt);
			if (m_settings.boxArt)
				FetchBoxArt();
		}
		if (id == "cheatsauto" && m_settings.cheats)
			FetchCheats();
		if (id == "nx-covers")
		{
			// each game's cover again, the other kind: box art fetched where it is missing
			for (Game& game : m_games)
				game.boxArt = SwitchCover(game.entry.game);
			FetchBoxArt();
		}
		SaveSettings();
	}

	// A game's own value, from its view of the settings after a row changed it
	void Shell::KeepGameSetting(const std::string& setting, const ps5settings::Launcher& view)
	{
		const std::string side = ps5launcher::SideName(m_side);
		if (setting.starts_with("core:") && IsRetro())
		{
			// a core option: its value in the game's view, kept as the game's own (empty: the core's own value)
			const std::string key = setting.substr(5);
			const auto& chosen = view.retro[ps5retro::Slot(SideIndex())].coreOptions;
			const auto found = chosen.find(key);
			m_settings.gameCoreOptions[ps5settings::GameKey(side, m_gameTitle)][key] = found == chosen.end() ? std::string() : found->second;
			SaveSettings();
			m_gameView = ps5settings::ForGame(m_settings, side, m_gameTitle);
			return;
		}
		int value = 0;
		if (setting.empty() || !ps5settings::Value(view, side, setting, value))
			return;
		m_settings.games[ps5settings::GameKey(side, m_gameTitle)][setting] = value;
		SaveSettings();
		m_gameView = ps5settings::ForGame(m_settings, side, m_gameTitle);
	}

	void Shell::ChooseIndex(const Row& row, int index)
	{
		if (row.id == "retro-backend" && IsRetro())
		{
			m_settings.retro[ps5retro::Slot(SideIndex())].retroBackend = std::clamp(index, 0, 1); SaveSettings(); return;
		}
		if (row.id == "ui-language")
		{
			SetLanguage(index);
			return;
		}
		if (InGameSettings())
		{
			ps5settings::Launcher view = m_gameView;
			SetRowIndex(view, row.id, index);
			KeepGameSetting(row.setting, view);
			return;
		}
		SetRowIndex(m_settings, row.id, index);
		SettingsChanged(row.id);
	}

	void Shell::ResetGameSetting(const Row& row)
	{
		if (row.id.starts_with("porpoise:"))
		{
			ResetPorpoise(row);
			return;
		}
		const std::string side = ps5launcher::SideName(m_side);
		if (row.setting.starts_with("core:"))
		{
			// a core option of the game's own: the side's again
			const auto options = m_settings.gameCoreOptions.find(ps5settings::GameKey(side, m_gameTitle));
			if (options == m_settings.gameCoreOptions.end() || !options->second.erase(row.setting.substr(5)))
			{
				m_feedback.Play(ui::Cue::Edge);
				return;
			}
			if (options->second.empty())
				m_settings.gameCoreOptions.erase(options);
			SaveSettings();
			m_gameView = ps5settings::ForGame(m_settings, side, m_gameTitle);
			m_feedback.Play(ui::Cue::Back);
			return;
		}
		const auto found = m_settings.games.find(ps5settings::GameKey(side, m_gameTitle));
		if (row.setting.empty() || found == m_settings.games.end() || !found->second.erase(row.setting))
		{
			m_feedback.Play(ui::Cue::Edge);
			return;
		}
		if (found->second.empty())
			m_settings.games.erase(found);
		SaveSettings();
		m_gameView = ps5settings::ForGame(m_settings, side, m_gameTitle);
		m_feedback.Play(ui::Cue::Back);
	}

	void Shell::GameSettingsOpen(int game)
	{
		m_switchExtrasKey.clear();
		m_porpoise.read = false;
		if (game < 0 || game >= (int)m_games.size())
			return;
		const ps5emu::Game& g = m_games[game].entry.game;
		if (g.titleId == 0)
		{
			m_feedback.Play(ui::Cue::Denied);
			Toast(Tr("Settings can't be saved for this game (no game ID)"));
			return;
		}
		m_gameFrom = m_screen == ScreenId::Hub ? ScreenId::Hub : ScreenId::Library;
		m_sidePage = m_page;
		m_gameTitle = g.titleId;
		m_gameName = g.name;
		m_gameView = ps5settings::ForGame(m_settings, ps5launcher::SideName(m_side), g.titleId);
		m_gameRef = {SideIndex(), g.titleId, g.path.string()};
		m_gameSavesAt = -100;
		m_savesMessage.clear();
		const auto pages = SettingsPages();
		m_page = pages.empty() ? "graphics" : pages.front().id;
		m_subPage.clear();
		m_subParents.clear();
		m_onRows = false;
		m_settingRow = 0;
		m_onBar = false;
		m_message.clear();
		Show(ScreenId::Settings);
		m_screenAt = m_now;
	}

	void Shell::GameSettingsClose()
	{
		m_gameTitle = 0;
		m_gameName.clear();
		m_page = m_sidePage;
		m_subPage.clear();
		m_subParents.clear();
		m_onRows = false;
		m_message.clear();
		if (m_gameFrom == ScreenId::Hub)
			Show(ScreenId::Hub);
		else
			ShowTab(m_gameFrom); // the Library, or Eden's Home
	}

	// A row changed by Left or Right (step) or Cross
	void Shell::ChangeSetting(const Row& row, int step, bool cross)
	{
		const std::string& id = row.id;
		if (id.starts_with("cheats:"))
		{
			ChangeCheats(row, step, cross);
			return;
		}
		if (id.starts_with("nxextra:"))
		{
			ChangeSwitchExtra(row, step, cross);
			return;
		}
		if (id.starts_with("porpoise:"))
		{
			ChangePorpoise(row, step, cross);
			return;
		}
		if (id.starts_with("porpoisesave:"))
		{
			if (cross)
				ChoosePorpoiseSave(row);
			return;
		}
		if (id.starts_with("porpoisegame:"))
		{
			ChangePorpoiseGame(row, cross);
			return;
		}
		if (id.starts_with("porpoisebuttons:"))
		{
			if (cross)
			{
				m_porpoiseButtons = id.substr(std::strlen("porpoisebuttons:"));
				m_feedback.Play(ui::Cue::Select);
				MappingOpen();
			}
			return;
		}
		if (id.starts_with("porpoisecardsave:") || id.starts_with("porpoisecardload:") || id.starts_with("porpoisecarddelete:"))
		{
			if (cross)
				PorpoiseCardAction(id);
			return;
		}
		if (id.starts_with("porpoisecard:"))
		{
			if (cross)
				CopyPorpoiseCard(row);
			return;
		}
		if (id == "porpoisesaves-import")
		{
			if (cross)
				ImportPorpoiseSaves();
			return;
		}
		if (row.dimmed && (row.needsCore || id == "homemenu"))
		{
			m_feedback.Play(ui::Cue::Denied);
			return;
		}
		if (row.kind == Row::Kind::Hold)
		{
			if (cross)
			{
				m_feedback.Play(ui::Cue::Denied); // held, not pressed
				m_message.clear();
			}
			else if (step != 0 && row.options.size() > 1)
			{
				// a held row with choices (which account, which backup): Left and Right choose
				int& which = m_holdChoice[id];
				which = (which + step + (int)row.options.size()) % (int)row.options.size();
				m_feedback.Play(ui::Cue::Focus);
			}
			else if (step != 0)
				m_feedback.Play(ui::Cue::Edge);
			return;
		}
		if (id == "account")
		{
			// the account games start on: the next, or the one before
			const auto accounts = ps5emu::ListAccounts();
			if (accounts.size() < 2 || m_settings.accountPerUser)
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			const int count = (int)accounts.size();
			const int next = (row.index + (step < 0 ? -1 : 1) + count) % count;
			ps5emu::SetCurrentAccount(accounts[next].id);
			m_feedback.Play(ui::Cue::Focus);
			// tr: the Wii U account games start on now ({0}: its name)
			Toast(TrF("Games start on {0}'s saves", accounts[next].name));
			return;
		}
		if (id == "cheevosnotices")
		{
			if (row.dimmed)
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			ps5cheevos::SetNotices(!row.on);
			m_feedback.Play(ui::Cue::Toggle);
			return;
		}
		if (InGameSettings() && id == "gamebackup")
		{
			if (!cross)
				return;
			std::string error;
			if (ps5saves::StartGameBackup(m_gameRef, error))
			{
				m_savesMessage.clear();
				m_feedback.Play(ui::Cue::Select);
				Toast(TrF("Backing up {0}'s saves…", m_gameName));
			}
			else
			{
				m_savesMessage = error;
				m_feedback.Play(ui::Cue::Denied);
			}
			return;
		}
		if (InGameSettings() && IsSwitch() && id == "nxexport")
		{
			if (!cross || row.dimmed) return;
			const auto view = ps5eden::extras::Read(SwitchExtrasContext());
			std::string error;
			if (!view.error.empty()) error = view.error;
			else if (view.profiles.empty()) error = Tr("Choose a player first, in Settings > Players and saves.");
			else if (ps5saves::StartSwitchExport(m_gameRef, view.profiles[view.profile].id, error)) m_savesMessage = Tr("Copying to USB…");
			if (!error.empty()) Toast(error);
			return;
		}
		if (InGameSettings() && row.setting.empty())
		{
			m_feedback.Play(ui::Cue::Denied); // a row with nothing to change (No saves yet)
			return;
		}
		if (InGameSettings())
		{
			// the game's own value: from its view of the settings, kept as its own
			ps5settings::Launcher view = m_gameView;
			if (ApplyRow(view, row, step, cross))
				KeepGameSetting(row.setting, view);
			return;
		}
		if (id == "usb")
		{
			// Cemu's settings.xml keeps it, as its Emulated USB Devices window does
			const auto device = (ps5usb::Device)row.index;
			ps5usb::SetEnabled(device, !ps5usb::Enabled(device));
			m_feedback.Play(ui::Cue::Toggle);
			return;
		}
		if (row.kind != Row::Kind::Action && row.kind != Row::Kind::Link)
		{
			if (ApplyRow(m_settings, row, step, cross))
				SettingsChanged(id);
			return;
		}
		if (!cross)
			return;
		m_feedback.Play(ui::Cue::Select);
		auto& hand = Handheld(m_side);
		if (id == "newaccount")
		{
			if (ps5emu::ListAccounts().size() >= 12)
			{
				Toast(Tr("The Wii U has twelve accounts at most."));
				return;
			}
			OpenNameKeyboard(Tr("A name for the new account"), "", 10, [this](const std::string& name) {
				if (name.empty())
					return;
				std::string error;
				if (ps5emu::CreateAccount(name, error))
					Toast(TrF("Games start on {0}'s saves", name));
				else
					Toast(error);
			});
		}
		else if (id == "renameaccount")
		{
			const uint32_t current = ps5emu::CurrentAccount();
			std::string name;
			for (const auto& account : ps5emu::ListAccounts())
				if (account.id == current)
					name = account.name;
			OpenNameKeyboard(Tr("A new name"), name, 10, [this, current](const std::string& name) {
				if (!name.empty() && ps5emu::RenameAccount(current, name))
					Toast(TrF("Renamed {0}", name));
			});
		}
		else if (id == "backup")
		{
			std::string error;
			if (ps5saves::StartBackup(error))
			{
				m_savesMessage.clear();
				Toast(Tr("Backing up every side's saves…"));
			}
			else
			{
				m_savesMessage = error;
				m_feedback.Play(ui::Cue::Denied);
			}
		}
		else if (id == "esde-get")
		{
			// Cross stops a download under way (held starts one, or removes ES-DE: HoldDone)
			if (ps5esdeget::Busy() && ps5esdeget::Cancel())
				Toast(Tr("EmulationStation's download stopped"));
			else
				m_feedback.Play(ui::Cue::Denied);
		}
		else if (id == "retro-diagnostics" && IsRetro())
			OpenHelp(Tr("Emulator diagnostics"), ps5retro::Diagnostics(SideIndex(), m_settings.retro[ps5retro::Slot(SideIndex())].retroBackend));
		else if (id == "retro-library" && IsRetro())
		{
			// tr: the loading screen's name for a retro side's own emulator menus: {0} is the side (Nintendo 64)
			ps5emu::Game game; game.name = TrF("{0}: emulator settings", SideTitle(m_side));
			LaunchGame(game, Tr("Opening emulator settings"), {760, 240, 400, 560});
		}
		else if (id == "folder")
		{
			m_filesSide = m_side;
			m_pageFrom = ScreenId::Settings;
			FilesOpen(0);
		}
		else if (id == "shares")
			SharesOpen(ScreenId::Settings);
		else if (id == "rescan")
			LookForGames();
		else if ((id == "boxartmissing" || id == "boxartall") && IsRetro() && !IsPorpoise())
		{
			// a libretro side's: libretro's thumbnails, by name, those it had none for asked again
			std::vector<std::string> names;
			for (const Game& game : m_games)
				if (id == "boxartall" || game.boxArt.empty())
					names.push_back(std::filesystem::path(game.entry.game.path).stem().string());
			if (!m_settings.boxArt)
			{
				m_settings.boxArt = true;
				ps5boxart::SetEnabled(true);
				SaveSettings();
			}
			ps5boxart::FetchRetro(RetroThumbnails(SideIndex()), names, true);
			Toast(names.empty() ? std::string(Tr("Every game has its cover")) : TrP((long long)names.size(), "Looking for {0} cover", "Looking for {0} covers"));
		}
		else if (id == "boxartmissing" || id == "boxartall")
		{
			std::vector<std::string> ids;
			for (const Game& game : m_games)
				if (!game.entry.game.gameId.empty() && (id == "boxartall" || game.boxArt.empty()))
					ids.push_back(game.entry.game.gameId);
			if (!m_settings.boxArt)
			{
				// asking for covers turns box art on
				m_settings.boxArt = true;
				ps5boxart::SetEnabled(true);
				SaveSettings();
			}
			ps5boxart::Refetch(IsPorpoise() ? ps5boxart::System::GameCubeWii : BoxSide(m_side), ids, id == "boxartall");
			if (ids.empty())
				m_feedback.Play(ui::Cue::Denied);
		}
		else if (id == "packs")
			ps5packs::Start(); // GitHub's latest community packs, when newer
		else if (id == "mods")
			AddMods(0, {}); // a mod with no rules.txt needs a title ID folder from here
		else if (id == "appupdate")
		{
			// installing shows over the screen (the update sheet); otherwise GitHub is asked again
			const auto state = ps5update::GetStatus().state;
			if (state == ps5update::Status::State::Available)
				ps5update::Install();
			else if (state == ps5update::Status::State::Ready)
				m_update.restartAt = m_now; // as the update sheet's Restart now (Frame)
			else
				ps5update::Check();
		}
		else if (id == "player" || id == "porpoiseplayer")
			PlayerOpen(row.index);
		else if (id == "nxkeys" || id == "nxfirmware")
			OpenHelp(row.label, row.help);
		else if (id == "install")
			UpdatesOpen(0, ScreenId::Settings); // Updates and DLC: what the drives have (6.12)
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
				Toast(Tr("This room is for a different version."));
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
				Toast(TrF("Switch games will join {0}", room.name));
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
					OpenTextKeyboard(Tr("Password (leave empty if none)"), "", 128, true, [this, host, number](const std::string& password) {
						ps5settings::N3ds& nx = Handheld(System::Switch);
						nx.nxRoomHost = host;
						nx.nxRoomPort = number;
						nx.nxRoomName = host;
						nx.nxRoomPassword = password;
						SaveSettings();
						Toast(TrF("Switch games will join {0}", host));
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
		else if (id == "steamgriddb")
			OpenTextKeyboard(Tr("Your SteamGridDB key"), ps5boxart::PlayerSteamGridKeyText(), 64, true, [this](const std::string& key) {
				ps5boxart::SetSteamGridKey(key);
				m_porpoise.read = false;
				Toast(key.empty() ? Tr("Your SteamGridDB key is removed") : Tr("Used next time Playtendoh starts"));
			});
		else if (id == "makeityours")
			StartOpen(true);
		else if (id == "copylogs")
			m_diagnosticsDone[0] = CopyLogsToUsb();
		else if (id == "setupcheck")
			SetupOpen(false);
	}

	void Shell::HoldDone(const std::string& id)
	{
		if (id == "esde-get" && !ps5esde::Available())
		{
			// the download, held to say yes: the size was on the row
			ps5esdeget::Start();
			Toast(Tr("Downloading EmulationStation. Choose it as the Layout once it's in."));
			return;
		}
		if (id == "esde-get")
		{
			std::string error;
			if (ps5esdeget::Remove(error))
			{
				if (m_settings.ui.emulationStation)
					m_settings.ui.emulationStation = false;
				SaveSettings();
				Toast(Tr("EmulationStation removed. Its scraped data is kept."));
			}
			else
				Toast(error);
			return;
		}
		if (id == "core-reset" && IsRetro())
		{
			m_settings.retro[ps5retro::Slot(SideIndex())].coreOptions.clear();
			SaveSettings();
			Toast(TrF("{0} reset to defaults", ps5libretro::CoreTitle(SideIndex())));
			return;
		}
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
				m_message = TrF("Playtendoh cannot write to {0}. Is the HEN loaded?", ps5paths::Root());
				return;
			}
			ps5log::Line("[reorganize] {} asked for in Settings: as the app starts again", id == "separate" ? "a folder for each system" : "putting back");
			ps5update::Restart();
			m_message = ps5update::GetStatus().message; // back only when the PS5 did not start it again
			if (m_message.empty())
				m_message = Tr("Close Playtendoh and start it again from the home screen to move the folders.");
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
					Toast(TrF("{0} deleted. Its saves are kept.", others[which].name));
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
			ps5porpoise::ClearAchievementsAccount();
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
				ps5saves::StartSwitchImport(m_gameRef, view.profiles[view.profile].id, m_switchImports[selected].folder, error, m_switchImports[selected].accountSaveId)) m_savesMessage = Tr("Importing the save…");
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
		else if (id == "porpoiseresetall")
			ResetAllPorpoise();
		else if (id == "resetgame")
		{
			if (IsPorpoise())
				ForgetPorpoiseGame();
			m_settings.games.erase(ps5settings::GameKey(ps5launcher::SideName(m_side), m_gameTitle));
			m_settings.gameCoreOptions.erase(ps5settings::GameKey(ps5launcher::SideName(m_side), m_gameTitle));
			SaveSettings();
			m_gameView = ps5settings::ForGame(m_settings, ps5launcher::SideName(m_side), m_gameTitle);
			m_message = Tr("Done. Using the system's settings again");
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
			m_subPage.clear(), m_subParents.clear();
		auto rows = SettingRows(m_page);
		if (!m_subPage.empty() && rows.empty())
		{
			m_subPage.clear();
			m_subParents.clear();
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
		Glass(canvas, card, kRadiusCard, Surface());
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
			working ? Tr("Use a device on the same network as the PS5. No app needed. Sound stays on the PS5. Anyone on the network can open it.") :
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
		Glass(canvas, card, kRadiusCard, Surface());
		const float pad = 32, qr = std::min(card.w - 2 * pad, 260.0f);
		float y = card.y + pad;
		canvas.Text(Style(kOverlineStyle), card.x + pad, y, Tr("Support Playtendoh"), Accent(), card.w - 2 * pad, 1);
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
			Tr("Playtendoh is free. If you enjoy it, you can support its work here."), Secondary(), card.w - 2 * pad, 4,
			ui::Align::Centre);
	}

	void Shell::SettingsDraw(Canvas& canvas)
	{
		// 6.0: Settings is a place of its own, not a box over the Library: the side's colour as a slow aurora
		// behind glass, the pages on a floating rail with a liquid blob that
		// slides and stretches between them, and every row of a page on one tall sheet of glass, a highlight
		// gliding inside it to the focused row
		const float appear = Enter(0);
		const bool light = LightGround(), contrast = m_settings.ui.highContrast;
		const uint32_t accent = Accent();
		const auto pages = SettingsPages();
		{
			// 6.0: the side's backdrop stays, dimmed only so far that its picture, shapes and colours show
			// through the glass; high contrast covers it, as before
			const Box screen{0, 0, 1920, 1080};
			const float dim = contrast ? 0.97f : light ? 0.55f : 0.50f;
			canvas.Rect(screen, 0, ui::WithAlpha(light ? ui::SetAlpha(ui::Mix(kInk1, 0xffffffff, 0.15f), dim) : ui::SetAlpha(kInk0, dim), appear));
			if (!contrast)
			{
				const float t = m_settings.ui.reduceMotion ? 0.0f : (float)m_now;
				const float ax = 1500 + 160 * std::sin(t * 0.11f), ay = 260 + 90 * std::cos(t * 0.13f);
				const float bx = 380 + 140 * std::cos(t * 0.09f), by = 900 + 70 * std::sin(t * 0.12f);
				const float cx = 1100 + 260 * std::sin(t * 0.07f + 1.3f), cy = 640 + 120 * std::cos(t * 0.08f);
				const uint32_t second = ui::Mix(accent, light ? 0xffffd28c : 0xffd27a5a, 0.55f);
				const uint32_t third = ui::Mix(accent, 0xffffffff, 0.35f);
				canvas.RadialGradient(screen, 0, ui::SetAlpha(accent, (light ? 0.30f : 0.40f) * appear), ui::SetAlpha(accent, 0), ax, ay, 900, 620);
				canvas.RadialGradient(screen, 0, ui::SetAlpha(second, (light ? 0.24f : 0.30f) * appear), ui::SetAlpha(second, 0), bx, by, 820, 560);
				// a third, slower, drifting under the rows, so the glass has light moving behind it
				canvas.RadialGradient(screen, 0, ui::SetAlpha(third, (light ? 0.16f : 0.22f) * appear), ui::SetAlpha(third, 0), cx, cy, 560, 380);
				canvas.Grain(screen, ui::SetAlpha(kText, 0.02f));
				// what the glass below shows, blurred: the backdrop with its light
				GlassBackdrop(canvas, 0);
			}
		}
		if (!InGameSettings())
			DrawBar(canvas, true);
		if (pages.empty())
			return;
		int page = 0;
		for (int i = 0; i < (int)pages.size(); i++)
			if (pages[i].id == m_page)
				page = i;
		m_page = pages[page].id;
		canvas.PushAlpha(appear);
		canvas.PushOffset(0, (1 - appear) * 30);

		// -- the rail: whose settings, then the pages, a liquid blob under the one open ---------------------
		const float railX = kSafeX - 8, railW = 360, top = 140, bottom = 1080 - 104;
		const Box rail{railX, top, railW, bottom - top};
		// a pane of glass (Glass: the backdrop under it blurred, bent and lit at its rim), on its shadow
		auto pane = [&](const Box& box, float radius, float tint, float shine) {
			if (!contrast)
				canvas.Shadow(box, radius, 22, 56, light ? 0x1c000000 : 0x4d000000);
			Glass(canvas, box, radius, contrast ? kInk2 : light ? ui::SetAlpha(0xffffffff, tint) : ui::SetAlpha(kInk1, tint), shine);
		};
		pane(rail, 34, light ? 0.46f : 0.34f, 0.9f);
		// tr: above Settings' pages: whose settings they are ({0}: Wii U, Nintendo 3DS or Nintendo DS)
		const std::string head = InGameSettings() ? m_gameName : TrF("{0} settings", SystemName(m_side));
		canvas.Text(Style(kOverlineStyle), rail.x + 30, rail.y + 28, head, Tertiary(), railW - 60, 1);
		const float railTop = rail.y + 66, railBottom = rail.Bottom() - 18, step = 58;
		// Playtendoh's pages, the same on every side, under the side's past a line and their heading
		int firstApp = -1;
		for (int i = 0; i < (int)pages.size() && firstApp < 0; i++)
			if (pages[i].app)
				firstApp = i;
		const float cut = firstApp > 0 ? step : 0;
		auto pageY = [&](int i) { return i * step + (firstApp > 0 && i >= firstApp ? cut : 0); };
		const float listHeight = pageY((int)pages.size() - 1) + step;
		m_railScroll.target = std::clamp(pageY(page) - (railBottom - railTop) * 0.5f, 0.0f, std::max(0.0f, listHeight - (railBottom - railTop)));
		m_railScroll.omega = kScrollOmega;
		m_railScroll.Update(m_dt);
		m_railBlob.target = railTop + pageY(page) - m_railScroll.value;
		m_railBlob.omega = m_settings.ui.reduceMotion ? 60.0f : 17.0f;
		m_railBlob.damping = m_settings.ui.reduceMotion ? 1.0f : kLiquidDamping;
		if (m_railBlob.value == 0)
			m_railBlob.Snap(m_railBlob.target);
		m_railBlob.Update(m_dt);
		canvas.PushClip({rail.x, railTop - 8, railW, railBottom - railTop + 8});
		{
			// the blob: stretched along its way while it moves, as a drop of liquid does
			const float stretch = std::min(26.0f, std::fabs(m_railBlob.velocity) * 0.022f);
			const Box blob{rail.x + 14, m_railBlob.value - stretch * 0.5f, railW - 28, step - 6 + stretch};
			canvas.Glow(blob, blob.h * 0.5f, 26, ui::SetAlpha(accent, m_onRows ? 0.18f : 0.34f));
			canvas.LinearGradient(blob, blob.h * 0.5f, ui::SetAlpha(accent, m_onRows ? 0.55f : 0.95f), ui::SetAlpha(ui::Mix(accent, 0xffffffff, 0.25f),
				m_onRows ? 0.45f : 0.85f), blob.x, blob.y, blob.Right(), blob.Bottom());
			canvas.LinearGradient(blob, blob.h * 0.5f, 0x40ffffff, 0x00ffffff, blob.x, blob.y, blob.x, blob.y + blob.h * 0.6f);
			if (!m_onRows)
				Focus(blob, blob.h * 0.5f);
		}
		if (firstApp > 0)
		{
			// the pages the same on every system (Language, Launcher, Backups...): the app's own
			const float cy = railTop + firstApp * step - m_railScroll.value;
			canvas.Rect({rail.x + 30, cy + 8, railW - 60, 1}, 0, ui::SetAlpha(kText, light ? 0.14f : 0.12f));
			canvas.Text(Style(kOverlineStyle), rail.x + 30, cy + 22, TrF("{0} settings", "Playtendoh"), Tertiary(), railW - 60, 1);
		}
		for (int i = 0; i < (int)pages.size(); i++)
		{
			const float ry = railTop + pageY(i) - m_railScroll.value;
			const bool selected = i == page;
			const Box item{rail.x + 14, ry, railW - 28, step - 6};
			const uint32_t ink = selected ? (ui::Luminance(accent) > 0.6f ? 0xff1b0f0a : 0xffffffff) : Secondary();
			canvas.Draw(pages[i].icon, {item.x + 20, item.CentreY() - 14, 28, 28}, ink);
			ui::TextStyle style = Style({23, selected ? ui::Weight::SemiBold : ui::Weight::Medium, 1.0f});
			style.minScale = 1;
			const ui::TextBlock text = m_fonts.Layout(style, pages[i].title, item.w - 76, 2);
			canvas.Text(text, item.x + 62, item.CentreY() - text.height * 0.5f, selected ? ink : kText);
		}
		canvas.PopClip();

		// -- the page: a big title, then its rows on one sheet of glass --------------------------------------
		const SettingsPage& current = pages[page];
		const bool sub = !m_subPage.empty();
		const float px = rail.Right() + 56, pw = 1920 - kSafeX - px;
		const float ty = top - 14;
		// above the title: the page a › page is on (the rail says whose settings these are)
		if (sub)
			canvas.Text(Style({20, ui::Weight::SemiBold, 1.0f, 3.0f, true}), px, ty + 6, current.title + "  ›", ui::SetAlpha(accent, 0.95f), pw, 1);
		canvas.Text(Style({60, ui::Weight::Bold, 1.05f, -0.8f}), px, ty + 32, sub ? m_subTitle : current.title, kText, pw, 1);
		canvas.Text(Style({22, ui::Weight::Regular, 1.3f}), px, ty + 104, sub ? m_subLine : current.subtitle, Secondary(), pw, 1);
		auto rows = SettingRows(m_page);
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		const float ry = ty + 152;
		const float rowH = 104;
		const float available = bottom - ry;
		const float content = rows.size() * rowH;
		const float most = std::max(0.0f, content - available + 16);
		const std::string scrolled = m_page + "/" + m_subPage;
		m_rowsScroll.target = std::clamp((m_settingRow + 1.6f) * rowH - available, 0.0f, most);
		m_rowsScroll.omega = m_settings.ui.reduceMotion ? 60.0f : kScrollOmega;
		if (m_rowsScrollPage != scrolled)
		{
			m_rowsScroll.Snap(m_rowsScroll.target);
			m_rowBlob.Snap(m_settingRow * rowH);
			m_rowsScrollPage = scrolled;
		}
		m_rowsScroll.Update(m_dt);
		const float scroll = m_rowsScroll.value;
		const Box sheet{px, ry, pw, std::min(available, std::max(rowH, content + 8))};
		if (!rows.empty())
		{
			// the rows' sheet on a stack: a second pane peeking out under its foot, where there is room
			if (!contrast && sheet.Bottom() + 18 <= bottom + 12)
				pane({sheet.x + 30, sheet.y + 18, sheet.w - 60, sheet.h}, 30, light ? 0.30f : 0.24f, 0.5f);
			pane(sheet, 30, light ? 0.52f : 0.40f, 1.0f);
		}
		canvas.PushClip(sheet, 30);
		const float labelScale = LabelScale(rows, pw);
		// the highlight: gliding to the focused row, stretched while it moves
		m_rowBlob.target = m_settingRow * rowH;
		m_rowBlob.omega = m_settings.ui.reduceMotion ? 60.0f : 18.0f;
		m_rowBlob.damping = m_settings.ui.reduceMotion ? 1.0f : kLiquidDamping;
		m_rowBlob.Update(m_dt);
		if (m_onRows && !rows.empty())
		{
			const float stretch = std::min(22.0f, std::fabs(m_rowBlob.velocity) * 0.02f);
			const Box blob{px + 8, ry + 4 + m_rowBlob.value - scroll - stretch * 0.5f, pw - 16, rowH - 8 + stretch};
			canvas.Glow(blob, 22, 24, ui::SetAlpha(accent, light ? 0.20f : 0.28f));
			canvas.Rect(blob, 22, ui::SetAlpha(accent, light ? 0.16f : 0.24f));
			canvas.LinearGradient(blob, 22, ui::SetAlpha(0xffffffff, light ? 0.55f : 0.14f), 0x00ffffff, blob.x, blob.y, blob.x, blob.y + blob.h * 0.7f);
			canvas.Ring(blob, 22, 2.0f, ui::SetAlpha(accent, 0.85f));
		}
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const Box box{px + 8, ry + 4 + i * rowH - scroll, pw - 16, rowH - 8};
			if (box.Bottom() < ry - rowH || box.y > sheet.Bottom() + rowH)
				continue;
			const bool focused = m_onRows && i == m_settingRow;
			if (i > 0)
				canvas.Rect({px + 32, box.y - 4, pw - 64, 1}, 0, ui::SetAlpha(kText, light ? 0.08f : 0.07f));
			DrawRow(canvas, rows[i], box, focused, labelScale, true);
			if (focused && rows[i].kind == Row::Kind::Hold)
				HoldRing(canvas, box, m_holdProgress);
		}
		canvas.PopClip();
		// what the page says under its rows: the remote screen's card, About's support card, its words
		const float under = sheet.Bottom() + 22;
		const std::string& shown = sub ? m_subPage : m_page;
		if (shown == "remote" && m_settings.remoteScreen && !InGameSettings())
			DrawRemoteScreen(canvas, {px, std::min(under, bottom - 372), pw, 372});
		else
		{
			const std::string panel = PanelText(shown);
			float textW = pw;
			if (m_page == "about")
			{
				const float cardW = 380;
				DrawSupport(canvas, {px + pw - cardW, std::min(under, bottom - 300), cardW, std::max(300.0f, std::min(560.0f, bottom - under))});
				textW = pw - cardW - 40;
			}
			if (!panel.empty() && under < bottom - 30)
			{
				const ui::TextStyle style = Style({m_page == "about" ? 20.0f : 21.0f, ui::Weight::Regular, 1.45f});
				const int lines = std::max(1, (int)((bottom - under) / (style.size * style.lineHeight)));
				canvas.Text(style, px + 8, under, panel, Secondary(), textW - 8, lines);
			}
		}
		canvas.PopOffset();
		canvas.PopAlpha();
	}

}
