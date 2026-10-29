Warning: truncated output (original token count: 25968)
Total output lines: 2651

// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the new launcher (shell.h): its loop, the sides and their games, the bar along the top,
// the hints, the backdrop, the overlays and the launch. The screens are in their own files
// (shell_internal.h).

#include "shell_internal.h"
#include "../actions.h"
#include "../sound.h"
#include "../../app/boxart.h"
#include "../../app/forwarders.h"
#include "../../app/keyboard.h"
#include "../../app/mods.h"
#include "../../app/pack_updates.h"
#include "../../app/paths.h"
#include "../../app/updates.h"
#include "../../azahar/azahar.h"
#include "../../azahar/library.h"
#include "../../eden/eden.h"
#include "../../eden/library.h"
#include "../../melonds/library.h"
#include "../../ps5/kernel.h"
#include "../../ps5/log.h"
#include "../../ps5/notify.h"
#include "../../ps5/pad.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <utility>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		Host s_host;
		bool s_hostSet = false;
		std::string s_failure; // why the launcher could not start or go on (LastFailure)

		const Host& TheHost()
		{
			if (!s_hostSet)
			{
				s_host = DefaultHost();
				s_hostSet = true;
			}
			return s_host;
		}

		// the keyboard's pages, as the in-game keyboards have them (app/keyboard.h), so the app has one
		// keyboard: the Latin letters, the menus' language's own, the symbols, which L1 and R1 go
		// through; capitals with Shift (its key or Triangle). Under a page's rows, Shift, Space, Delete
		// and Done.
		const std::vector<ps5keyboard::Page>& KeyboardPages()
		{
			return ps5keyboard::Pages(ps5lang::Current());
		}
	}

	const std::string& LastFailure()
	{
		return s_failure;
	}

	void SetHost(Host host)
	{
		s_host = std::move(host);
		s_hostSet = true;
	}

	Outcome Run(ps5settings::Launcher& settings, ps5launcher::Status& status, const std::function<void(System)>& prepare,
		std::optional<ps5launcher::Choice>& choice)
	{
		Shell shell(settings, status, prepare);
		return shell.Run(choice);
	}

	Shell::Shell(ps5settings::Launcher& settings, ps5launcher::Status& status, const std::function<void(System)>& prepare)
		: m_settings(settings), m_status(status), m_prepare(prepare)
	{
	}

	Shell::~Shell()
	{
		StopUnpacking();
		if (m_images)
			m_images->Stop();
		m_gfx.Stop();
	}

	// -- the loop -----------------------------------------------------------------------------------

	Outcome Shell::Run(std::optional<ps5launcher::Choice>& choice)
	{
		const Host& host = TheHost();
		ui::SetLog([](const std::string& line) { ps5log::Write(line); });
		std::string error;
		const double started = host.clock();
		m_fonts.SetLanguage(ps5lang::Current()); // before the atlas: a CJK language's has more room
		if (!m_fonts.Load(host.assets + "/fonts/lexend.sdf", error))
		{
			ps5log::Line("[ui] {}", error);
			s_failure = error;
			return Outcome::Classic;
		}
		m_fonts.SetFallbackFolders(host.fontFolders);
		ApplyTheme();
		ui::Target target = host.target();
		const auto createSurface = target.createSurface;
		if (createSurface)
			target.createSurface = [this, createSurface](VkInstance instance, std::string& why) {
				m_videoOut = true;
				return createSurface(instance, why);
			};
		if (!m_gfx.Start(host.getInstanceProcAddr, target, error))
		{
			ps5log::Line("[ui] the new launcher cannot draw: {}", error);
			s_failure = "it cannot draw: " + error;
			return m_videoOut ? Outcome::Restart : Outcome::Classic;
		}
		m_gfx.SetAtlas(m_fonts.AtlasWidth(), m_fonts.AtlasHeight(), m_fonts.Atlas());
		m_atlasHeight = m_fonts.AtlasHeight();
		uint32_t first, rows;
		m_fonts.TakeChanged(first, rows);
		m_images = std::make_unique<ui::Images>(m_gfx);
		ps5sound::Start(m_settings.music, m_settings.musicVolume, m_settings.menuSounds);
		ps5boxart::SetEnabled(m_settings.boxArt);
		ps5catalog::Load();
		m_start = m_now = m_lastFrame = host.clock();
		m_sideMix.Snap(1); // the backdrop shows from the first frame; switching sides fades it in again

		// the bubbles, everywhere at first
		uint32_t seed = 0x5735;
		auto random = [&seed] {
			seed ^= seed << 13;
			seed ^= seed >> 17;
			seed ^= seed << 5;
			return (seed >> 8) / 16777216.0f;
		};
		for (int i = 0; i < 90; i++)
		{
			const float radius = 3 + random() * 34;
			m_bubbles.push_back({random() * 1920, random() * 1080, radius, 14 + 38 * (0.5f * random() + 0.5f * radius / 37), (random() * 2 - 1) * 7,
				0.04f + random() * 0.12f});
		}

		// the side: the one a game came back from, else the one last used, else (a first start) the first
		// with games (Wii U, 3DS, DS, Switch), else the Wii U's. Nothing asks which side (5.3), unless Start on:
		// Ask each time is on.
		const bool returning = !m_settings.side.empty();
		std::string side = returning ? m_settings.side : m_settings.ui.lastSide;
		if (returning)
		{
			m_settings.side.clear(); // a fresh start next time
			SaveSettings();
		}
		if (side.empty())
		{
			const int wiiuGames = std::max<int>(m_settings.gameCount, (int)ps5catalog::Games(ps5catalog::System::WiiU).size());
			const int n3dsGames = std::max<int>(m_settings.n3ds.gameCount, (int)ps5catalog::Games(ps5catalog::System::N3ds).size());
			const int ndsGames = std::max<int>(m_settings.nds.gameCount, (int)ps5catalog::Games(ps5catalog::System::Nds).size());
			const int nxGames = std::max<int>(m_settings.nx.gameCount, (int)ps5catalog::Games(ps5catalog::System::Switch).size());
			side = wiiuGames > 0 ? "wiiu" : n3dsGames > 0 ? "3ds" : ndsGames > 0 ? "ds" : nxGames > 0 ? "switch" : "wiiu";
		}
		// why the last game did not start (main_ps5.cpp's): said once here, not a state that keeps every
		// game from starting
		std::string launchError;
		for (std::string* error : {&m_status.launchError, &m_status.launchError3ds, &m_status.launchErrorDs, &m_status.launchErrorSwitch})
			if (!error->empty())
			{
				launchError = *error;
				error->clear();
			}
		// a home screen tile's game (--rom): its side, whichever was used last
		if (!m_status.autoLaunch.empty())
		{
			side = SideName(m_status.autoLaunchSide);
			ps5log::Line("[launcher] a home screen tile's game, on the {} side: {}", SideTitle(m_status.autoLaunchSide), m_status.autoLaunch);
		}
		m_side = ps5launcher::SideNamed(side);
		if (!SideShown(m_side) && m_status.autoLaunch.empty())
			m_side = FirstShownSide();
		// a version this console has not run before: What's new, once, before anything else, on a first
		// start too (a home screen tile's game starts at once instead, and What's new waits for the next
		// start); then a first start's look and feel to choose, or an update's new options
		const bool firstStart = !m_settings.ui.setupDone;
		const bool newVersion = m_settings.ui.lastVersion != PS5CEMU_VERSION;
		const bool updated = !firstStart && !returning && newVersion;
		const bool news = newVersion && !returning && m_status.autoLaunch.empty();
		if (newVersion && m_status.autoLaunch.empty())
		{
			ps5log::Line("[ui] version {} (last run: {})", PS5CEMU_VERSION, m_settings.ui.lastVersion.empty() ? "3.5.0 or earlier" : m_settings.ui.lastVersion);
			m_settings.ui.lastVersion = PS5CEMU_VERSION;
			SaveSettings();
		}
		const NewsThen then = firstStart ? NewsThen::Welcome : SetupNeeded() ? NewsThen::Setup : updated ? NewsThen::NewOptions : NewsThen::Side;
		if (news)
			WhatsNewOpen(then);
		else if (then == NewsThen::Welcome)
			WelcomeOpen(true); // then the Setup check, then the side
		else if (then == NewsThen::Setup)
			SetupOpen(true); // then the side (FinishStart)
		else if (then == NewsThen::NewOptions)
			WelcomeOpen(false); // then the side
		else
		{
			// the side's Library, on the game played last: after a game, that game (6.8)
			OpenSide(m_side, true);
			if (returning)
				Toast(Tr("Saved your place in the library"));
		}
		if (!launchError.empty())
			OpenHelp(Tr("The game could not start"), launchError);
		// a ps5cemu.json that could not be read, kept aside (ps5settings::TakeBrokenFile): said once
		else if (const std::string broken = ps5settings::TakeBrokenFile(); !broken.empty())
		{
			ps5log::Line("[settings] ps5cemu.json could not be read: kept as {}, the defaults used", broken);
			OpenHelp(Tr("Your settings could not be read"),
				// tr: {0} is where the unreadable settings file is kept: /data/ps5cemu/ps5cemu.json.broken
				TrF("The launcher's settings file, ps5cemu.json, has a mistake in it, so every setting is back to its default. The file as "
					"it was is kept as {0}: mend it on a computer and put it back as ps5cemu.json, or leave it.",
					broken));
		}
		// the session before this one ended in a crash (main_ps5.cpp): said once, with where its log is
		else if (!m_status.lastCrash.empty())
		{
			OpenHelp(Tr("PS5-ECAM closed after a crash"),
				// tr: {0} is the crash report's first line in the log, as it is: "signal 11 at 0x7f3a..."
				TrF("The last time, PS5-ECAM stopped with a crash: {0}. Its log is kept: Settings > Diagnostics > Copy logs to USB puts it "
					"on a drive, with the sessions before it, to attach to a report on GitHub.",
					m_status.lastCrash));
			m_status.lastCrash.clear();
		}
		// a folder for each system that this start could not give every system (main_ps5.cpp): said once
		else if (const auto& failed = m_status.reorganized.failed; !failed.empty())
		{
			std::string names;
			for (const std::string& name : failed)
			{
				const System side = name == "azahar" ? System::N3ds : name == "melonds" ? System::Nds : name == "eden" ? System::Switch : System::WiiU;
				names += (names.empty() ? "" : ", ") + std::string(SystemName(side));
			}
			OpenHelp(Tr("Some files could not move"),
				// tr: {0} names the systems whose files did not move into folders of their own: "Wii U, Nintendo 3DS"
				TrF("{0}: their files stayed where they were, with nothing lost. The boot log says why: Settings > Diagnostics > Copy logs "
					"to USB.",
					names));
		}
		// a folder for each system: what this start did, said once, or else offered to an install from
		// before at a fresh start, unless Later was chosen less than a week ago (Settings > Game files has it)
		m_layout = ps5reorganize::Look(ps5paths::Root(), ps5paths::kDefaultRoot);
		if (!m_status.reorganized.moved.empty())
			Toast(Tr("The folders have moved"));
		else if (!returning && !firstStart && m_status.autoLaunch.empty() && !m_layout.separate.empty() && m_layout.blocked.empty() &&
				 Today() >= m_settings.ui.reorganizeAsked + 7)
			m_reorganize.offer = true;
		ps5log::Line("[ui] the new launcher is up in {:.0f} ms", (host.clock() - started) * 1000);

		while (!m_done && !m_failed)
			Frame();
		if (m_failed)
		{
			m_images->Stop();
			ps5sound::Stop();
			m_gfx.Stop();
			s_failure = "the GPU stopped drawing it";
			return Outcome::Restart;
		}
		// the background work stops behind the launch (4.2, rule 4), the screen kept drawn
		ps5boxart::Stop();
		ps5packs::Stop();
		ps5mods::Stop();
		ps5forwarders::Stop();
		ps5emu::StopFindingInstallables();
		ps5saves::Finish(); // a backup or restore running finishes first
		const double waitFrom = host.clock();
		bool waited = false;
		while (SystemScanning() && !m_failed)
		{
			if (!waited)
				ps5log::Line("[launcher] waiting for the library's scan to finish");
			waited = true;
			Frame();
		}
		if (waited)
			ps5log::Line("[launcher] the scan finished {:.0f} ms after the game was chosen", (host.clock() - waitFrom) * 1000);
		ps5retro::FinishScans();
		ps5catalog::Save();
		Frame();
		// the in-game menus' font for what Lexend lacks: the console's, for this language's words, its
		// keyboard's keys and this game's name; none for a Latin language and title
		if (m_choice)
		{
			const ui::Fonts::FontFile font = m_fonts.FileFor(
				ps5lang::MenuCharacters() + ps5keyboard::Characters(ps5lang::Current()) + ui::Decode(m_choice->game.name));
			ps5lang::SetMenuFont({font.path, font.face});
			if (!font.path.empty())
				ps5log::Line("[ui] the in-game menu's second font: {} (face {})", font.path, font.face);
			// a Switch game's loading screen and in-game menu are drawn by Eden's program: what they show
			// of the game, as the other sides' menus have it (its name, GameTDB's publisher and year, its
			// cover), left for it
			if (m_choice->system == ps5launcher::System::Switch)
			{
				ps5eden::Session session{m_choice->game.name};
				session.titleId = m_choice->game.titleId;
				if (const int index = FindGame(m_choice->game.titleId); index >= 0)
				{
					Game& game = m_games[index];
					if (game.known)
						for (const std::string& part : {game.info.publisher, ps5gameinfo::Year(game.info.released)})
							if (!part.empty())
								session.details += (session.details.empty() ? "" : "  /  ") + part;
					session.cover = game.boxArt.empty() ? ps5eden::CoverPath(game.entry.game) : game.boxArt;
				}
				ps5eden::WriteSession(session);
			}
		}
		ps5sound::Stop();
		m_feedback.Stop();
		m_images->Stop();
		const uint64_t memory = m_gfx.MemoryBytes();
		m_gfx.Stop();
		ps5log::Line("[ui] the new launcher is gone ({} frames, {} MiB of GPU memory freed)", m_frames, memory >> 20);
		choice = m_choice;
		return Outcome::Chosen;
	}

	void Shell::Frame()
	{
		const Host& host = TheHost();
		m_now = host.clock();
		m_dt = (float)std::clamp(m_now - m_lastFrame, 0.0, 0.05);
		const double interval = m_now - m_lastFrame;
		m_lastFrame = m_now;
		m_iconLookedThisFrame = false;
		if (++m_frames % 120 == 0)
		{
			ps5pad::Rescan(); // controllers joining or leaving, about every two seconds
			if (IsRetro() && Handheld(m_side).autoGameFolders && m_screen == ScreenId::Library && !m_launch.active)
			{
				const auto signature = ps5retro::StorageSignature(ps5retro::StorageRoots());
				if (!m_storageSignature.empty() && signature != m_storageSignature) LookForGames();
				m_storageSignature = signature;
			}
		}

		ui::Actions actions = m_input.Poll(m_now);
		// the screensaver, for OLED screens: the launcher dims after kIdleSeconds with no button pressed,
		// and the press that wakes it does nothing else
		constexpr double kIdleSeconds = 300;
		if (m_lastInput < 0)
			m_lastInput = m_now;
		if (!actions.presses.empty() || actions.held)
		{
			const bool asleep = m_now - m_lastInput > kIdleSeconds;
			m_lastInput = m_now;
			if (asleep)
			{
				actions = {};
				m_input.Reset();
			}
		}
		if (m_capture.active)
		{
			PollCapture();
			actions = {};
			if (!m_capture.active)
				m_input.Reset(); // what was pressed for the mapping is not the menu's
		}
		m_feedback.Update(m_now);
		Update(actions);
		if (m_status.coreStarting)
			PollCore();
		PollGames();
		PollPacks();
		PollMods();
		PollTiles();
		PollAutoLaunch();
		if (m_installing)
			PollInstall();
		if (m_unpack.open)
			PollUnpacking();
		if (m_screen == ScreenId::Updates || m_updates.installing)
			PollUpdates();
		// a release skipped (Skip this version, or gone back from) is not offered at start
		const auto update = ps5update::GetStatus();
		const bool prompting = ps5update::Prompting() && !(update.state == ps5update::Status::State::Available && !update.installing &&
														  ps5update::Readable(update.latest) == m_settings.ui.skipVersion);
		if (prompting != m_update.open)
		{
			m_update.open = prompting;
			m_update.at = m_now;
			m_update.choice = 0;
		}
		// a folder for each system, offered once a side's library is up and nothing else is asking
		if (m_reorganize.offer && m_screen == ScreenId::Library && !m_update.open && !m_help.open && !m_picker.open && !m_menu.open &&
			!m_keyboard.open && !m_unpack.open && !m_launch.active)
		{
			m_reorganize = {};
			m_reorganize.open = true;
			m_reorganize.at = m_now;
		}

		m_images->SetUploads(!m_launch.active);
		m_images->Update();
		m_list.Clear();
		Canvas canvas(m_list, m_fonts);
		Draw(canvas);
		// the screensaver's dimming, over everything: in over three seconds, gone at a press
		if (const double idle = m_now - m_lastInput - kIdleSeconds; idle > 0 && !m_launch.active)
			canvas.Rect({0, 0, 1920, 1080}, 0, ui::SetAlpha(0xff000000, 0.85f * (float)std::min(idle / 3.0, 1.0)));
		// the glyphs this frame rendered, uploaded with it; the atlas made again when they made it grow
		if (m_fonts.AtlasHeight() != m_atlasHeight)
		{
			m_atlasHeight = m_fonts.AtlasHeight();
			m_gfx.SetAtlas(m_fonts.AtlasWidth(), m_fonts.AtlasHeight(), m_fonts.Atlas());
		}
		uint32_t first, rows;
		if (m_fonts.TakeChanged(first, rows))
			m_gfx.AtlasChanged(first, rows);
		if (!m_gfx.Frame(m_list, (float)(m_now - m_start)))
		{
			ps5log::Line("[ui] the GPU stopped drawing the launcher");
			m_failed = true;
		}
		// the update's restart, once the sheet has said it is starting again: LoadExec holds this thread
		// while it tries each place the app starts from, and comes back only when none took
		if (m_update.restartAt >= 0 && m_now - m_update.restartAt > 0.15)
		{
			m_update.restartAt = -1;
			ps5update::Restart();
		}
		// and a folder for each system's, made as the app starts (app/reorganize.h)
		if (m_reorganize.restartAt >= 0 && m_now - m_reorganize.restartAt > 0.15)
		{
			m_reorganize.restartAt = -1;
			ps5update::Restart();
			// back here only when the PS5 did not start the app again
			m_reorganize.message = Tr("Close PS5-ECAM and start it again from the home screen: the folders move as it starts.");
		}
		if (m_frames == 1)
			ps5log::Line("[ui] first frame: {} instances, {} draws", m_list.instances.size(), m_list.runs.size());
		// the PS5's splash goes once the launcher is drawn
		if (!m_splashHidden)
		{
			sceSystemServiceHideSplashScreen();
			m_splashHidden = true;
		}
		// the tour log's line, every 600 frames
		static double s_worst = 0, s_sum = 0;
		static int s_count = 0;
		if (m_frames > 1)
		{
			s_worst = std::max(s_worst, interval);
			s_sum += interval;
			s_count++;
		}
		if (s_count == 600)
		{
			ps5log::Line("[ui] frames={} avg={:.2f} ms max={:.2f} ms draws={} instances={} gpu={} MiB", m_frames, s_sum / s_count * 1000,
				s_worst * 1000, m_list.runs.size(), m_list.instances.size(), m_gfx.MemoryBytes() >> 20);
			s_worst = s_sum = 0;
			s_count = 0;
		}
		if (host.afterFrame)
			host.afterFrame();
	}

	void Shell::Update(const ui::Actions& actions)
	{
		if (m_launch.active)
		{
			if (m_now - m_launch.at > kLaunchSeconds + 0.25)
				m_done = true;
			return;
		}
		for (const ui::Press& press : actions.presses)
		{
			if (m_launch.active || m_capture.active)
				break;
			if (OverlayUpdate(press))
				continue;
			const bool tabbed = m_screen == ScreenId::Library || m_screen == ScreenId::Settings;
			if (tabbed && BarUpdate(press))
				continue;
			switch (m_screen)
			{
			case ScreenId::WhatsNew: WhatsNewUpdate(press); break;
			case ScreenId::Welcome: WelcomeUpdate(press, actions); break;
			case ScreenId::Setup: SetupUpdate(press); break;
			case ScreenId::Library: LibraryUpdate(press); break;
			case ScreenId::Hub: HubUpdate(press); break;
			case ScreenId::Settings: SettingsUpdate(press, actions); break;
			case ScreenId::Packs: PacksUpdate(press); break;
			case ScreenId::Player: PlayerUpdate(press, actions); break;
			case ScreenId::Mapping: MappingUpdate(press); break;
			case ScreenId::Files: FilesUpdate(press); break;
			case ScreenId::Artic: ArticUpdate(press, actions); break;
			case ScreenId::Updates: UpdatesUpdate(press); break;
			case ScreenId::Shares: SharesUpdate(press, actions); break;
			}
		}
		// what is held to confirm goes on without presses
		if (actions.presses.empty() && !m_picker.open && !m_help.open && !m_menu.open && !m_keyboard.open && !m_update.open && !m_unpack.open &&
			!m_reorganize.open)
		{
			const ui::Press none{Button::Count, false};
			if (m_screen == ScreenId::Settings)
				SettingsUpdate(none, actions);
			else if (m_screen == ScreenId::Player)
				PlayerUpdate(none, actions);
			else if (m_screen == ScreenId::Artic)
				ArticUpdate(none, actions);
			else if (m_screen == ScreenId::Shares)
				SharesUpdate(none, actions);
		}
	}

	void Shell::Draw(Canvas& canvas)
	{
		m_drawing = &canvas;
		DrawBackdrop(canvas);
		const float appear = m_launch.active ? 1.0f - ui::ease::CubicOut((float)((m_now - m_launch.at) / kLaunchSeconds)) * 0.85f : 1.0f;
		canvas.PushAlpha(appear);
		switch (m_screen)
		{
		case ScreenId::WhatsNew: WhatsNewDraw(canvas); break;
		case ScreenId::Welcome: WelcomeDraw(canvas); break;
		case ScreenId::Setup: SetupDraw(canvas); break;
		case ScreenId::Library: LibraryDraw(canvas); break;
		case ScreenId::Hub: HubDraw(canvas); break;
		case ScreenId::Settings: SettingsDraw(canvas); break;
		case ScreenId::Packs: PacksDraw(canvas); break;
		case ScreenId::Player: PlayerDraw(canvas); break;
		case ScreenId::Mapping: MappingDraw(canvas); break;
		case ScreenId::Files: FilesDraw(canvas); break;
		case ScreenId::Artic: ArticDraw(canvas); break;
		case ScreenId::Updates: UpdatesDraw(canvas); break;
		case ScreenId::Shares: SharesDraw(canvas); break;
		}
		const bool overlay = m_picker.open || m_help.open || m_menu.open || m_keyboard.open || m_update.open || m_unpack.open || m_reorganize.open;
		if (overlay)
			m_ringAsked = false; // the overlay's own focus is drawn with it
		if (!m_launch.active)
		{
			DrawFocusRing(canvas);
			DrawHints(canvas, Hints());
		}
		canvas.PopAlpha();
		DrawOverlays(canvas);
		m_drawing = nullptr;
	}

	// -- sides and games ------------------------------------------------------------------------

	bool Shell::SystemScanning() const
	{
		return IsRetro() ? ps5retro::Scanning(SideIndex()) : Is3ds()	  ? ps5azahar::Scanning() :
			IsDs()		  ? ps5melonds::Scanning() :
			IsSwitch()	  ? ps5eden::Scanning() :
							m_status.coreReady && ps5emu::Scanning();
	}

	void Shell::WantBackdrop(Game* game)
	{
		// the game's own picture: a Wii U game's boot screen once read, else its box art
		const std::string path = game ? BackdropOf(*game) : std::string();
		if (path == m_backdropWanted && (!game || game->entry.ambient[0] == m_wantedAmbient[0]))
			return;
		if (path != m_backdropWanted)
			m_wantedAt = m_now;
		m_backdropWanted = path;
		m_wantedAmbient[0] = game ? game->entry.ambient[0] : 0;
		m_wantedAmbient[1] = game ? game->entry.ambient[1] : 0;
		if (game && path == m_backdrop)
		{
			// the picture's colours arrived after it: they glide in now
			for (int i = 0; i < 2; i++)
			{
				m_ambientFrom[i] = m_ambient[i];
				m_ambientTo[i] = m_wantedAmbient[i];
			}
			m_ambientMix.Snap(0);
			m_ambientMix.Go(1, m_now, kAmbientSeconds, ui::ease::InOut);
		}
	}

	void Shell::OpenSide(System side, bool first)
	{
		if (m_prepared != side)
		{
			if (m_prepared)
			{
				// the side being left finishes looking for games first
				const System leaving = m_side;
				m_side = *m_prepared;
				while (SystemScanning() && !m_failed)
					Frame();
				m_side = leaving;
			}
			m_side = side;
			m_games.clear();
			m_listed = false;
			if (!first || m_frames > 0)
			{
				// what is happening shows while the side starts (Cemu takes a few seconds)
				Show(ScreenId::Library);
				Frame();
			}
			m_prepare(side);
			m_prepared = side;
			// A and B chosen for every side before Cemu was up (Make it yours): the Wii U players' now
			if (side == System::WiiU && m_status.coreReady && m_settings.wiiuFaceLayout)
			{
				for (int player = 0; player < ps5pad::kMaxPlayers; player++)
					ps5emu::SetFaceLayout(player, m_settings.wiiuFaceLayout == 1 ? ps5emu::FaceLayout::Nintendo : ps5emu::FaceLayout::PlayStation);
				m_settings.wiiuFaceLayout = 0;
				SaveSettings();
			}
		}
		const bool changed = m_side != side || first;
		m_side = side;
		m_settings.ui.lastSide = SideName(side);
		SaveSettings();
		LoadGames();
		m_feedback.SetLight(Accent());
		if (changed)
			m_sideMix.Go(1, m_now, m_settings.ui.reduceMotion ? kReducedSeconds : kAmbientSeconds), m_sideMix.from = 0;
		// the Library, on the game this side had in focus, else the one played last (6.1): in Recently
		// played, where that game is first (the list, when nothing was pl…13968 tokens truncated…;
				}
				else if (m_keyboard.column == 2)
					text = ps5keyboard::Deleted(text);
				else
					finish();
				m_feedback.Play(ui::Cue::Select);
				if (!m_keyboard.naming)
					LibraryRefresh();
				break;
			}
			case Button::Square:
			{
				std::string& text = m_keyboard.naming ? m_keyboard.text : m_search;
				text = ps5keyboard::Deleted(text);
				m_feedback.Play(ui::Cue::Back);
				if (!m_keyboard.naming)
					LibraryRefresh();
				break;
			}
			case Button::Triangle:
				// Shift, as on the in-game keyboards: capitals and the second symbols until pressed again
				m_keyboard.shift = !m_keyboard.shift;
				m_feedback.Play(ui::Cue::Toggle);
				break;
			case Button::Options:
				// Done, as the hints have it: a name is kept
				finish();
				m_feedback.Play(ui::Cue::Select);
				break;
			case Button::Circle:
			{
				// deletes, as on the in-game keyboards; with nothing left, a fresh press closes the
				// keyboard (a name is left as it was), a held one stops at empty
				std::string& text = m_keyboard.naming ? m_keyboard.text : m_search;
				if (!text.empty())
				{
					text = ps5keyboard::Deleted(text);
					m_feedback.Play(ui::Cue::Back);
					if (!m_keyboard.naming)
						LibraryRefresh();
				}
				else if (!press.repeat)
				{
					m_keyboard.open = false;
					m_feedback.Play(ui::Cue::Back);
				}
				break;
			}
			default: break;
			}
			return true;
		}
		if (m_menu.open)
		{
			const int count = (int)m_menu.items.size();
			if (b == Button::Circle || b == Button::Options)
			{
				m_menu.open = false;
				m_feedback.Play(ui::Cue::Back);
			}
			else if (b == Button::Up || b == Button::Down)
			{
				const int next = m_menu.selected + (b == Button::Down ? 1 : -1);
				if (next < 0 || next >= count)
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
				{
					m_menu.selected = next;
					m_feedback.Play(ui::Cue::Focus);
				}
			}
			else if (b == Button::Cross)
			{
				m_menu.open = false;
				const std::string id = m_menu.items[m_menu.selected].first;
				const int game = m_menu.game;
				Game& g = m_games[game];
				if (id == "play")
					Launch(game);
				else if (id == "resume")
					Launch(game, true);
				else if (id == "hub")
				{
					m_feedback.Play(ui::Cue::Select);
					HubOpen(game, m_screen);
				}
				else if (id == "packs")
				{
					m_feedback.Play(ui::Cue::Select);
					PacksOpen(game, m_screen);
				}
				else if (id == "updates") // that game's alone (6.12)
					UpdatesOpen(g.entry.game.titleId, m_screen);
				else if (id == "mods")
					AddMods(g.entry.game.titleId, g.entry.game.name);
				else if (id == "tile")
					AddTile(g);
				else if (id == "untile")
					RemoveTile(g);
				else if (id == "removeaddons")
				{
					// which of them: the picker is the confirmation, and Circle leaves both. A Wii U game's are
					// those installed in mlc01, not those Cemu reads in the games folder
					const bool wiiu = IsWiiU();
					bool update = g.entry.game.hasUpdate, dlc = g.entry.game.dlcCount > 0;
					if (wiiu)
						ps5emu::InstalledAddOns(g.entry.game.titleId, update, dlc);
					std::vector<std::string> options;
					std::vector<std::pair<bool, bool>> choices;
					if (update)
					{
						options.push_back(Tr("The update"));
						choices.push_back({true, false});
					}
					if (dlc)
					{
						options.push_back(Tr("The DLC"));
						choices.push_back({false, true});
					}
					if (update && dlc)
					{
						options.push_back(Tr("The update and the DLC"));
						choices.push_back({true, true});
					}
					const uint64_t titleId = g.entry.game.titleId;
					OpenPicker(g.entry.game.name, Tr("Remove its update or DLC"), options, -1, [this, titleId, choices, wiiu](int choice) {
						std::string error;
						const auto& [update, dlc] = choices[choice];
						if (wiiu ? ps5emu::RemoveInstalled(titleId, update, dlc, error) : ps5azahar::RemoveInstalled(titleId, update, dlc, error))
						{
							if (wiiu)
								// tr: after a Wii U game's update or DLC is removed: Updates and DLC (the game's Options) installs it again
								Toast(Tr("Removed. Updates and DLC puts it back."));
							else
								Toast(Tr("Removed. Install a CIA file puts it back."));
						}
						else
						{
							// tr: {0} is the system's reason, as it gives it
							const std::string why = TrF("It could not all be removed: {0}", error);
							OpenHelp(Tr("Remove its update or DLC"), why);
						}
						LookForGames();
					});
				}
				else if (id == "settings")
				{
					m_feedback.Play(ui::Cue::Select);
					GameSettingsOpen(game);
				}
				else if (id == "saves")
				{
					// its game settings, on their Saves page (6.15)
					m_feedback.Play(ui::Cue::Select);
					GameSettingsOpen(game);
					if (InGameSettings())
						m_page = "gamesaves";
				}
				else if (id == "favourite")
				{
					g.entry.favourite = !g.entry.favourite;
					ps5catalog::SetFavourite(CatalogSide(), g.entry.game.titleId, g.entry.favourite);
					ps5catalog::Save();
					m_feedback.Play(ui::Cue::Toggle);
					Toast(g.entry.favourite ? Tr("Added to Favourites") : Tr("Taken out of Favourites"));
					LibraryRefresh();
				}
				else if (id == "hide")
				{
					g.entry.hidden = !g.entry.hidden;
					ps5catalog::SetHidden(CatalogSide(), g.entry.game.titleId, g.entry.hidden);
					ps5catalog::Save();
					m_feedback.Play(ui::Cue::Toggle);
					// where it is found again: the Hidden filter only while the filters' row is shown (Settings > Launcher)
					if (!g.entry.hidden)
						Toast(Tr("Back in the Library"));
					else if (m_settings.ui.libraryFilters)
						Toast(Tr("Hidden. The Library's Hidden filter has it, and Search finds it."));
					else
						// tr: a game just hidden from the Library while its filters' row is off: Search still lists it
						Toast(Tr("Hidden. Search finds it."));
					LibraryRefresh();
				}
			}
			return true;
		}
		return false;
	}

	void Shell::DrawOverlays(Canvas& canvas)
	{
		// toasts, top right
		std::erase_if(m_toasts, [this](const ToastItem& toast) { return m_now - toast.at > 3.6; });
		float ty = 124;
		for (const ToastItem& toast : m_toasts)
		{
			const float age = (float)(m_now - toast.at);
			const float alpha = std::min({1.0f, age / 0.2f, (3.6f - age) / 0.4f});
			const ui::TextStyle style = Style(kLabelStyle);
			const ui::TextBlock text = m_fonts.Layout(style, toast.text, 760, 2);
			const Box box{1920 - kSafeX - text.width - 56, ty, text.width + 56, text.height + 32};
			canvas.PushAlpha(std::max(0.0f, alpha));
			canvas.PushOffset(0, (1 - std::min(1.0f, age / 0.25f)) * -12);
			DrawSheet(canvas, box, alpha);
			canvas.Rect({box.x + 18, box.CentreY() - 5, 10, 10}, 5, Accent());
			canvas.Text(text, box.x + 38, box.y + 16, kText);
			canvas.PopOffset();
			canvas.PopAlpha();
			ty += box.h + 12;
		}

		auto appear = [this](double at) { return m_settings.ui.reduceMotion ? std::min(1.0f, (float)((m_now - at) / kReducedSeconds)) :
																			ui::ease::CubicOut((float)((m_now - at) / 0.3)); };
		if (m_picker.open)
		{
			const float a = appear(m_picker.at);
			Scrim(canvas, 0.7f * a);
			const int count = (int)m_picker.options.size(), visible = std::min(count, 8);
			const float rowH = 64;
			// wider, to 900, for a longer language's options (the one in use has a check and "In use" after it)
			float widest = 0;
			for (const std::string& option : m_picker.options)
				widest = std::max(widest, m_fonts.Width(Style(kBodyStyle), option));
			const float inUseWidth = m_fonts.Width(Style(kCaptionStyle), Tr("In use"));
			const float sheetWidth = std::clamp(widest + 40 + 20 + 20 + 56 + std::min(160.0f, inUseWidth) + 40, 620.0f, 900.0f);
			const Box sheet{1920 - kSafeX - sheetWidth, 540 - (visible * rowH + 170) / 2, sheetWidth, visible * rowH + 170};
			canvas.PushAlpha(a);
			canvas.PushOffset((1 - a) * 40, 0);
			DrawSheet(canvas, sheet, a);
			canvas.Text(Style(kOverlineStyle), sheet.x + 36, sheet.y + 34, m_picker.kicker, Secondary());
			canvas.Text(Style(kHeadingStyle), sheet.x + 36, sheet.y + 60, m_picker.title, kText, sheet.w - 72, 1);
			const int first = std::clamp(m_picker.selected - visible / 2, 0, std::max(0, count - visible));
			m_picker.scroll.target = (float)first;
			m_picker.scroll.omega = kScrollOmega;
			m_picker.scroll.Update(m_dt);
			const Box list{sheet.x + 20, sheet.y + 124, sheet.w - 40, visible * rowH};
			canvas.PushClip(list);
			for (int i = 0; i < count; i++)
			{
				const Box row{list.x, list.y + (i - m_picker.scroll.value) * rowH, list.w, rowH - 6};
				if (row.Bottom() < list.y || row.y > list.Bottom())
					continue;
				const bool focused = i == m_picker.selected;
				if (focused)
					canvas.Rect(row, 14, Surface2());
				// the option, and on the one in use a check and "In use" (at most 160 wide) after it
				float labelWidth = row.w - 180;
				if (i == m_picker.active)
				{
					canvas.Draw(Icon::Check, {row.Right() - 46, row.CentreY() - 13, 26, 26}, Accent());
					const ui::TextBlock inUse = canvas.Text(Style(kCaptionStyle), row.Right() - 216, row.CentreY() - 13, Tr("In use"), Secondary(), 160, 1,
						ui::Align::Right);
					labelWidth = std::min(labelWidth, row.w - 20 - 56 - inUse.width - 20);
				}
				canvas.Text(Style(kBodyStyle), row.x + 20, row.CentreY() - 19, m_picker.options[i], focused ? kText : Secondary(), labelWidth, 1);
			}
			canvas.PopClip();
			canvas.PopOffset();
			canvas.PopAlpha();
		}
		if (m_help.open)
		{
			const float a = appear(m_help.at);
			Scrim(canvas, 0.75f * a);
			const ui::TextBlock body = m_fonts.Layout(Style(kBodyStyle), m_help.text, 820, 14);
			const ui::TextBlock title = m_fonts.Layout(Style(kHeadingStyle), m_help.title, 820, 2);
			const float h = 48 + title.height + 20 + body.height + 48;
			const Box sheet{960 - 450, 540 - h / 2, 900, h};
			canvas.PushAlpha(a);
			canvas.PushOffset(0, (1 - a) * 30);
			DrawSheet(canvas, sheet, a);
			canvas.Text(title, sheet.x + 40, sheet.y + 48, kText);
			canvas.Text(body, sheet.x + 40, sheet.y + 48 + title.height + 20, Secondary());
			canvas.PopOffset();
			canvas.PopAlpha();
		}
		if (m_menu.open && m_menu.game >= 0 && m_menu.game < (int)m_games.size())
		{
			const float a = appear(m_menu.at);
			Scrim(canvas, 0.6f * a);
			const int count = (int)m_menu.items.size();
			// rows of 66, closer when that many would reach the button hints (a Wii U game's eleven): the
			// sheet ends 24 above them
			const float rowsRoom = 1080 - kHintsBottom - 30 - 24 - 150 - 150;
			const float rowH = std::min(66.0f, rowsRoom / (float)std::max(count, 1));
			const Box sheet{1920 - kSafeX - 560, 150, 560, 150 + count * rowH};
			canvas.PushAlpha(a);
			canvas.PushOffset((1 - a) * 40, 0);
			DrawSheet(canvas, sheet, a);
			Game& game = m_games[m_menu.game];
			Badge(canvas, sheet.x + 36, sheet.y + 32, m_side);
			canvas.Text(Style(kHeadingStyle), sheet.x + 36, sheet.y + 78, game.entry.game.name, kText, sheet.w - 72, 1);
			// an item with no icon here gets More: map::at threw for one (Remove its update or DLC) and ended the app
			static const std::map<std::string, Icon> kIcons = {{"play", Icon::Play}, {"hub", Icon::ChevronRight}, {"settings", Icon::Sparkle},
				{"saves", Icon::Save}, {"packs", Icon::Gear}, {"mods", Icon::Usb}, {"updates", Icon::Download}, {"removeaddons", Icon::Dash},
				{"favourite", Icon::Star}, {"hide", Icon::Dash}, {"tile", Icon::Home}, {"untile", Icon::Home}, {"resume", Icon::Play}};
			for (int i = 0; i < count; i++)
			{
				const Box row{sheet.x + 20, sheet.y + 136 + i * rowH, sheet.w - 40, rowH - 6};
				const bool focused = i == m_menu.selected;
				if (focused)
					canvas.Rect(row, 14, Surface2());
				const auto icon = kIcons.find(m_menu.items[i].first);
				canvas.Draw(icon != kIcons.end() ? icon->second : Icon::More, {row.x + 18, row.CentreY() - 14, 28, 28}, focused ? kText : Secondary());
				canvas.Text(Style(kBodyStyle), row.x + 62, row.CentreY() - 19, m_menu.items[i].second, focused ? kText : Secondary());
				if (focused)
				{
					canvas.Ring(row.Inset(-4), 18, 4, kFocus);
				}
			}
			canvas.PopOffset();
			canvas.PopAlpha();
		}
		if (m_keyboard.open)
		{
			const float a = appear(m_keyboard.at);
			const std::vector<ps5keyboard::Page>& pages = KeyboardPages();
			const int pageIndex = std::clamp(m_keyboard.page, 0, (int)pages.size() - 1);
			const ps5keyboard::Page& page = pages[pageIndex];
			// as tall as the language's tallest page, its keys as wide as its longest row lets ten keys'
			// width hold them: the sheet stays as it is from page to page
			const float keyH = 56, gap = 10, tenKeys = 92 * 10 + gap * 9;
			const int tallest = ps5keyboard::MostRows(ps5lang::Current());
			const int longest = std::max(10, ps5keyboard::MostKeys(ps5lang::Current()));
			const float keyW = std::floor((tenKeys - gap * (longest - 1)) / longest);
			const float height = 86 + (tallest + 1) * (keyH + gap) + 4;
			const Box sheet{960 - 560, 1080 - 60 - height, 1120, height};
			canvas.PushAlpha(a);
			canvas.PushOffset(0, (1 - a) * 60);
			DrawSheet(canvas, sheet, a);
			std::string typed = m_keyboard.naming ? m_keyboard.text : m_search;
			if (m_keyboard.secret)
			{
				// a password: a dot for each character
				std::string dots;
				for (size_t i = 0; i < ps5keyboard::Length(typed); i++)
					dots += "•";
				typed = dots;
			}
			const std::string empty = m_keyboard.naming ? m_keyboard.title : std::string(Tr("Search"));
			canvas.Draw(m_keyboard.naming ? Icon::Sparkle : Icon::Search, {sheet.x + 36, sheet.y + 30, 30, 30}, Secondary());
			canvas.Text(Style(kHeadingStyle), sheet.x + 80, sheet.y + 26, typed.empty() ? empty : typed + "_", typed.empty() ? Tertiary() : kText,
				sheet.w - 120 - (m_keyboard.raw ? 260 : 0), 1);
			// the page R1 goes to, by the letters it has ("abc", "äöü", "абв", "#+=")
			canvas.Text(Style({22, ui::Weight::Medium, 1.0f}), sheet.Right() - 40 - 240, sheet.y + 34,
				TrF("L1 R1: {0}", pages[(pageIndex + 1) % pages.size()].label), Secondary(), 240, 1, ui::Align::Right);
			const int actionRow = (int)page.rows.size();
			for (int row = 0; row <= actionRow; row++)
			{
				const bool last = row == actionRow;
				const int length = last ? 4 : (int)page.rows[row].size();
				// the last row as wide as ten keys, under the tallest page's rows: Shift 2 keys, Space 4,
				// Delete 2, Done 2
				const float widths[4] = {92 * 2 + gap, 92 * 4 + gap * 3, 92 * 2 + gap, 92 * 2 + gap};
				float total = 0;
				for (int k = 0; k < length; k++)
					total += (last ? widths[k] : keyW) + (k ? gap : 0);
				float x = sheet.CentreX() - total / 2;
				const float y = sheet.y + 86 + (last ? tallest : row) * (keyH + gap);
				for (int k = 0; k < length; k++)
				{
					const float w = last ? widths[k] : keyW;
					const Box key{x, y, w, keyH};
					const bool focused = m_keyboard.row == row && m_keyboard.column == k;
					const bool lit = last && k == 0 && m_keyboard.shift; // Shift held on
					canvas.Rect(key, 12, focused ? kText : lit ? Accent() : Surface2());
					// tr: the on-screen keyboard's keys
					const std::string label = !last ? ps5keyboard::Label(page.rows[row][k], m_keyboard.shift) :
						k == 0 ? std::string(m_keyboard.shift ? Tr("Shift: on") : Tr("Shift")) : k == 1 ? Tr("Space") : k == 2 ? Tr("Delete") : Tr("Done");
					canvas.Text(Style({26, ui::Weight::Medium, 1.0f}), key.x + 8, key.CentreY() - 15, label, focused ? kInk1 : kText, key.w - 16, 1,
						ui::Align::Centre);
					x += w + gap;
				}
			}
			canvas.PopOffset();
			canvas.PopAlpha();
		}
		if (m_update.open)
		{
			const float a = appear(m_update.at);
			Scrim(canvas, 0.8f * a);
			using State = ps5update::Status::State;
			const auto status = ps5update::GetStatus();
			const std::string next = ps5update::Readable(status.latest), mine = ps5update::Readable(PS5CEMU_VERSION);
			std::string title, text;
			float done = -1;
			std::vector<std::string> choices;
			switch (status.state)
			{
			case State::Available:
				title = TrF("PS5-ECAM {0} is out", next);
				text = TrF("This is {0}. Download and install it now? Your games, saves and settings stay as they are.", mine);
				choices = {Tr("Update now"), Tr("Later"), Tr("Skip this version")};
				break;
			case State::Downloading:
				title = TrF("Updating to {0}", next);
				text = status.total ? TrF("Downloading: {0} of {1} MB", status.received >> 20, status.total >> 20) : Tr("Downloading…");
				done = status.total ? (float)status.received / (float)status.total : 0.0f;
				break;
			case State::Verifying:
				title = TrF("Updating to {0}", next);
				text = Tr("Checking the download");
				done = 1;
				break;
			case State::Installing:
				title = TrF("Updating to {0}", next);
				text = Tr("Installing");
				done = 1;
				break;
			case State::Ready:
				title = TrF("{0} is installed", next);
				if (m_update.restartAt >= 0)
					text = Tr("Starting PS5-ECAM again…");
				else if (!status.message.empty())
				{
					text = status.message; // the PS5 did not start it again
					choices = {Tr("Restart now"), Tr("Later")};
				}
				else
				{
					text = Tr("PS5-ECAM starts again to finish the update.");
					choices = {Tr("Restart now")};
				}
				break;
			case State::Failed:
				title = Tr("The update did not finish");
				text = status.message + "\n" + TrF("PS5-ECAM is still {0}.", mine);
				choices = {Tr("OK")};
				break;
			default: title = Tr("Checking for updates"); break;
			}
			const Box sheet{960 - 520, 300, 1040, 440};
			canvas.PushAlpha(a);
			canvas.PushOffset(0, (1 - a) * 40);
			DrawSheet(canvas, sheet, a);
			canvas.Text(Style(kOverlineStyle), sheet.x + 48, sheet.y + 44, Tr("PS5-ECAM update"), Secondary(), sheet.w - 96, 1);
			canvas.Text(Style(kTitleStyle), sheet.x + 48, sheet.y + 72, title, kText, sheet.w - 96, 1);
			ui::TextStyle body = Style(kBodyStyle);
			body.tabular = true;
			canvas.Text(body, sheet.x + 48, sheet.y + 140, text, Secondary(), sheet.w - 96, 3);
			if (done >= 0)
			{
				const Box bar{sheet.x + 48, sheet.y + 260, sheet.w - 96, 12};
				canvas.Rect(bar, 6, Wash(0.14f));
				canvas.Rect({bar.x, bar.y, bar.w * std::clamp(done, 0.0f, 1.0f), bar.h}, 6, Accent());
			}
			m_update.choice = std::min<int>(m_update.choice, std::max<int>(0, (int)choices.size() - 1));
			float x = sheet.x + 48;
			for (int i = 0; i < (int)choices.size(); i++)
			{
				const Box b = PillButton(canvas, x, sheet.Bottom() - 48 - 72, choices[i], i == 0, i == m_update.choice);
				x += b.w + 20;
			}
			DrawFocusRing(canvas);
			if (!choices.empty())
				DrawHints(canvas, choices.size() > 1 ? std::vector<Hint>{{Icon::LeftRight, Tr("Choose")}, {Icon::Cross, Tr("Select")}, {Icon::Circle, Tr("Later")}} :
													   std::vector<Hint>{{Icon::Cross, status.state == State::Failed ? Tr("OK") : Tr("Restart")}});
			else if (status.state == State::Downloading || status.state == State::Verifying)
				DrawHints(canvas, {{Icon::Circle, Tr("Cancel")}}); // (a download that stalls fails by itself too)
			canvas.PopOffset();
			canvas.PopAlpha();
		}

		// a folder for each system, offered to an install from before (app/reorganize.h)
		if (m_reorganize.open)
		{
			const float a = appear(m_reorganize.at);
			Scrim(canvas, 0.8f * a);
			const bool restarting = m_reorganize.restartAt >= 0;
			const std::string text = restarting					 ? std::string(Tr("Starting PS5-ECAM again…")) :
				!m_reorganize.message.empty() ? m_reorganize.message :
												// tr: {0} lists the four folders: /data/cemu, /data/azahar, /data/melonds, /data/eden
												TrF("Your Wii U, 3DS, DS and Switch files go into folders of their own: {0}. They are moved on "
													"the same drive, never copied, so it takes a moment whatever their size. PS5-ECAM starts "
													"again to do it.",
													SystemFolders());
			std::vector<std::string> choices;
			if (!restarting)
				choices = m_reorganize.message.empty() ? std::vector<std::string>{Tr("Reorganize"), Tr("Later")} : std::vector<std::string>{Tr("OK")};
			const Box sheet{960 - 520, 280, 1040, 480};
			canvas.PushAlpha(a);
			canvas.PushOffset(0, (1 - a) * 40);
			DrawSheet(canvas, sheet, a);
			canvas.Text(Style(kOverlineStyle), sheet.x + 48, sheet.y + 44, Tr("Game files"), Secondary(), sheet.w - 96, 1);
			canvas.Text(Style(kTitleStyle), sheet.x + 48, sheet.y + 72, Tr("A folder for each system"), kText, sheet.w - 96, 1);
			canvas.Text(Style(kBodyStyle), sheet.x + 48, sheet.y + 140, text, Secondary(), sheet.w - 96, 6);
			m_reorganize.choice = std::min<int>(m_reorganize.choice, std::max<int>(0, (int)choices.size() - 1));
			float x = sheet.x + 48;
			for (int i = 0; i < (int)choices.size(); i++)
			{
				const Box b = PillButton(canvas, x, sheet.Bottom() - 48 - 72, choices[i], i == 0, i == m_reorganize.choice);
				x += b.w + 20;
			}
			DrawFocusRing(canvas);
			if (choices.size() > 1)
				DrawHints(canvas, {{Icon::LeftRight, Tr("Choose")}, {Icon::Cross, Tr("Select")}, {Icon::Circle, Tr("Later")}});
			else if (!choices.empty())
				DrawHints(canvas, {{Icon::Cross, Tr("OK")}});
			canvas.PopOffset();
			canvas.PopAlpha();
		}

		// a game in an archive being unpacked before it starts: how far it is, and Circle to stop it
		if (m_unpack.open)
		{
			const float a = appear(m_unpack.at);
			Scrim(canvas, 0.8f * a);
			const uint64_t done = m_unpack.progress.done, total = m_unpack.progress.total;
			const bool stopping = m_unpack.progress.cancel;
			const float fraction = total ? std::clamp((float)done / (float)total, 0.0f, 1.0f) : 0.0f;
			const Box sheet{960 - 520, 330, 1040, 360};
			canvas.PushAlpha(a);
			canvas.PushOffset(0, (1 - a) * 40);
			DrawSheet(canvas, sheet, a);
			canvas.Text(Style(kOverlineStyle), sheet.x + 48, sheet.y + 44, Is3ds() ? Tr("Starting Azahar") : Tr("Starting Cemu"), Secondary(), sheet.w - 96, 1);
			canvas.Text(Style(kTitleStyle), sheet.x + 48, sheet.y + 72, m_unpack.name, kText, sheet.w - 96, 1);
			// tr: under an archived game's name while it is unpacked, the first time it starts
			canvas.Text(Style(kBodyStyle), sheet.x + 48, sheet.y + 140, Tr("Unpacking the game from its archive. This is done once: the copy is kept for the next start."),
				Secondary(), sheet.w - 96, 2);
			const Box bar{sheet.x + 48, sheet.y + 248, sheet.w - 96, 12};
			canvas.Rect(bar, 6, Wash(0.14f));
			canvas.Rect({bar.x, bar.y, bar.w * fraction, bar.h}, 6, Accent());
			ui::TextStyle figures = Style(kBodyStyle);
			figures.tabular = true;
			canvas.Text(figures, bar.x, bar.y + 30, stopping ? std::string(Tr("Stopping…")) : TrF("{0} of {1}", SizeWords(done), SizeWords(total)),
				Secondary(), bar.w - 160, 1);
			canvas.Text(figures, bar.Right() - 150, bar.y + 30, ps5lang::Percent((long long)(fraction * 100)), kText, 150, 1, ui::Align::Right);
			if (!stopping)
				DrawHints(canvas, {{Icon::Circle, Tr("Cancel")}});
			canvas.PopOffset();
			canvas.PopAlpha();
		}

		// the launch (6.8): the cover to the centre, growing, the rest dimmed, and what is happening
		if (m_launch.active)
		{
			const float t = ui::ease::QuintOut((float)((m_now - m_launch.at) / kLaunchSeconds));
			const float height = 560, width = height * std::clamp(m_launch.aspect, 0.5f, 1.4f);
			const Box to{960 - width / 2, 170, width, height};
			const Box from = m_launch.from;
			const Box at{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.w + (to.w - from.w) * t, from.h + (to.h - from.h) * t};
			canvas.Shadow(at, kRadiusCover, 30, 70, 0xa0000000);
			if (m_launch.game >= 0 && m_launch.game < (int)m_games.size())
				DrawCover(canvas, m_games[m_launch.game], at, false, 0); // its box art, or its card with the icon kept small
			else
				canvas.LinearGradient(at, kRadiusCover, ui::Mix(Accent(), kInk1, 0.4f), kInk1, at.x, at.y, at.x, at.Bottom());
			canvas.Ring(at, kRadiusCover, 1, 0x1fffffff);
			canvas.PushAlpha(t);
			canvas.Text(Style(kTitleStyle), 960 - 700, 770, m_launch.title, kText, 1400, 1, ui::Align::Centre);
			canvas.Text(Style(kBodyStyle), 960 - 700, 836, m_launch.caption, Secondary(), 1400, 1, ui::Align::Centre);
			canvas.PopAlpha();
		}
	}
}
