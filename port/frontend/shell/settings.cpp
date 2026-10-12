// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Settings (docs/UI-REDESIGN.md, 6.5): what the side you are on can change, found at a
// glance. One short list of pages with icons down the left, the side's own and the few every side
// shares, and nothing of another side; the page's rows across the rest of the screen, each a control
// (a toggle, a slider, pips, segments, swatches), the focused one with a line saying what it does, its
// longer help behind Triangle. Game settings are the same pages, scoped to one game: only what a game
// can have its own of, each value its side's (Default) until changed, Square back to Default.

#include "shell_internal.h"
#include "../actions.h"
#include "../sound.h"
#include "../../app/boxart.h"
#include "../../app/pack_updates.h"
#include "../../app/paths.h"
#include "../../app/updates.h"
#include "../../app/usb_devices.h"
#include "../../azahar/azahar.h"
#include "../../azahar/controls.h"
#include "../../azahar/library.h"
#include "../../melonds/library.h"
#include "../../melonds/melonds.h"
#include "../../ps5/privilege.h"
#include "../../ps5/display.h"
#include "../../ps5/log.h"
#include "../../ps5/pad.h"
#include "../../stream/stream.h"

#include <algorithm>
#include <cmath>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		// tr: the 3DS borders' names (artwork around the screens)
		constexpr const char* kBorderThemes[] = {TrMarkC("border", "None"), TrMarkC("border", "Midnight"), TrMarkC("border", "Waves"),
			TrMarkC("border", "Aurora"), TrMarkC("border", "Shell"), "PS5CEMU-HAR"};
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
		constexpr const char* kUpscaleFilters[] = {TrMark("Linear"), TrMark("Bicubic"), TrMark("Bicubic Hermite"), TrMark("Nearest neighbour"), TrMark("AMD FSR 1")};
		constexpr const char* kMusic[] = {"setup", "off"};
		constexpr const char* kMusicNames[] = {TrMark("Setup theme"), TrMark("Off")};
		constexpr int kHolds[] = {400, 600, 800, 1000, 1200, 1500};
		constexpr int kClocks[] = {25, 50, 75, 100, 125, 150, 200, 300, 400}; // the 3DS's CPU clock, as its in-game menu has them
		// tr: how the DS's screens are scaled to the TV: the filters' names
		constexpr const char* kDsFilters[] = {TrMark("Sharp"), TrMark("Smooth"), TrMark("Square pixels")};

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
		[[maybe_unused]] constexpr const char* kThemeWords[] = {TrMark("Link Jack"), TrMark("Classic (3.0)"), TrMark("Midnight"), TrMark("Phantom"), TrMark("Terminal"),
			TrMark("Glass over each side's colours, as the design has it."), TrMark("3.0.0's launcher: deep blue panels, flatter, a little squarer."),
			TrMark("True black behind everything and nothing moving: for OLED screens."),
			TrMark("Black, white and one loud colour per side, sharp corners, bands slashing across."),
			TrMark("A hacker's console: phosphor on near-black, square corners, a grid with a scan line."), TrMark("Meyer Oscar"),
			TrMark("Meyer Oscar Light"), TrMark("Your recently played games first, then your whole library."),
			TrMark("Meyer Oscar with a light background.")};

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

		// the border names' context: kBorderThemes' first five
		std::vector<std::string> BorderNames()
		{
			std::vector<std::string> names;
			for (size_t i = 0; i < std::size(kBorderThemes); i++)
				names.push_back(i + 1 < std::size(kBorderThemes) ? TrC("border", kBorderThemes[i]) : kBorderThemes[i]);
			return names;
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
		// the side's pages, as docs/UI-REDESIGN.md 6.5 lists them; the shared ones on every side
		std::vector<SettingsPage> pages;
		// the menus' language first, with a globe, which reads in any language (docs/UI-REDESIGN.md 7.7)
		pages.push_back({"ui-language", Tr("Language"), Tr("The language of the menus: the PS5's own, unless you choose another."), Icon::Globe});
		const char* every = IsWiiU() ? Tr("For every Wii U game.") : Is3ds() ? Tr("For every 3DS game.") : Tr("For every DS game.");
		pages.push_back({"graphics", Tr("Graphics"), every, Icon::Monitor});
		if (!IsWiiU())
			pages.push_back({"screens", Tr("Screens"), every, Icon::Screens});
		pages.push_back({"audio", Tr("Audio"), Tr("The games' sound, and the launcher's."), Icon::Speaker});
		pages.push_back({"controls", Tr("Controls"), IsWiiU() ? Tr("What each player's DualSense is to the Wii U.") : Is3ds() ? Tr("The DualSense as the 3DS.") : Tr("The DualSense as the DS."),
			Icon::Pad});
		pages.push_back({"remote", Tr("Remote touch screen"), Tr("For every side. From dolphin78's idea and prototype."), Icon::Phone});
		if (IsWiiU())
			pages.push_back({"usb", Tr("USB devices"), Tr("Cemu's emulated portals."), Icon::Usb});
		if (!IsWiiU())
			pages.push_back({"system", Tr("System"), Is3ds() ? Tr("The emulated 3DS's region, language and speed.") : Tr("The emulated DS's language and BIOS."), Icon::Chip});
		pages.push_back({"files", Tr("Game files"), IsWiiU() ? Tr("Where your Wii U games are, and installs into the Wii U's storage.") :
										Is3ds()		 ? Tr("Where your 3DS games are, and installs into the 3DS's storage.") :
													   Tr("Where your DS games are, and where their saves go."),
			Icon::Folder});
		// the Wii U's accounts and every side's saves (6.13); a handheld has one player, so its page is saves alone
		pages.push_back({"saves", IsWiiU() ? Tr("Accounts and saves") : Tr("Saves"),
			IsWiiU() ? Tr("Each account has its own saves of every game. Backups hold every side's.") : Tr("Backups hold every side's saves."),
			Icon::Save});
		if (Is3ds())
			pages.push_back({"artic", Tr("Artic Base"), Tr("Play from your 3DS, over your network."), Icon::Artic});
		pages.push_back({"online", Tr("Online"), IsWiiU() ? Tr("Box art, graphic packs and PS5CEMU-HAR itself.") : Tr("Box art and PS5CEMU-HAR itself."), Icon::Globe});
		pages.push_back({"launcher", Tr("Launcher"), Tr("How it looks, and how it reads."), Icon::Sparkle});
		pages.push_back({"diagnostics", Tr("Diagnostics"), Tr("The Setup check, logs, caches and what this session is."), Icon::Pulse});
		// the version always in sight, whatever the credits' length in a language
		pages.push_back({"about", TrC("app", "About"), std::string(Tr("Who made what.")) + " · " + Tr("Version") + " " + ps5update::Readable(PS5CEMU_VERSION),
			Icon::Info});
		if (!InGameSettings())
			return pages;
		// a game's settings: only the pages with something a game can have its own of (SideRows' rows
		// with a setting: Graphics, Screens, Audio and System)
		std::vector<SettingsPage> game;
		for (const SettingsPage& page : pages)
			if (page.id == "graphics" || page.id == "screens" || page.id == "audio" || page.id == "system")
				game.push_back({page.id, page.title, TrF("For {0} only. Its other settings are its side's.", m_gameName), page.icon});
		return game;
	}

	void Shell::SettingsOpen(const std::string& page, bool onRows, int row)
	{
		if (InGameSettings())
			GameSettingsClose();
		m_page = page;
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

	// -- the rows -------------------------------------------------------------------------------------

	std::vector<Row> Shell::SideRows(const std::string& page, const ps5settings::Launcher& settings)
	{
		using Kind = Row::Kind;
		const ps5settings::N3ds& hand = m_side == System::Nds ? settings.nds : settings.n3ds;
		std::vector<Row> rows;
		auto toggle = [&](const char* id, const std::string& label, bool on, const std::string& description, const std::string& help) {
			Row row;
			row.kind = Kind::Toggle;
			row.id = id;
			row.label = label;
			row.on = on;
			row.value = on ? Tr("On") : Tr("Off");
			row.description = description;
			row.help = help;
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
			rows.push_back(row);
			return &rows.back();
		};
		// a Wii U row Cemu's core must be up for
		auto cemu = [&](Row* row) {
			row->needsCore = true;
			if (!m_status.coreReady)
			{
				row->dimmed = true;
				row->description = m_status.coreStarting ? Tr("Cemu is still starting: a moment.") : Tr("Cemu did not start, so this cannot be changed.");
			}
			return row;
		};
		const std::string side = ps5launcher::SideName(m_side);
		auto own = [&](Row* row, const char* setting) { row->setting = setting; };

		if (page == "ui-language")
		{
			// Cross, Left or Right opens the list
			int index = 0;
			choice("ui-language", Tr("Language"), LanguageOptions(index), index, Tr("The menus' language. Game names and descriptions stay as they are."),
				Tr("PS5CEMU-HAR speaks the PS5's own language unless you choose another here; the first start's Setup check has the same "
				   "choice. Game names, their descriptions and graphic packs come from GameTDB, the games and the packs' authors, and stay as "
				   "they wrote them."));
		}
		else if (page == "graphics" && IsWiiU())
		{
			own(choice("upscaling", Tr("Upscaling to 4K"), Options(kUpscaleFilters), settings.upscaleFilter, Tr("How the picture is scaled to the TV."),
					Tr("Bicubic is sharp, Bicubic Hermite a little softer, Linear softer still; Nearest neighbour keeps pixels square. AMD FSR 1 keeps "
					   "edges clean for a game drawn below 4K, for a little more work by the GPU than Bicubic.")),
				"upscaleFilter");
			own(toggle("highframerate", Tr("120 Hz output"), settings.highFrameRate, Tr("For displays that take 120 Hz."),
					Tr("The 119.88 Hz mode, on displays that support it. Games still run at their own speed, and a frame that misses a refresh "
					"waits 8 ms for the next instead of 17.")),
				"highFrameRate");
			std::vector<std::string> pacing;
			for (int i = 1; i <= 3; i++)
				pacing.push_back(ps5display::FramePacingName(i, settings.highFrameRate));
			own(choice("framepacing", Tr("Frame pacing"), pacing, settings.framePacing - 1, Tr("Holds a game to an even frame rate."),
					Tr("Each frame stays on screen for at least two or three refreshes, so a game that cannot hold the display's rate runs at an "
					"even one: 60 or 40 fps with 120 Hz output, 30 or 20 without. 60 fps with 120 Hz output suits a game at 4K that sometimes "
					"drops under 60; 30 fps at 60 Hz suits one at 8K. Also in the in-game menu."),
					Kind::Segmented),
				"framePacing");
			own(toggle("overlay", Tr("Performance overlay"), settings.overlay, Tr("Frame rate, CPU and memory in a corner."),
					Tr("Frames per second, CPU and memory use in the top left corner, as Cemu shows them. Also in the in-game menu.")),
				"overlay");
			toggle("shadernotices", Tr("Shader notices"), settings.shaderNotices, Tr("Cemu's \"compiled shaders\" notes over a game."),
				Tr("The notes Cemu shows in a corner while a game builds its shaders and pipelines (\"Loaded 1200 shaders\"). Off hides "
				   "them; the shaders are built either way. Also in the in-game menu."));
			own(toggle("async", Tr("Async shader compile"), settings.asyncShaders, Tr("No stutter while new shaders build."),
					Tr("On, a new shader is built while the game carries on, so it does not stutter, but some things may be missing for a moment. "
					"Off, the game waits for each one: stutter, but nothing drawn wrong.")),
				"asyncShaders");
		}
		else if (page == "graphics" && Is3ds())
		{
			own(choice("resolution", Tr("Internal resolution"), Options(kResolutions), hand.resolution - 1, Tr("Higher is sharper and asks more of the GPU."),
					Tr("How large the 3DS's 3D scenes are drawn before they are scaled to the TV. Higher is sharper and asks more of the GPU, and "
					"each time a game reads a picture back the wait grows with it."),
					Kind::Stepper),
				"resolution");
			own(choice("filter", Tr("Texture filter"), TextureFilterNames(), FilterIndex(hand.textureFilter), Tr("Smooths textures as they are scaled up."),
					Tr("Smooths the game's textures as they are scaled up; None keeps them as the 3DS draws them. A filter redraws every texture "
					"at the internal resolution: at high resolutions it is the costliest setting. If a game stutters, try None first.")),
				"textureFilter");
			own(toggle("customtextures", Tr("Custom textures"), hand.customTextures, Tr("Texture packs from azahar/load/textures."),
					Tr("Texture packs in /data/ps5cemu/azahar/load/textures/<title ID>, as the desktop Azahar loads them.")),
				"customTextures");
			own(toggle("outlinefix", Tr("Outline fix"), hand.invariantPosition, Tr("For black dots moving over outlined 3D models."),
					Tr("Computes every 3D position the same way twice, as Azahar does on Apple's GPUs, so the outlines games such as Pokémon X "
					   "and Y draw around their models stop showing moving black dots (#23). On by default for Pokémon X, Y, Omega Ruby and "
					   "Alpha Sapphire; for other games, turn it on in their game settings if they show the same. Applies to the next game.")),
				"invariantPosition");
		}
		else if (page == "graphics")
		{
			own(choice("dsfilter", Tr("Screen filter"), Options(kDsFilters), hand.dsFilter, Tr("How the DS's screens are scaled."),
					Tr("How the DS's 256 x 192 screens are scaled to the TV. Sharp keeps every pixel square and even at any size; Smooth blurs "
					"them together; Square pixels takes the nearest, uneven where the size is not a whole number. The in-game menu changes it "
					"too."),
					Kind::Segmented),
				"dsFilter");
			const bool executable = ps5privilege::Current().executable;
			own(toggle("dsjit", Tr("Recompiler"), hand.dsJit,
					executable ? "melonDS's JIT: much faster than its interpreter." : Tr("No executable memory: the interpreter runs."),
					Tr("Runs the DS's two CPUs on melonDS's recompiler, in the executable memory the PS5 gives (Diagnostics says whether it "
					"does), much faster than its interpreter. Off: the interpreter, slower, for a game the recompiler gets wrong. Applies to "
					"the next game.")),
				"dsJit");
		}
		else if (page == "screens")
		{
			own(choice("layout", Tr("Screen layout"), Options(kLayouts), hand.layout, Tr("How the two screens share the TV."),
					Tr("One above the other, the top one alone, the top one large with the bottom one beside it, or the two side by side. In a "
					"game, touchpad click + R1 goes to the next.")),
				"layout");
			own(choice("border", Tr("Border"), Options(kBorderThemes), hand.border, Tr("Artwork around the screens."),
					Tr("Artwork around the screens, never over them. It follows every layout, and the in-game menu changes it too."),
					Kind::Swatches),
				"border");
		}
		else if (page == "audio")
		{
			own(slider("volume", IsWiiU() ? Tr("Wii U games' volume") : Is3ds() ? "3DS games' volume" : Tr("DS games' volume"),
					IsWiiU() ? settings.volume : hand.volume, 100, Tr("The games' sound."), Tr("The games' sound on this side. Left and Right change it by 10%.")),
				"volume");
			if (IsWiiU())
				toggle("gamepadspeaker", Tr("GamePad speaker"), settings.gamePadSpeaker, Tr("The Wii U GamePad's own sound, on the DualSense."),
					Tr("The sounds games play on the Wii U GamePad's speaker, from player 1's DualSense speaker. Many games send their whole sound "
					"there too, so it is off unless you want it. Applies to the next game."));
			const int music = (int)(std::find(std::begin(kMusic), std::end(kMusic), settings.music) - std::begin(kMusic));
			choice("music", Tr("Launcher music"), Options(kMusicNames), music, Tr("The music under the menus."),
				Tr("The launcher's own music, in the spirit of a console's setup screen, or none. Every side has the same."));
			slider("musicvolume", Tr("Music volume"), settings.musicVolume, 100, Tr("How loud the menus' music is."),
				Tr("How loud the launcher's music is. Left and Right change it by 10%."));
			toggle("menusounds", Tr("Menu sounds"), settings.menuSounds, Tr("The sounds of moving and choosing."),
				Tr("The launcher's sounds as you move, choose and go back."));
		}
		else if (page == "controls")
		{
			toggle("rumble", Tr("Vibration"), settings.rumble, Tr("The DualSense's rumble, in games and in the launcher."),
				Tr("Off, the DualSense never rumbles: not for a game's vibration, nor for the launcher's. Every side has the same."));
			{
				int index = 0;
				auto options = TurboOptions(IsWiiU() ? settings.turbo : hand.turbo, index);
				choice("turbo", Tr("Turbo button"), std::move(options), index, Tr("Held, it presses again and again by itself."),
					Tr("Choose a DualSense button: while you hold it in a game, it presses and lets go by itself about fifteen times a second, "
					   "for games that make you mash. The button keeps whatever it is mapped to. None turns it off. Every player on this side "
					   "has it."));
			}
			if (!IsWiiU())
			{
				const bool aOnCircle = ps5azahar::MappedInput(hand, ps5azahar::Button::A) == ps5emu::PadInput::Circle;
				choice("abbuttons", Tr("A and B"), {Tr("A on Circle"), Tr("A on Cross")}, aOnCircle ? 0 : 1,
					Tr("Where A is: on Circle, as on the console, or on Cross."),
					Tr("A on Circle and B on Cross, where the console has them, or A on Cross and B on Circle, as PlayStation games confirm. X "
					   "and Y swap to match. Also in the in-game menu."),
					Kind::Segmented);
			}
			if (IsWiiU())
			{
				for (int player = 0; player < ps5pad::kMaxPlayers; player++)
				{
					const auto controls = m_status.coreReady ? ps5emu::GetPlayerControls(player) : ps5emu::PlayerControls{};
					Row* row = action(Kind::Link, "player", TrF("Player {0}", player + 1), m_status.coreReady ? TypeName(controls.type) : "",
						controls.connected ? Tr("Cross: this player's controller, motion and buttons.") : Tr("No DualSense for this player yet."),
						Tr("What the game sees in this player's hands, and its motion, vibration, deadzones and buttons. Player 1 is the signed-in "
						"user who started the app; the other signed-in users' DualSenses are players 2 to 4."));
					row->index = player;
					row->dimmed = m_status.coreReady && !controls.connected;
					cemu(row);
				}
			}
			else
			{
				if (Is3ds())
					toggle("motion", Tr("Motion controls"), hand.motion, Tr("The DualSense's motion as the 3DS's."),
						Tr("The DualSense's gyroscope and accelerometer as the 3DS's own, for the games that aim or steer by tilting it."));
				slider("deadzone", Tr("Stick deadzone"), hand.deadzone, 50, Tr("How far a stick moves before it counts."),
					Is3ds() ? Tr("How far a stick moves before the game sees it, for the circle pad and the C-stick. Raise it if something drifts "
							  "when you let go of the stick; lower it for finer control.") :
							  Tr("How far the left stick moves before it presses the DS's D-pad. Raise it if a direction is pressed when you let go "
							  "of the stick."))
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
				action(Kind::Link, "buttons", Tr("Buttons"), TrP(mapped, "{0} button set", "{0} buttons set"), Tr("Which DualSense button is which."),
					Is3ds() ? Tr("Which DualSense button is which of the 3DS's. A is on Circle and B on Cross by default, where the 3DS has them; "
							  "the circle pad is the left stick, the C-stick the right one, and the touchpad the touch screen.") :
							  Tr("Which DualSense button is which of the DS's. A is on Circle and B on Cross by default, where the DS has them; the "
							  "left stick is a second D-pad, the touchpad the touch screen, and R3 held blows into the microphone."));
				action(Kind::Hold, "resetcontrols", Tr("Reset to defaults"), m_message.empty() ? Tr("Hold Cross") : m_message,
					Is3ds() ? Tr("Default buttons, motion and deadzone.") : Tr("Default buttons and deadzone."), Tr("The default buttons, motion and deadzone."));
			}
		}
		else if (page == "remote")
		{
			const char* screen = IsWiiU() ? Tr("The GamePad's screen") : Is3ds() ? Tr("The bottom screen") : Tr("The touch screen");
			toggle("remote", Tr("Remote touch screen"), settings.remoteScreen, TrF("{0} in a phone's or tablet's browser, with touch.", screen),
				TrF("{0} in any browser on the PS5's network, a phone's, a tablet's or a computer's, with nothing to install, and its "
							"touches sent back to the game. Scan the code shown here, or open the address. The DualSense stays the buttons, "
							"and the sound stays on the PS5.\nWhile a browser shows it, the PS5 copies that screen up to 30 times a second, so "
							"a game can run a little slower; with no browser, it costs nothing. Any device on your network can open the page, "
							"so turn it off on a network you share.\nOne switch for every side (the GamePad's screen is player 1's), also in "
							"a game's menu (touchpad click + Options). From dolphin78's idea and prototype (#38).",
					screen));
		}
		else if (page == "usb")
		{
			for (ps5usb::Device device : ps5usb::kDevices)
			{
				Row* row = toggle("usb", ps5usb::Name(device), m_status.coreReady && ps5usb::Enabled(device),
					device == ps5usb::Device::Skylanders ? Tr("For the Skylanders games.") :
					device == ps5usb::Device::Infinity	 ? Tr("For Disney Infinity 3.0.") :
														   Tr("For LEGO Dimensions."),
					Tr("Cemu's emulated portal, plugged in as a game starts. In the game, the menu's USB devices category puts figures on it: dumps "
					"in ") + ps5usb::Folder(device) + ". A real portal on the PS5's USB is not reached.");
				row->index = (int)device;
				cemu(row);
			}
		}
		else if (page == "system" && Is3ds())
		{
			// Azahar looks for it in its own files, which only the 3DS side opens
			ps5emu::Game home;
			const bool homeMenu = m_prepared == System::N3ds && ps5azahar::HomeMenu(hand.region, home);
			own(choice("region", Tr("Region"), Options(kRegions), hand.region + 1, Tr("The emulated 3DS's region."),
					Tr("Automatic takes each game's own region. A game made for another region may refuse to start or show other languages. "
					"Applies to the next game.")),
				"region");
			own(choice("language", Tr("Language"), Options(kLanguages), hand.language + 1, Tr("The emulated 3DS's language."),
					Tr("The language games that follow the console's show their text in. Applies to the next game.")),
				"language");
			std::vector<std::string> clocks;
			for (int clock : kClocks)
				clocks.push_back(fmt::format("{}%", clock));
			own(choice("cpuclock", Tr("CPU clock"), clocks, ClockIndex(hand.cpuClock), Tr("The emulated 3DS's CPU speed."),
					Tr("How fast the emulated CPU runs, against a 3DS's: higher can steady a game that slows down where a 3DS would not, and asks "
					"more of the PS5; lower can speed up one held back by its own pacing. 100% is a 3DS. Also in the in-game menu.")),
				"cpuClock");
			Row* row = action(Kind::Action, "homemenu", Tr("Home Menu"), homeMenu ? Tr("Start") : Tr("Run Artic Setup first"),
				Tr("The 3DS Home Menu, from your console's files."),
				Tr("Starts the 3DS Home Menu, once Artic Base's setup has copied your own console's system files (Artic Base)."));
			row->dimmed = !homeMenu;
		}
		else if (page == "system")
		{
			own(choice("language", Tr("Language"), Options(kLanguages), hand.language + 1, Tr("The emulated DS's language."),
					Tr("The language games that follow the console's show their text in, up to Chinese: the DS has no others (Korean and the "
					"rest are English). Applies to the next game.")),
				"language");
			const bool found = ps5melonds::OwnBiosFound();
			own(toggle("dsownbios", Tr("Your DS's BIOS"), hand.dsOwnBios, found ? Tr("Found in melonds/bios.") : Tr("Not found: melonDS's own run the games."),
					"bios7.bin, bios9.bin and firmware.bin, dumped from your own DS, in /data/ps5cemu/melonds/bios: melonDS runs on them when "
					"they are there and this is on. Without them, its own replacements (FreeBIOS) run most games as well. Applies to the next "
					"game."),
				"dsOwnBios");
		}
		else if (page == "files")
		{
			const std::string& folder = GamesFolder(m_side);
			action(Kind::Link, "folder", Is3ds() ? Tr("3DS games") : IsDs() ? Tr("DS games") : Tr("Wii U games"), ShortPath(folder, 34),
				Tr("Where your games are. Cross picks another."),
				IsWiiU() ? Tr("Your Wii U games: .wua, .wud, .wux, or folders with code, content and meta. Encrypted .wud and .wux need their keys "
						   "in /data/ps5cemu/keys.txt.") :
				Is3ds()	 ? Tr("Your 3DS games: .3ds or .cci, .cxi, .3dsx and Azahar's compressed dumps, decrypted, here or in the folders in it. "
						   "CIA files are installed (Install a CIA file). Encrypted dumps need the 3DS's aes_keys.txt in "
						   "/data/ps5cemu/azahar/sysdata.") :
						   Tr("Your DS games: .nds files, here or in the folders in it."));
			action(Kind::Action, "rescan", Tr("Look for games now"), m_scanning ? Tr("Looking…") : "", Tr("This side's folder, again."),
				Tr("Looks through this side's folder again, as it does each time the side opens."));
			if (IsWiiU())
			{
				const bool keys = IsFile(std::string(ps5paths::kRoot) + "/keys.txt");
				action(Kind::Action, "keys", Tr("Wii U disc keys"), keys ? "keys.txt found" : "keys.txt missing", Tr("Only encrypted .wud and .wux need them."),
					Tr("Encrypted .wud and .wux dumps need their disc keys in /data/ps5cemu/keys.txt, one a line. Decrypted dumps, .wua and "
					"folders need none."));
				cemu(action(Kind::Link, "install", Tr("Updates and DLC"), "", Tr("Found on your drives, each installed with one press."),
					Tr("Looks on every USB drive and extended storage plugged in, in the games folder and in /data for updates, DLC and games "
					   "(folders with code, content and meta), and lists them by game with what is installed now. Cross installs one, Square "
					   "every new one; each goes into the Wii U's storage (mlc01), as Cemu's Install game title, update or DLC does. Triangle "
					   "there chooses a folder anywhere else.")));
			}
			else if (Is3ds())
				action(Kind::Link, "installcia", Tr("Install a CIA file"), "", Tr("A game, an update or DLC."),
					Tr("Installs a CIA into the 3DS's storage, as Azahar's Install CIA does: an update or DLC goes with its game, and a game joins "
					"the library. Circle cancels while it runs."));
		}
		else if (page == "saves")
		{
			if (IsWiiU())
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
					settings.accountPerUser ? Tr("Chosen by your PS5 user: One account for each PS5 user is on.") :
											  Tr("The next game starts on these saves. A game already running keeps its own."),
					Tr("Each Wii U account has its own saves of every game, as on a Wii U. The next game you start uses the one shown here; a game "
					   "already running keeps the one it started with.")));
				if (settings.accountPerUser)
					account->dimmed = true;
				// tr: how many Wii U accounts there are, of the twelve a Wii U has at most
				cemu(action(Kind::Link, "newaccount", Tr("New account"), TrF("{0} of 12", (int)accounts.size()),
					Tr("Its own saves of every game, from the start."), Tr("Name it with the keyboard, ten letters at most. Games start on it from then on.")));
				cemu(action(Kind::Link, "renameaccount", TrF("Rename {0}", currentName), "", Tr("The account games start on."),
					Tr("The name games show for this account, ten letters at most. Its saves stay as they are.")));
				// deleting: Left and Right choose which, never the one games start on
				std::vector<std::string> others;
				for (const auto& a : accounts)
					if (a.id != current)
						others.push_back(a.name);
				int& which = m_holdChoice["deleteaccount"];
				which = others.empty() ? 0 : std::clamp(which, 0, (int)others.size() - 1);
				Row* remove = cemu(action(Kind::Hold, "deleteaccount", Tr("Delete an account"),
					others.empty() ? std::string(Tr("Only one")) : others[which] + "  ·  " + Tr("Hold Cross"),
					others.empty() ? Tr("There is only the one games start on.") : Tr("Left and Right choose which. Its saves stay in the Wii U's storage."),
					Tr("The account goes; its saves stay in the Wii U's storage (and in any backup), so making one again does not lose them. The "
					   "account games start on cannot be deleted: choose another first.")));
				remove->options = others;
				remove->index = which;
				if (others.empty())
					remove->dimmed = true;
				cemu(toggle("accountperuser", Tr("One account for each PS5 user"), settings.accountPerUser,
					Tr("Whoever starts PS5CEMU-HAR plays on their own saves."),
					Tr("The PS5 user who starts the app plays on a Wii U account of their own, made the first time with their PS5 name, so "
					   "brothers and sisters each keep their own saves just by starting the app from their own PS5 user.")));
			}
			// every side's saves, to a USB drive and back
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
			action(Kind::Action, "backup", Tr("Back up to USB"), backupValue, Tr("Every side's saves, save states and Wii U accounts."),
				Tr("Copies the Wii U's saves (every account's, with the accounts), the 3DS's saves, extra data and save states, and the DS's "
				   "saves and save states to PS5CEMU-HAR-saves on the first USB drive plugged in, in a folder named by the date and time. Never "
				   "the games, so a backup stays small."));
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
				Tr("Puts the chosen backup's saves back where they came from. The saves there now are moved first to "
				   "/data/ps5cemu/saves-before, so nothing is lost; saves the backup does not have stay as they are."));
			restore->options = backups;
			restore->index = chosen;
			if (backups.empty())
				restore->dimmed = true;
		}
		else if (page == "artic")
			action(Kind::Link, "artic", Tr("Artic Base"), settings.n3ds.articAddress, Tr("Play a game from your 3DS, and set up from it."),
				Tr("Artic Base plays a game from a 3DS on your network, its saves staying on the 3DS; Artic Setup copies a 3DS's system files "
				"into Azahar, for the Home Menu and the games that need them."));
		else if (page == "online")
		{
			toggle("boxart", Tr("Box art from GameTDB"), settings.boxArt, Tr("Covers for the library, downloaded once."),
				Tr("The first time a game shows up, its cover is downloaded from GameTDB (art.gametdb.com) by the ID on its box. Off: nothing "
				"more is downloaded."));
			// downloading the covers now: the missing ones, or every one again
			{
				int missing = 0;
				for (const Game& game : m_games)
					missing += !game.entry.game.gameId.empty() && game.boxArt.empty();
				const size_t pending = ps5boxart::Pending();
				const std::string status = pending ? TrP((long long)pending, "Downloading: {0} left", "Downloading: {0} left") :
					ps5boxart::Answered() == 0 ? std::string(Tr("GameTDB did not answer: is the PS5 online?")) :
					missing				   ? TrP(missing, "{0} game without box art", "{0} games without box art") :
											 std::string(Tr("Every game has box art"));
				action(Kind::Action, "boxartmissing", Tr("Download missing box art"), status, Tr("Tries again for every game without a cover."),
					Tr("Asks GameTDB again for the cover of every game on this side that has none, including games it had no cover for "
					   "before. Covers are downloaded in the background while you use the launcher."));
				action(Kind::Hold, "boxartall", Tr("Download all box art again"), m_message.empty() ? Tr("Hold Cross") : m_message,
					Tr("Replaces every cover on this side with a fresh download."),
					Tr("Downloads the cover of every game on this side again, replacing the ones you have. Useful when a cover is wrong or "
					   "looks broken."));
			}
			if (IsWiiU())
				action(Kind::Action, "packs", Tr("Community graphic packs"), PacksStatus(), Tr("Cross checks GitHub for newer ones."),
					Tr("Cemu's community graphic packs, from GitHub's latest release when it is newer than the ones installed. The packs bundled "
					"with the app stay as the fallback."));
			action(Kind::Action, "appupdate", Tr("PS5CEMU-HAR updates"), AppUpdateStatus(), Tr("Cross checks GitHub for a newer version."),
				Tr("PS5CEMU-HAR asks GitHub for its latest release each time it starts. Cross asks again, or installs the newer version it found: "
				   "the release's files are downloaded, checked and put in place of these, and the app starts again. Your games, saves and "
				   "settings stay as they are."));
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
					const std::string name = theme.id.rfind("user:", 0) == 0 ? theme.name : std::string(Tr(theme.name.c_str()));
					// tr: a theme that is still being tried out: "Phantom (experimental)"
					names.push_back(theme.experimental ? TrF("{0} (experimental)", name) : name);
					if (theme.id == settings.ui.theme)
						index = i;
				}
				const ui::Theme& current = m_themes.empty() ? ui::BuiltInThemes().front() : m_themes[std::min(index, (int)m_themes.size() - 1)];
				choice("theme", Tr("Theme"), names, index,
					current.id.rfind("user:", 0) == 0 ? current.description : std::string(Tr(current.description.c_str())),
					TrF("How the launcher looks: its colours, its corners and what moves behind the menus. Phantom and Terminal are experiments "
						"of their own. Make your own as a JSON file in {0}/themes, starting from any of these: the text under this page says "
						"how. Only the launcher changes: the in-game menus and the games stay as they are.",
						ps5paths::kRoot));
			}
			// the sides the bar offers: any mix, at least one
			toggle("side-wiiu", Tr("Wii U side"), SideShown(System::WiiU), Tr("Cemu's games, in the bar."),
				Tr("Whether the bar offers the Wii U side (Cemu). Hidden, its games and settings stay as they are, and turning it on again "
				   "brings them back. At least one side stays on."));
			toggle("side-3ds", Tr("3DS side"), SideShown(System::N3ds), Tr("Azahar's games, in the bar."),
				Tr("Whether the bar offers the 3DS side (Azahar). Hidden, its games and settings stay as they are, and turning it on again "
				   "brings them back. At least one side stays on."));
			toggle("side-ds", Tr("DS side"), SideShown(System::Nds), Tr("melonDS's games, in the bar."),
				Tr("Whether the bar offers the DS side (melonDS). Hidden, its games and settings stay as they are, and turning it on again "
				   "brings them back. At least one side stays on."));
			toggle("pictures", Tr("Game pictures behind menus"), settings.ui.gamePictures, Tr("The focused game's cover, softened, behind the screens."),
				Tr("The focused game's picture fills the screen behind the menus, softened and dimmed, so moving the focus changes the whole "
				"screen. Off: the game's colours only."));
			toggle("largertext", Tr("Larger text"), settings.ui.largerText, Tr("Every size of text a step up."),
				Tr("Every style of text a step larger: captions from 20 to 23, reading text from 26 to 30."));
			toggle("highcontrast", Tr("High contrast"), settings.ui.highContrast, Tr("Opaque panels, full-strength text, a thicker ring."),
				Tr("Opaque panels instead of glass, all text at full strength, a thicker focus ring and no grain or pictures behind the menus."));
			toggle("reducemotion", Tr("Reduce motion"), settings.ui.reduceMotion, Tr("Fades in place of movement."),
				Tr("Springs become short fades; nothing drifts, breathes or rises behind the menus."));
			action(Kind::Link, "makeityours", Tr("Make it yours"), "", Tr("The first start's choices again: sides, theme, buttons."),
				Tr("The page the first start opens on: the language, the sides, the theme, A and B, the music, vibration and the games' "
				   "pictures, on one page."));
			int hold = 2;
			for (int i = 0; i < 6; i++)
				if (kHolds[i] <= settings.ui.holdMs)
					hold = i;
			choice("holdms", Tr("Hold to confirm"), Options({"0.4 s", "0.6 s", "0.8 s", "1.0 s", "1.2 s", "1.5 s"}), hold,
				Tr("How long Cross is held for what cannot be undone."),
				Tr("Clearing caches, Artic Setup and resetting controls are held to confirm, never pressed twice. This is how long."), Kind::Stepper);
		}
		else if (page == "diagnostics")
		{
			action(Kind::Link, "setupcheck", Tr("Setup check"), "", Tr("Storage, recompilers, games, keys and box art, checked."),
				Tr("Each check has a status, what it means and what to do. It is also shown on the first start and whenever a check fails as "
				"the app starts."));
			action(Kind::Action, "copylogs", Tr("Copy logs to USB"), m_diagnosticsDone[0], Tr("The logs a report needs, onto a USB drive."),
				Tr("Copies the last five sessions' logs and the settings into a dated folder on a USB drive, to attach to a report."));
			if (IsWiiU())
				action(Kind::Hold, "clearcaches", Tr("Clear shader caches"), m_diagnosticsDone[1].empty() ? Tr("Hold Cross") : m_diagnosticsDone[1],
					Tr("For a game that crashes on a bad cache."), Tr("Deletes Cemu's shader caches. Games build them again as they run: the first minutes stutter."));
			else if (Is3ds())
				action(Kind::Hold, "clearcaches", Tr("Clear shader caches"), m_diagnosticsDone[2].empty() ? Tr("Hold Cross") : m_diagnosticsDone[2],
					Tr("For a game that crashes on a bad cache."),
					Tr("Deletes Azahar's shader caches. Games build them again as they run: the first minutes stutter."));
		}
		(void)side;
		return rows;
	}

	std::vector<Row> Shell::SettingRows(const std::string& page)
	{
		if (!InGameSettings())
			return SideRows(page, m_settings);
		// a game's: its rows only, each its own value or the side's (Default)
		const std::string side = ps5launcher::SideName(m_side);
		const auto found = m_settings.games.find(ps5settings::GameKey(side, m_gameTitle));
		std::vector<Row> rows;
		for (Row& row : SideRows(page, m_gameView))
		{
			if (row.setting.empty())
				continue;
			row.fromSide = found == m_settings.games.end() || !found->second.count(row.setting);
			rows.push_back(std::move(row));
		}
		if (!rows.empty())
		{
			Row reset;
			reset.kind = Row::Kind::Hold;
			reset.id = "resetgame";
			reset.label = Tr("Back to defaults");
			reset.value = m_message.empty() ? Tr("Hold Cross") : m_message;
			reset.description = Tr("Every setting of this game its side's again.");
			reset.help = Tr("Clears this game's own settings: it starts with its side's again, as every game without settings of its own does.");
			rows.push_back(reset);
		}
		return rows;
	}

	std::string Shell::PanelText(const std::string& page) const
	{
		if (InGameSettings())
			return {};
		if (page == "diagnostics")
		{
			std::string text;
			for (const auto& line : m_status.diagnostics)
				text += line + "\n";
			return text;
		}
		if (page == "launcher")
			return TrF("Your own theme: a file such as sunset.json in {0}/themes, read as the app starts, then listed under Theme by its name:\n"
					   "{{\"name\": \"Sunset\", \"base\": \"midnight\", \"colours\": {{\"wiiu\": \"#ff7a59\", \"ink-0\": \"#120806\"}}, "
					   "\"corners\": 0.5, \"motif\": \"slashes\"}}\n"
					   "base: a theme to start from (classic, midnight, phantom, terminal). colours: ink-0 to ink-2 (the background), text, "
					   "wiiu, n3ds and nds (each side's), glass, line, good, warn, bad and focus, as #rrggbb. corners: 0 square to 1.5 round. "
					   "motif: side, none, slashes or grid.",
				ps5paths::kRoot);
		if (page == "files" && IsDs())
			return Tr("Saves: /data/ps5cemu/melonds/saves (a .sav beside the game is taken the first time). Save states: melonds/states. Cheats: "
				   "melonds/cheats/<game file>.mch, melonDS's format. DSi games, the DS's wireless and its GBA slot are not there.");
		if (page == "remote" && !m_settings.remoteScreen)
			return Tr("On, a code here opens the game's second screen in a phone's browser. Nothing to install; the PS5 and the phone on the "
				   "same network.");
		if (page == "usb")
			return Tr("Figure dumps go in /data/ps5cemu/figures: skylanders, infinity and dimensions. Switch a portal on here, start the game, then "
				   "put figures on it from the in-game menu (touchpad click + Options > USB devices).");
		if (page == "about")
			return Tr("Cemu, the Wii U emulator, by the Cemu team and its contributors; RADV on the PS5 by Mihawk-99 and mpereiraesaa; the "
				   "community graphic packs' authors.\nAzahar, the 3DS emulator, by the Azahar team and the Citra contributors before them; "
				   "Mihawk-99's PS5 port of it and dynarmic.\nmelonDS, the DS emulator, by Arisotura and the melonDS team.\nBox art and game "
				   "information: GameTDB. The font: Lexend.\nThe remote touch screen: dolphin78's idea and prototype.\nAn unofficial port, not affiliated with the Cemu, Azahar or melonDS teams, Nintendo "
				   "or Sony.\n\nWii U games: /data/ps5cemu/games, storage and saves: /data/ps5cemu/mlc01, keys: /data/ps5cemu/keys.txt\n3DS "
				   "games: /data/ps5cemu/azahar/games, storage: /data/ps5cemu/azahar/sdmc\nDS games: /data/ps5cemu/melonds/games, saves: "
				   "/data/ps5cemu/melonds/saves\n\nVersion ") +
				ps5update::Readable(PS5CEMU_VERSION);
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
		ps5settings::N3ds& hand = m_side == System::Nds ? settings.nds : settings.n3ds;
		if (id == "music")
			settings.music = kMusic[std::clamp(index, 0, 1)];
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
		else if (id == "dsfilter")
			hand.dsFilter = std::clamp(index, 0, 2);
		else if (id == "theme")
			settings.ui.theme = index >= 0 && index < (int)m_themes.size() ? m_themes[index].id : "";
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
		ps5settings::N3ds& hand = m_side == System::Nds ? settings.nds : settings.n3ds;
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
			if (id == "side-wiiu" || id == "side-3ds" || id == "side-ds")
			{
				const int bit = id == "side-wiiu" ? 1 : id == "side-3ds" ? 2 : 4;
				const int sides = on ? settings.ui.sides | bit : settings.ui.sides & ~bit;
				if (sides == 0)
				{
					m_feedback.Play(ui::Cue::Denied);
					Toast(Tr("One side at least stays on"));
					return false;
				}
				settings.ui.sides = sides;
			}
			else if (id == "pictures")
				settings.ui.gamePictures = on;
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
			else if (id == "overlay")
				settings.overlay = on;
			else if (id == "async")
				settings.asyncShaders = on;
			else if (id == "shadernotices")
				settings.shaderNotices = on;
			else if (id == "customtextures")
				hand.customTextures = on;
			else if (id == "outlinefix")
				hand.invariantPosition = on;
			else if (id == "motion")
				hand.motion = on;
			else if (id == "dsjit")
				hand.dsJit = on;
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
											   nullptr;
			if (!value)
				return false;
			const int most = id == "deadzone" ? 50 : 100, by = id == "deadzone" ? 5 : 10;
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
		if (id == "theme")
		{
			ApplyTheme();
			m_feedback.SetLight(Accent());
			LibraryRefresh(); // Meyer Oscar's Library has no filters, the others' have
		}
		if (id == "side-wiiu" || id == "side-3ds" || id == "side-ds")
			SidesChanged();
		if (id == "accountperuser" && m_settings.accountPerUser)
			UseOwnAccount();
		if (id == "remote")
			ps5stream::Configure(m_settings.remoteScreen);
		if (id == "boxart")
		{
			ps5boxart::SetEnabled(m_settings.boxArt);
			if (m_settings.boxArt)
				FetchBoxArt();
		}
		SaveSettings();
	}

	// A game's own value, from its view of the settings after a row changed it
	void Shell::KeepGameSetting(const std::string& setting, const ps5settings::Launcher& view)
	{
		const std::string side = ps5launcher::SideName(m_side);
		int value = 0;
		if (setting.empty() || !ps5settings::Value(view, side, setting, value))
			return;
		m_settings.games[ps5settings::GameKey(side, m_gameTitle)][setting] = value;
		SaveSettings();
		m_gameView = ps5settings::ForGame(m_settings, side, m_gameTitle);
	}

	void Shell::ChooseIndex(const Row& row, int index)
	{
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
		const std::string side = ps5launcher::SideName(m_side);
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
		if (game < 0 || game >= (int)m_games.size())
			return;
		const ps5emu::Game& g = m_games[game].entry.game;
		if (g.titleId == 0)
		{
			m_feedback.Play(ui::Cue::Denied);
			Toast(Tr("This game has no ID to keep settings by"));
			return;
		}
		m_gameFrom = m_screen == ScreenId::Hub ? ScreenId::Hub : ScreenId::Library;
		m_sidePage = m_page;
		m_gameTitle = g.titleId;
		m_gameName = g.name;
		m_gameView = ps5settings::ForGame(m_settings, ps5launcher::SideName(m_side), g.titleId);
		const auto pages = SettingsPages();
		m_page = pages.empty() ? "graphics" : pages.front().id;
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
		m_onRows = false;
		m_message.clear();
		if (m_gameFrom == ScreenId::Hub)
			Show(ScreenId::Hub);
		else
			ShowTab(ScreenId::Library);
	}

	// A row changed by Left or Right (step) or Cross
	void Shell::ChangeSetting(const Row& row, int step, bool cross)
	{
		const std::string& id = row.id;
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
		else if (id == "folder")
		{
			m_filesSide = m_side;
			m_pageFrom = ScreenId::Settings;
			FilesOpen(0);
		}
		else if (id == "rescan")
		{
			if (Is3ds())
				ps5azahar::StartScan(hand.gamesFolder);
			else if (IsDs())
				ps5melonds::StartScan(hand.gamesFolder);
			else if (m_status.coreReady)
				ps5emu::Rescan();
			m_scanning = CoreReady();
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
			ps5boxart::Refetch(BoxSide(m_side), ids, id == "boxartall");
			if (ids.empty())
				m_feedback.Play(ui::Cue::Denied);
		}
		else if (id == "packs")
			ps5packs::Start(); // GitHub's latest community packs, when newer
		else if (id == "appupdate")
		{
			// installing shows over the screen (the update sheet); otherwise GitHub is asked again
			const auto state = ps5update::GetStatus().state;
			if (state == ps5update::Status::State::Available)
				ps5update::Install();
			else if (state == ps5update::Status::State::Ready)
				ps5update::Restart();
			else
				ps5update::Check();
		}
		else if (id == "player")
			PlayerOpen(row.index);
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
		else if (id == "homemenu")
		{
			// the 3DS Home Menu, from the console's files Artic Setup copied
			ps5emu::Game home;
			if (ps5azahar::HomeMenu(hand.region, home))
				LaunchGame(home, Tr("Starting the 3DS"), {760, 240, 400, 400});
			else
				m_feedback.Play(ui::Cue::Denied);
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
		m_feedback.Play(ui::Cue::Hold);
		if (id == "clearcaches")
			m_diagnosticsDone[Is3ds() ? 2 : 1] = ClearShaderCaches(Is3ds());
		else if (id == "boxartall")
		{
			ChangeSetting(Row{Row::Kind::Action, "boxartall"}, 0, true);
			// tr: a held row's result: the covers are on their way
			m_message = Tr("Downloading in the background");
		}
		else if (id == "resetcontrols")
		{
			ps5azahar::ResetControls(Handheld(m_side));
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
		else if (id == "resetgame")
		{
			m_settings.games.erase(ps5settings::GameKey(ps5launcher::SideName(m_side), m_gameTitle));
			SaveSettings();
			m_gameView = ps5settings::ForGame(m_settings, ps5launcher::SideName(m_side), m_gameTitle);
			m_message = Tr("Done: its side's settings");
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
		auto rows = SettingRows(m_page);
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
					m_barFocus = 3;
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
			m_onRows = false;
			m_feedback.Play(ui::Cue::Back);
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
				m_onRows = false;
				m_feedback.Play(ui::Cue::Back);
			}
			else
				ChangeSetting(row, -1, false);
			break;
		case Button::Right: ChangeSetting(row, 1, false); break;
		case Button::Cross: ChangeSetting(row, 0, true); break;
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
		canvas.Text(Style(kOverlineStyle), card.x + pad, y, Tr("Support PS5CEMU-HAR"), Accent(), card.w - 2 * pad, 1);
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
			Tr("PS5CEMU-HAR is free. If it brings your games back, you can support its work here."), Secondary(), card.w - 2 * pad, 4,
			ui::Align::Centre);
	}

	void Shell::SettingsDraw(Canvas& canvas)
	{
		// a dialog over the screen it was opened from, on every side and in every theme (6.5): that
		// screen as it was, dimmed, taking no input and asking for no focus
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
			const ui::TextStyle style = Style({24, selected ? ui::Weight::SemiBold : ui::Weight::Medium, 1.0f});
			const ui::TextBlock text = m_fonts.Layout(style, pages[i].title, item.w - 86, 1);
			canvas.Text(text, item.x + 68, item.CentreY() - text.height * 0.5f, selected ? kText : Secondary());
		}
		canvas.PopClip();

		// the page: its title, what it is for, its rows filling the rest of the dialog
		const SettingsPage& current = pages[page];
		const float px = dialog.x + navW + 48, pw = dialog.Right() - 48 - px;
		float ty = dialog.y + 36;
		canvas.Text(Style(kTitleStyle), px, ty, current.title, kText, pw, 1);
		canvas.Text(Style({22, ui::Weight::Regular, 1.3f}), px, ty + 62, current.subtitle, Secondary(), pw, 1);
		auto rows = SettingRows(m_page);
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		const float ry = ty + 112;
		const float rowH = 96;
		// rows past the dialog's height scroll with the focus
		const float available = dialog.Bottom() - 28 - ry;
		const float scroll = std::max(0.0f, (m_settingRow + 1) * (rowH + 12) - available);
		canvas.PushClip({px - 20, ry - 12, pw + 40, available + 24});
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const Box box{px, ry + i * (rowH + 12) - scroll, pw, rowH};
			const bool focused = m_onRows && i == m_settingRow;
			DrawRow(canvas, rows[i], box, focused);
			if (focused && rows[i].kind == Row::Kind::Hold)
				HoldRing(canvas, box, m_holdProgress);
		}
		// the remote touch screen, while it is on: the code to scan, the address, what is happening
		const float under = ry + rows.size() * (rowH + 12) - scroll;
		if (m_page == "remote" && m_settings.remoteScreen && !InGameSettings())
			DrawRemoteScreen(canvas, {px, under + 18, pw, 372});
		else
		{
			// what the page says under its rows, in as many lines as the dialog has room for; About's beside
			// its support card
			const std::string panel = PanelText(m_page);
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
