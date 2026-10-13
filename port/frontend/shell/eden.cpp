// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Eden (docs/UI-REDESIGN.md, 6.17), the launcher of ProsperoEden v1.001.000 in the app's
// own colours, with every one of its features. Its layout and motion follow ProsperoEden's
// (blackbearreloaded/ProsperoEden, GPL-3.0-or-later, Copyright (C) 2026 BlackBearReloaded:
// headless/prosperoeden/pe/ui shell.cpp, home.cpp, library.cpp, settings.cpp and launcher.cpp):
//
// - the rail at the left: the sections (Home, Library, Settings, About) as tiles, the section shown
//   lit, opening with their names while it has the focus; L1 and R1 go between the sections from
//   anywhere on them, Left from a screen's first column reaches it. Under the sections, the sides.
// - Home: the game to continue, its name large, what is known of it, Play and Game settings, its
//   cover floating at the right with the controllers under it, and the games played before it.
// - the Library: covers at the left, the focused game's details at the right, Play and Game settings
//   under them.
// - Settings: a board of tiles, each page's values in a few words, what the focused one is about
//   under them; a page (and a game's settings) is a page of three parts: its pages, its rows, what
//   the focused row is about.
// - About: who made what, the version, the support card.
// - the opening: the picture the PS5 shows while the app starts, held, then handed over to the
//   launcher, its three consoles going to their places on the rail; and the launch: the cover flying
//   to the middle, a little past its size, its name coming in under it.
//
// The rest (a game's hub, its graphic packs, the folder browser, the overlays) is the one every
// theme has.

#include "shell_internal.h"
#include "../actions.h"
#include "../../app/emulator.h"
#include "../../app/paths.h"
#include "../../app/updates.h"
#include "../../ps5/pad.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		// where Eden's parts are (ProsperoEden's layout, and its screens' own)
		constexpr float kRail = 120, kRailWide = 372, kRailTile = 72, kRailPitch = 90;
		constexpr float kLeft = 184, kRight = 1824, kWidth = kRight - kLeft, kTop = 44, kFooter = 1026;
		constexpr int kSections = 4;
		// Home: the words at the left, the cover at the right, the games played before along the foot
		constexpr float kWords = 1080, kCoverTop = 150, kCoverHeight = 470, kCoverWidth = 440;
		constexpr float kShelfTop = 784, kCardGap = 20, kCardHeight = 132;
		constexpr int kRecents = 4;
		constexpr int kPlay = 0, kGameSettings = 1, kFirstRecent = 2, kViewAll = 6;
		// the Library: covers at the left, the focused game's details at the right
		constexpr float kGridTop = 152, kGridBottom = 972;
		constexpr Box kDetail{1304, 136, 520, 836};
		// Settings: tiles, four to a row, and under them what the focused one is about
		constexpr int kTileColumns = 4;
		constexpr float kTileGap = 24, kTilesTop = 152, kStripHeight = 68;
		// a page of three parts: its pages at the left, its rows, what the focused row is about
		constexpr Box kKinds{kLeft, 160, 400, 792};
		constexpr Box kMiddle{624, 160, 760, 792};
		constexpr Box kExplain{1424, 160, 400, 792};
		// the opening: the launch picture held, then handed over to the launcher
		constexpr float kReveal = 0.55f, kHandover = 0.8f;
		// where the PS5's launch picture (sce_sys/pic0.dds, tools/render-presentation.py) has its consoles:
		// the GamePad, the 3DS, the DS
		constexpr float kConsoles[3][2] = {{320, 315}, {960, 315}, {1600, 315}};

		// tr: the Library's orders (Square), as library.cpp names them
		constexpr const char* kSortNames[] = {TrMark("Recently played"), TrMark("A to Z"), TrMark("Release year"), TrMark("How it runs")};

		float RailTileTop(int index)
		{
			return 540 - (kRailPitch * (kSections - 1) + kRailTile) * 0.5f + kRailPitch * index;
		}

		// the rail's side squares, at its foot: the k-th of n
		Box RailSideBox(int k, int n)
		{
			constexpr float size = 52;
			return {(kRail - size) * 0.5f, 1080 - 52 - size - (n - 1 - k) * 66.0f, size, size};
		}

		Box Lerp(const Box& a, const Box& b, float t)
		{
			return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t};
		}

		const char* SectionName(int section)
		{
			// tr: Eden's rail: its sections
			return section == 0 ? Tr("Home") : section == 1 ? TrC("tab", "Library") : section == 2 ? TrC("tab", "Settings") : TrC("app", "About");
		}

		ui::Icon SectionIcon(int section)
		{
			return section == 0 ? ui::Icon::Home : section == 1 ? ui::Icon::Grid : section == 2 ? ui::Icon::Gear : ui::Icon::Info;
		}

		// a colour made paler: the accent as Eden's kickers and lit text have it
		uint32_t Pale(uint32_t colour)
		{
			return ui::Mix(colour, 0xffffffff, 0.45f);
		}

		std::string Clock()
		{
			char clock[16] = "";
			const std::time_t now = std::time(nullptr);
			std::tm local{};
			if (localtime_r(&now, &local))
				std::strftime(clock, sizeof(clock), "%H:%M", &local);
			return clock;
		}
	}

	// -- the rail and what every section has ---------------------------------------------------------------

	int Shell::EdenSectionIndex() const
	{
		switch (m_screen)
		{
		case ScreenId::Home: return 0;
		case ScreenId::Library: return 1;
		case ScreenId::Settings: return 2;
		case ScreenId::About: return 3;
		default: return -1;
		}
	}

	bool Shell::EdenOnBoard() const
	{
		const int section = EdenSectionIndex();
		if (section < 0)
			return false;
		return section != 2 || (!InGameSettings() && !m_onRows);
	}

	std::vector<System> Shell::RailSides() const
	{
		std::vector<System> sides;
		for (System side : {System::WiiU, System::N3ds, System::Nds})
			if (SideShown(side))
				sides.push_back(side);
		return sides;
	}

	void Shell::EdenGo(int section)
	{
		section = std::clamp(section, 0, kSections - 1);
		const int from = EdenSectionIndex();
		m_railFocus = false;
		if (section == from && EdenOnBoard())
			return;
		m_edenDirection = section >= from ? 1 : -1;
		static constexpr const char* kPaths[kSections] = {"~/home", "~/library", "~/settings", "~/about"};
		TermLog(std::string("cd ") + kPaths[section]); // Terminal's log
		switch (section)
		{
		case 0: ShowTab(ScreenId::Home); break;
		case 1: ShowTab(ScreenId::Library); break;
		case 2:
			m_onRows = false; // its board, also from one of its pages
			ShowTab(ScreenId::Settings);
			break;
		default: ShowTab(ScreenId::About); break;
		}
	}

	bool Shell::EdenBarUpdate(const ui::Press& press)
	{
		if (InGameSettings())
			return false; // a game's settings: a page of their own, Circle back to where they came from
		const Button b = press.button;
		if (b == Button::Touchpad)
		{
			if (SidesShown() > 1)
				SwitchSide(NextShownSide(m_side));
			else
				m_feedback.Play(ui::Cue::Denied);
			return true;
		}
		if (b == Button::L1 || b == Button::R1)
		{
			if (!EdenOnBoard() && !m_railFocus)
				return false; // a page of Settings open: L1 and R1 are not the sections'
			const int section = EdenSectionIndex() + (b == Button::R1 ? 1 : -1);
			if (section < 0 || section >= kSections)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				EdenGo(section);
				m_feedback.Play(ui::Cue::Focus);
			}
			return true;
		}
		if (!m_railFocus)
			return false;
		const std::vector<System> sides = RailSides();
		const int items = kSections + (int)sides.size();
		switch (b)
		{
		case Button::Up:
		case Button::Down:
		{
			const int next = m_railIndex + (b == Button::Down ? 1 : -1);
			if (next < 0 || next >= items)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_railIndex = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Cross:
		case Button::Right:
			if (m_railIndex < kSections)
			{
				if (m_railIndex == EdenSectionIndex() && EdenOnBoard())
				{
					m_railFocus = false;
					m_feedback.Play(ui::Cue::Focus);
				}
				else
				{
					EdenGo(m_railIndex);
					m_feedback.Play(ui::Cue::Select);
				}
			}
			else
			{
				const System side = sides[std::clamp(m_railIndex - kSections, 0, (int)sides.size() - 1)];
				if (side == m_side)
				{
					m_railFocus = false;
					m_feedback.Play(ui::Cue::Focus);
				}
				else
					SwitchSide(side);
				m_railFocus = false;
			}
			break;
		case Button::Circle:
		case Button::Left:
			m_railFocus = false;
			m_feedback.Play(ui::Cue::Back);
			break;
		default: break;
		}
		return true;
	}

	void Shell::SideTile(Canvas& canvas, System side, const Box& box, float radius, uint32_t tint)
	{
		// each side's square with its console's glyph in it (tools/render-icons.py), plain colour until
		// the picture has loaded
		static const std::string kTiles[3] = {ps5paths::Assets() + "/ui/icons/ps5cemu-72.tga", ps5paths::Assets() + "/ui/icons/azahar-72.tga",
			ps5paths::Assets() + "/ui/icons/melonds-72.tga"};
		const ui::Picture& picture = m_images->Get(kTiles[SideIndex(side)]);
		if (picture.texture)
			canvas.Image(picture.texture, box, radius, tint);
		else
			canvas.Rect(box, radius, ui::SetAlpha(AccentOf(side), ((tint >> 24) & 0xff) / 255.0f));
	}

	void Shell::EdenDrawRail(Canvas& canvas)
	{
		if (PhantomLayout())
			return PhantomDrawRail(canvas); // experiments.cpp
		if (TerminalLayout())
			return TermDrawRail(canvas);
		const bool still = m_settings.ui.reduceMotion;
		m_railOpen.target = m_railFocus ? 1.0f : 0.0f;
		m_railOpen.omega = still ? 60.0f : 18.0f;
		m_railOpen.Update(m_dt);
		const float open = std::clamp(m_railOpen.value, 0.0f, 1.0f);
		const float width = kRail + (kRailWide - kRail) * open;
		const int section = EdenSectionIndex();
		// during the opening it comes in from the left as the picture leaves
		float slide = 0;
		if (m_openingOn && !still)
			slide = -width * (1 - ui::ease::InOut(m_openingAt < 0 ? 0.0f : (float)((m_now - m_openingAt - kReveal) / kHandover)));
		canvas.PushOffset(slide, 0);

		// its body: it opens over the screen beside it while the focus is on it, that screen dimmed so what
		// the rail covers half of reads as behind it
		if (open > 0.01f)
		{
			canvas.Rect({width, 0, 1920 - width, 1080}, 0, ui::SetAlpha(kInk0, 0.6f * open));
			canvas.Shadow({0, 0, width, 1080}, 0, 0, 60, ui::SetAlpha(0xff000000, 0.5f * open));
		}
		if (m_settings.ui.highContrast)
			canvas.Rect({0, 0, width, 1080}, 0, kInk2);
		else
			canvas.LinearGradient({0, 0, width, 1080}, 0, ui::SetAlpha(kInk1, 0.9f + 0.1f * open), ui::SetAlpha(kInk0, 0.94f + 0.06f * open), 0, 0, 0, 1080);
		canvas.Rect({width - 1.5f, 0, 1.5f, 1080}, 0, ui::WithAlpha(kGlassEdge, 0.7f + 0.3f * open));

		// the mark: the three sides' squares, as the bar has them (the opening brings them here)
		SideTile(canvas, System::WiiU, {28, 38, 36, 36}, 9, 0xffffffff);
		SideTile(canvas, System::N3ds, {54, 47, 36, 36}, 9, 0xebffffff);
		SideTile(canvas, System::Nds, {39, 68, 36, 36}, 9, 0xebffffff);
		if (open > 0.01f)
		{
			canvas.PushAlpha(open);
			canvas.Text(Style({22, ui::Weight::SemiBold, 1.0f, 4.0f}), 112, 60, "PS5CEMU-HAR", kText, kRailWide - 112 - 24, 1);
			canvas.PopAlpha();
		}

		// the sections
		for (int i = 0; i < kSections; i++)
		{
			m_railLit[i].target = i == section ? 1.0f : 0.0f;
			m_railLit[i].omega = still ? 60.0f : 14.0f;
			m_railLit[i].Update(m_dt);
			const float lit = std::clamp(m_railLit[i].value, 0.0f, 1.0f);
			const bool focused = m_railFocus && m_railIndex == i;
			const Box tile{(kRail - kRailTile) * 0.5f, RailTileTop(i), kRailTile + (width - kRail), kRailTile};
			// the section shown keeps a quiet plate of the side's colour; the focus is the ring
			if (lit > 0.01f)
			{
				canvas.Rect(tile, 20, m_settings.ui.highContrast ? ui::SetAlpha(Surface2(), lit) : ui::SetAlpha(Accent(), 0.16f * lit));
				canvas.Ring(tile, 20, 1.5f, ui::SetAlpha(Accent(), 0.55f * lit));
			}
			else if (focused)
				canvas.Rect(tile, 20, Surface2());
			const uint32_t ink = focused ? kText : ui::Mix(Tertiary(), Pale(Accent()), lit);
			canvas.Draw(SectionIcon(i), {tile.x + 20, tile.y + 20, 32, 32}, ink);
			if (open > 0.01f)
			{
				canvas.PushAlpha(open);
				const ui::TextStyle style = Style({26, lit > 0.5f ? ui::Weight::SemiBold : ui::Weight::Medium, 1.0f});
				const ui::TextBlock name = m_fonts.Layout(style, SectionName(i), kRailWide - (tile.x + kRailTile + 8) - 32, 1);
				canvas.Text(name, tile.x + kRailTile + 8, tile.CentreY() - name.height * 0.5f, focused || lit > 0.5f ? kText : Secondary());
				canvas.PopAlpha();
			}
			if (focused)
				Focus(tile, 20);
		}

		// the sides, at its foot (the opening draws them while it brings them here)
		const std::vector<System> sides = RailSides();
		for (int k = 0; k < (int)sides.size() && !(m_openingOn && !still); k++)
		{
			const Box box = RailSideBox(k, (int)sides.size());
			const bool on = sides[k] == m_side;
			const bool focused = m_railFocus && m_railIndex == kSections + k;
			if (on)
				canvas.Ring(box.Inset(-6), 18, 2, ui::SetAlpha(AccentOf(sides[k]), 0.9f));
			SideTile(canvas, sides[k], box, 12, on || focused ? 0xffffffff : 0x8cffffff);
			if (open > 0.01f)
			{
				canvas.PushAlpha(open);
				const ui::TextStyle style = Style({24, on ? ui::Weight::SemiBold : ui::Weight::Medium, 1.0f});
				const ui::TextBlock name = m_fonts.Layout(style, SideTitle(sides[k]), kRailWide - (box.Right() + 30) - 32, 1);
				canvas.Text(name, box.Right() + 30, box.CentreY() - name.height * 0.5f, on || focused ? kText : Secondary());
				canvas.PopAlpha();
			}
			if (focused)
				Focus(box.Inset(-6), 18);
		}
		canvas.PopOffset();
	}

	float Shell::EdenDrawClock(Canvas& canvas)
	{
		const ui::TextStyle style = Style({30, ui::Weight::Medium, 1.0f, 0, false, true});
		const ui::TextBlock clock = m_fonts.Layout(style, Clock());
		float right = kRight;
		canvas.Text(clock, right - clock.width, kTop + 32 - clock.height * 0.5f, kText);
		right -= clock.width + 28;
		// the controllers connected, the first lit
		for (int player = ps5pad::kMaxPlayers - 1; player >= 0; player--)
		{
			if (!ps5pad::IsConnected(player))
				continue;
			const Box pad{right - 38, kTop + 32 - 14, 38, 28};
			if (player == 0)
				canvas.Rect(pad, 8, kText);
			else
				canvas.Ring(pad, 8, 2, Wash(0.35f));
			const ui::TextBlock text = m_fonts.Layout({15, ui::Weight::Bold, 1.0f}, fmt::format("P{}", player + 1));
			canvas.Text(text, pad.CentreX() - text.width * 0.5f, pad.CentreY() - text.height * 0.5f, player == 0 ? kInk1 : Secondary());
			right -= 46;
		}
		return right;
	}

	void Shell::EdenDrawTitle(Canvas& canvas, const std::string& title, const std::string& copy)
	{
		const ui::TextStyle style = Style({52, ui::Weight::Bold, 1.0f});
		const ui::TextBlock block = m_fonts.Layout(style, title, 760, 1);
		const float top = kTop + 32 - block.height * 0.5f;
		canvas.Text(block, kLeft, top, kText);
		if (!copy.empty())
		{
			const ui::TextStyle small = Style({22, ui::Weight::Regular, 1.0f});
			const ui::TextBlock text = m_fonts.Layout(small, copy, 600, 1);
			canvas.Text(text, kLeft + block.width + 24, top + block.height - text.height - 6, Tertiary());
		}
	}

	void Shell::EdenDrawHints(Canvas& canvas, const std::vector<Hint>& hints)
	{
		if (PhantomLayout())
			return PhantomDrawHints(canvas, hints); // experiments.cpp
		if (TerminalLayout())
			return TermDrawHints(canvas, hints);
		// along the foot from the left, as ProsperoEden has them (beside the rail, as wide as it is open),
		// smaller when a language's words would reach the Library's place at the right
		ui::TextStyle style = Style({22, ui::Weight::Medium, 1.0f});
		float x = kLeft + (kRailWide - kRail) * std::clamp(m_railOpen.value, 0.0f, 1.0f);
		auto width = [&] {
			float total = 0;
			for (const Hint& hint : hints)
				if (!hint.text.empty())
					total += (hint.icon == Icon::R1 ? 34 : 0) + 30 + 10 + m_fonts.Width(style, hint.text) + 34;
			return total;
		};
		while (width() > kRight - 140 - x && style.size > 16)
			style.size -= 1;
		const float y = kFooter - 15;
		for (const Hint& hint : hints)
		{
			if (hint.text.empty())
				continue;
			if (hint.icon == Icon::R1) // the sections: L1 and R1
			{
				canvas.Draw(Icon::L1, {x, y, 30, 30}, Secondary());
				x += 34;
			}
			canvas.Draw(hint.icon, {x, y, 30, 30}, Secondary());
			x += 30 + 10;
			const ui::TextBlock text = m_fonts.Layout(style, hint.text);
			canvas.Text(text, x, y + 15 - text.height * 0.5f, Secondary());
			x += text.width + 34;
		}
	}

	std::vector<Hint> Shell::EdenHints(bool& handled) const
	{
		handled = true;
		const std::string other = SidesShown() > 1 ? SystemName(NextShownSide(m_side)) : "";
		// tr: the hint for L1 and R1 on Eden's screens: they go between its sections
		const Hint sections{Icon::R1, Tr("Sections")};
		if (m_railFocus)
			return {{Icon::UpDown, Tr("Move")}, {Icon::Cross, Tr("Open")}, {Icon::Circle, Tr("Back")}, sections, {Icon::Touchpad, other}};
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		switch (m_screen)
		{
		case ScreenId::Home:
			if (LibraryState(kicker, title, text, buttons))
				return buttons.empty() ? std::vector<Hint>{sections, {Icon::Touchpad, other}} :
										 std::vector<Hint>{{Icon::Cross, Tr("Choose")}, sections, {Icon::Touchpad, other}};
			if (m_homeFocus == kViewAll || EdenContinueGame() < 0)
				return {{Icon::Cross, Tr("Open")}, sections, {Icon::Touchpad, other}};
			return {{Icon::Cross, Tr("Play")}, {Icon::Triangle, Tr("Game settings")}, {Icon::Options, Tr("Options")}, sections, {Icon::Touchpad, other}};
		case ScreenId::Library:
			if (LibraryState(kicker, title, text, buttons))
				return buttons.empty() ? std::vector<Hint>{sections, {Icon::Touchpad, other}} :
										 std::vector<Hint>{{Icon::Cross, Tr("Choose")}, sections, {Icon::Touchpad, other}};
			if (m_libraryIndex < ArticTiles())
				return {{Icon::Cross, Tr("Open")}, sections, {Icon::Touchpad, other}};
			return {{Icon::Cross, Tr("Play")}, {Icon::Triangle, Tr("Game settings")}, {Icon::Options, Tr("Options")}, {Icon::Square, Tr("Sort")},
				{Icon::L2, Tr("Filter")}, {Icon::R2, Tr("Search")}, sections};
		case ScreenId::Settings:
			if (EdenOnBoard() && !EdenLayout()) // the experiments' list of pages
				return {{Icon::UpDown, Tr("Pages")}, {Icon::Cross, Tr("Open")}, {Icon::Circle, Tr("Back")}, sections, {Icon::Touchpad, other}};
			if (EdenOnBoard())
				return {{Icon::Cross, Tr("Open")}, {Icon::Circle, Tr("Back")}, sections, {Icon::Touchpad, other}};
			handled = false; // a page's rows: as every theme has them
			return {};
		case ScreenId::About: return {{Icon::Circle, Tr("Back")}, sections, {Icon::Touchpad, other}};
		default: handled = false; return {};
		}
	}

	void Shell::EdenDrawScrims(Canvas& canvas)
	{
		if (PhantomLayout())
			return PhantomBackdrop(canvas); // experiments.cpp
		if (TerminalLayout())
			return TermBackdrop(canvas);
		// the artwork shows where a screen leaves it room: Home keeps its right side clear, the others
		// lie over an even shade (ProsperoEden's draw_scrims)
		m_edenShade.target = m_screen == ScreenId::Home ? 0.0f : 1.0f;
		m_edenShade.omega = m_settings.ui.reduceMotion ? 60.0f : 10.0f;
		m_edenShade.Update(m_dt);
		const float away = std::clamp(m_edenShade.value, 0.0f, 1.0f), home = 1 - away;
		const uint32_t shade = kInk0;
		if (home > 0.01f)
		{
			canvas.LinearGradient({0, 0, 653, 1080}, 0, ui::SetAlpha(shade, 0.94f * home), ui::SetAlpha(shade, 0.78f * home), 0, 0, 653, 0);
			canvas.LinearGradient({653, 0, 538, 1080}, 0, ui::SetAlpha(shade, 0.78f * home), ui::SetAlpha(shade, 0.16f * home), 653, 0, 1191, 0);
			canvas.LinearGradient({1191, 0, 729, 1080}, 0, ui::SetAlpha(shade, 0.16f * home), ui::SetAlpha(shade, 0.3f * home), 1191, 0, 1920, 0);
			canvas.LinearGradient({0, 640, 1920, 440}, 0, ui::SetAlpha(shade, 0), ui::SetAlpha(shade, 0.9f * home), 0, 640, 0, 1080);
		}
		if (away > 0.01f)
			canvas.LinearGradient({0, 0, 1920, 1080}, 0, ui::SetAlpha(shade, 0.86f * away), ui::SetAlpha(shade, 0.62f * away), 0, 0, 1920, 0);
	}

	void Shell::EdenArrive(Canvas& canvas, int part, float rise)
	{
		// each part of a section a moment after the one before it, the section sliding in from the side
		// it was asked from
		const float e = Enter(part);
		const float motion = m_settings.ui.reduceMotion ? 0.0f : 1.0f;
		canvas.PushAlpha(e);
		canvas.PushOffset((1 - Enter(0)) * 48 * m_edenDirection * motion, (1 - e) * rise * motion);
	}

	void Shell::EdenLeave(Canvas& canvas)
	{
		canvas.PopOffset();
		canvas.PopAlpha();
	}

	// -- Home -------------------------------------------------------------------------------------------

	int Shell::EdenContinueGame() const
	{
		const uint64_t last = m_side == System::N3ds ? m_settings.n3ds.lastGame : m_side == System::Nds ? m_settings.nds.lastGame : m_settings.lastGame;
		int game = last ? FindGame(last) : -1;
		if (game < 0 && !m_recent.empty() && m_recent[0] < (int)m_games.size())
			game = m_recent[0];
		return game;
	}

	std::vector<int> Shell::EdenRecentGames(int continuing) const
	{
		std::vector<int> games;
		auto add = [&](int game) {
			if (game >= 0 && game < (int)m_games.size() && game != continuing && (int)games.size() < kRecents &&
				std::find(games.begin(), games.end(), game) == games.end())
				games.push_back(game);
		};
		const auto& recent = m_side == System::N3ds ? m_settings.n3ds.recent : m_side == System::Nds ? m_settings.nds.recent : m_settings.recent;
		for (uint64_t titleId : recent)
			add(FindGame(titleId));
		for (int game : m_recent)
			add(game);
		return games;
	}

	int Shell::OwnSettings(const Game& game) const
	{
		const auto found = m_settings.games.find(ps5settings::GameKey(ps5launcher::SideName(m_side), game.entry.game.titleId));
		return found == m_settings.games.end() ? 0 : (int)found->second.size();
	}

	std::string Shell::EdenRunsWith(Game& game)
	{
		std::string line = std::string(SystemName(m_side)) + "  /  " + EmulatorName(m_side);
		if (IsWiiU() && m_status.coreReady)
		{
			if (game.packsOn < 0)
				game.packsOn = ps5emu::EnabledGraphicPackCount(game.entry.game.titleId);
			// tr: Eden's Home, under Play: what the game runs with
			line += "  /  " + (game.packsOn > 0 ? TrP(game.packsOn, "Graphic packs · {0} on", "Graphic packs · {0} on") : std::string(Tr("No graphic packs on")));
		}
		const int own = OwnSettings(game);
		line += "  /  " + (own ? TrP(own, "{0} setting of its own", "{0} settings of its own") : std::string(Tr("Its side's settings")));
		return line;
	}

	void Shell::EdenStateDraw(Canvas& canvas, float x, float y, float width)
	{
		// what the side shows before it has a game to show (a notice, Cemu starting, the scan, no games),
		// with what to do: LibraryState's, as the Library's card has it
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		LibraryState(kicker, title, text, buttons);
		m_stateButtons.clear();
		EdenArrive(canvas, 1, 26);
		canvas.Text(Style(kOverlineStyle), x, y, kicker, Pale(Accent()), width, 1);
		ui::TextStyle hero = Style({76, ui::Weight::Bold, 1.08f});
		hero.size = m_fonts.Fit(hero, title, width, 2, 52, 4);
		const ui::TextBlock titleBlock = canvas.Text(hero, x, y + 36, title, kText, width, 2);
		const ui::TextBlock textBlock = canvas.Text(Style(kBodyStyle), x, y + 60 + titleBlock.height, text, Secondary(), std::min(width, 980.0f), 5);
		m_stateButton = std::clamp(m_stateButton, 0, std::max(0, (int)buttons.size() - 1));
		float bx = x;
		for (int i = 0; i < (int)buttons.size(); i++)
		{
			const Box b = PillButton(canvas, bx, y + 100 + titleBlock.height + textBlock.height, buttons[i], i == 0, !m_railFocus && m_stateButton == i);
			m_stateButtons.push_back(b);
			bx += b.w + 20;
		}
		EdenLeave(canvas);
	}

	void Shell::EdenHomeUpdate(const ui::Press& press)
	{
		const Button b = press.button;
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				if (b == Button::Left && (m_stateButton == 0 || buttons.empty()))
					FocusBar();
				else
					LibraryStateUpdate(press);
				return;
			}
		}
		const int game = EdenContinueGame();
		const std::vector<int> recents = EdenRecentGames(game);
		const int count = (int)recents.size();
		int& focus = m_homeFocus;
		if (focus >= kFirstRecent && focus < kViewAll && focus - kFirstRecent >= count)
			focus = count ? kFirstRecent + count - 1 : kViewAll;
		if (game < 0 && focus == kGameSettings)
			focus = kPlay;
		const int before = focus;
		auto focused = [&]() -> int {
			if (focus <= kGameSettings)
				return game;
			return focus >= kFirstRecent && focus < kFirstRecent + count ? recents[focus - kFirstRecent] : -1;
		};
		switch (b)
		{
		case Button::Up:
			if (focus >= kFirstRecent)
				focus = kPlay;
			else
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			break;
		case Button::Down:
			if (focus <= kGameSettings)
				focus = count ? kFirstRecent : kViewAll;
			else
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			break;
		case Button::Left:
			if (focus == kGameSettings)
				focus = kPlay;
			else if (focus == kPlay || focus == kFirstRecent || (focus == kViewAll && count == 0))
			{
				FocusBar();
				return;
			}
			else if (focus == kViewAll)
				focus = kFirstRecent + count - 1;
			else
				focus--;
			break;
		case Button::Right:
			if (focus == kPlay && game >= 0)
				focus = kGameSettings;
			else if (focus >= kFirstRecent && focus < kViewAll)
				focus = focus - kFirstRecent + 1 < count ? focus + 1 : kViewAll;
			else
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			break;
		case Button::Cross:
			if (focus == kViewAll || (focus == kPlay && game < 0))
			{
				m_feedback.Play(ui::Cue::Select);
				EdenGo(1);
			}
			else if (focus == kGameSettings)
			{
				m_feedback.Play(ui::Cue::Select);
				GameSettingsOpen(game);
			}
			else if (focused() >= 0)
				Launch(focused());
			return;
		case Button::Triangle:
			if (focused() >= 0)
			{
				m_feedback.Play(ui::Cue::Select);
				GameSettingsOpen(focused());
			}
			else
				m_feedback.Play(ui::Cue::Denied);
			return;
		case Button::Options:
			if (focused() >= 0)
				OpenGameMenu(focused());
			else
				m_feedback.Play(ui::Cue::Denied);
			return;
		case Button::Circle: FocusBar(); return;
		default: return;
		}
		if (focus != before)
			m_feedback.Play(ui::Cue::Focus);
	}

	void Shell::EdenHomeDraw(Canvas& canvas)
	{
		if (PhantomLayout())
			return PhantomHomeDraw(canvas); // experiments.cpp
		if (TerminalLayout())
			return TermHomeDraw(canvas);
		const bool still = m_settings.ui.reduceMotion;
		// the top bar: the app, its version, the side
		EdenArrive(canvas, 0, -16);
		{
			const ui::TextBlock name = m_fonts.Layout(Style({24, ui::Weight::SemiBold, 1.0f, 6.0f}), "PS5CEMU-HAR");
			const float top = kTop + 32 - name.height * 0.5f;
			canvas.Text(name, kLeft, top, kText);
			float x = kLeft + name.width + 24;
			const ui::TextStyle small = Style({20, ui::Weight::Medium, 1.0f});
			const ui::TextBlock version = m_fonts.Layout(small, ps5update::Readable(PS5CEMU_VERSION));
			canvas.Text(version, x, kTop + 32 - version.height * 0.5f, Tertiary());
			x += version.width + 16;
			canvas.Text(small, x, kTop + 32 - version.height * 0.5f, std::string("/  ") + SideTitle(m_side), Pale(Accent()), 400, 1);
		}
		EdenLeave(canvas);
		EdenDrawClock(canvas);

		std::string kicker, title, text;
		std::vector<std::string> buttons;
		if (LibraryState(kicker, title, text, buttons))
		{
			WantBackdrop(nullptr);
			EdenStateDraw(canvas, kLeft, 220, kWords + 200);
			return;
		}
		const int game = EdenContinueGame();
		const std::vector<int> recents = EdenRecentGames(game);
		const int count = (int)recents.size();
		if (m_homeFocus >= kFirstRecent && m_homeFocus < kViewAll && m_homeFocus - kFirstRecent >= count)
			m_homeFocus = count ? kFirstRecent + count - 1 : kViewAll;
		const bool onRecent = m_homeFocus >= kFirstRecent && m_homeFocus < kFirstRecent + count;
		Game* g = game >= 0 ? &m_games[game] : nullptr;
		// the backdrop: the game to continue's picture, or the recent game in focus
		WantBackdrop(onRecent ? &m_games[recents[m_homeFocus - kFirstRecent]] : g);
		const bool focusHere = !m_railFocus;

		// ---- the game to continue ----
		EdenArrive(canvas, 1, 26);
		// tr: Eden's Home: over the game played last, large
		canvas.Text(Style(kOverlineStyle), kLeft, 168, g ? Tr("Continue playing") : Tr("Welcome"), Pale(Accent()), kWords, 1);
		const std::string heroText = g ? g->entry.game.name : std::string(Tr("Your next adventure"));
		ui::TextStyle hero = Style({84, ui::Weight::Bold, 1.08f});
		hero.size = m_fonts.Fit(hero, heroText, kWords, 2, 56, 4);
		const ui::TextBlock heroBlock = canvas.Text(hero, kLeft, 204, heroText, kText, kWords, 2);
		EdenLeave(canvas);
		const float below = 204 + std::max(heroBlock.height, hero.size * 1.08f);

		// what is known of it, as chips
		EdenArrive(canvas, 2, 22);
		const float chipsTop = below + 20;
		if (g)
		{
			float cx = kLeft;
			const ps5emu::Game& entry = g->entry.game;
			if (g->report)
				cx += Chip(canvas, cx, chipsTop, TrC("status", g->report->status), ps5compat::Kind(g->report->status)) + 10;
			if (IsWiiU())
			{
				cx += Chip(canvas, cx, chipsTop, entry.hasUpdate ? TrF("Update v{0}", entry.version) : fmt::format("v{}", entry.version)) + 10;
				if (entry.dlcCount)
					cx += Chip(canvas, cx, chipsTop, Tr("DLC")) + 10;
			}
			if (!Played(*g).empty())
				cx += Chip(canvas, cx, chipsTop, Played(*g)) + 10;
			const std::string when = LastPlayedWords(g->entry.lastPlayed);
			// tr: a chip on Eden's Home: when the game was last played ({0}: today, yesterday, 3 days ago)
			if (!when.empty())
				cx += Chip(canvas, cx, chipsTop, TrF("Last played {0}", when)) + 10;
			if (g->entry.favourite)
				Chip(canvas, cx, chipsTop, Tr("Favourite"));
		}
		else
			Chip(canvas, kLeft, chipsTop, Tr("Choose a game from your library."));
		EdenLeave(canvas);

		// its actions
		EdenArrive(canvas, 3, 22);
		const float buttonsTop = chipsTop + 38 + 36;
		const Box play = PillButton(canvas, kLeft, buttonsTop, g ? Tr("Play") : Tr("Open library"), true, focusHere && m_homeFocus == kPlay, Icon::Cross,
			true, 88, 520);
		if (g)
		{
			PillButton(canvas, play.Right() + 20, buttonsTop, Tr("Game settings"), false, focusHere && m_homeFocus == kGameSettings, Icon::Triangle, true, 88,
				520);
			// what it runs with
			canvas.Draw(Icon::Pad, {kLeft, buttonsTop + 88 + 26, 30, 30}, Tertiary());
			canvas.Text(Style({22, ui::Weight::Regular, 1.0f}), kLeft + 44, buttonsTop + 88 + 30, EdenRunsWith(*g), Tertiary(), kWords - 44, 1);
		}
		EdenLeave(canvas);

		// ---- its cover, floating over the artwork; the controllers under it ----
		{
			const float e = Enter(2);
			canvas.PushAlpha(e);
			canvas.PushOffset(still ? 0.0f : (1 - e) * 40, 0);
			const float drift = still ? 0.0f : 6.0f * std::sin((float)(m_now - m_start) * 0.7f);
			if (g)
			{
				Box art = CoverBox(*g, 0, kCoverTop + kCoverHeight, kCoverHeight, kCoverWidth);
				art.x = kRight - kCoverWidth + (kCoverWidth - art.w) * 0.5f;
				art.y += drift;
				const ui::Picture& picture = Cover(*g);
				uint32_t glow = g->entry.ambient[0] ? g->entry.ambient[0] : picture.ambient[0] ? picture.ambient[0] : Accent();
				if (!m_settings.ui.highContrast)
					canvas.Glow(art, kRadiusCover, 120, ui::SetAlpha(ui::Mix(glow, 0xffffffff, 0.3f), 0.22f));
				DrawCover(canvas, *g, art, false, 0);
				canvas.Ring(art, kRadiusCover, 1, ui::SetAlpha(Pale(Accent()), 0.3f));
				if (m_homeFocus <= kGameSettings)
					m_edenLaunchFrom = canvas.Placed(art);
			}
			else
			{
				// nothing to continue yet: the side's square, large, glowing
				const Box tile{kRight - kCoverWidth * 0.5f - 150, kCoverTop + 70 + drift, 300, 300};
				canvas.Glow(tile, 66, 120, ui::SetAlpha(Accent(), 0.3f));
				SideTile(canvas, m_side, tile, 66, 0xffffffff);
			}
			// the controllers: lit as they are connected
			const float px = kRight - kCoverWidth;
			// tr: Eden's Home, under the cover: the controllers connected
			canvas.Text(Style(kOverlineStyle), px, kCoverTop + kCoverHeight + 30, Tr("Controllers"), Tertiary(), kCoverWidth, 1);
			const float padW = 92, gap = (kCoverWidth - padW * 4) / 3;
			for (int player = 0; player < 4; player++)
			{
				const bool lit = ps5pad::IsConnected(player);
				const Box pad{px + player * (padW + gap), kCoverTop + kCoverHeight + 62, padW, 48};
				canvas.Rect(pad, 14, lit ? ui::SetAlpha(Accent(), 0.18f) : Wash(0.04f));
				canvas.Ring(pad, 14, 1.5f, lit ? ui::SetAlpha(Accent(), 0.6f) : Wash(0.12f));
				canvas.Draw(Icon::Pad, {pad.x + 14, pad.CentreY() - 15, 30, 30}, lit ? kText : Tertiary());
				canvas.Text(Style({22, ui::Weight::SemiBold, 1.0f}), pad.x + 54, pad.CentreY() - 13, std::to_string(player + 1), lit ? Pale(Accent()) : Tertiary(), 30, 1);
			}
			canvas.PopOffset();
			canvas.PopAlpha();
		}

		// ---- the games played before it ----
		EdenArrive(canvas, 5, 26);
		canvas.Text(Style(kOverlineStyle), kLeft, kShelfTop, Tr("Recently played"), Pale(Accent()), 900, 1);
		{
			// tr: Eden's Home: the button to the Library, over the recent games
			const ui::TextStyle style = Style({18, ui::Weight::SemiBold, 1.0f, 3.0f, true});
			const ui::TextBlock label = m_fonts.Layout(style, Tr("View all games"));
			const bool f = focusHere && m_homeFocus == kViewAll;
			const Box r{kRight - label.width - 48, kShelfTop - 12, label.width + 48, 42};
			canvas.Rect(r, 21, f ? Surface2() : Wash(0.05f));
			canvas.Text(label, r.x + 24, r.CentreY() - label.height * 0.5f, f ? kText : Secondary());
			if (f)
				Focus(r, 21);
		}
		const float cardW = (kWidth - kCardGap * (kRecents - 1)) / kRecents;
		if (recents.empty())
			canvas.Text(Style(kBodyStyle), kLeft, kShelfTop + 60, Tr("The games you play appear here."), Tertiary(), kWidth, 1);
		for (int i = 0; i < count; i++)
		{
			Game& recent = m_games[recents[i]];
			const bool f = focusHere && m_homeFocus == kFirstRecent + i;
			const Box base{kLeft + i * (cardW + kCardGap), kShelfTop + 46, cardW, kCardHeight};
			const Box card = f && !still ? base.Scaled(1.03f) : base;
			canvas.Rect(card, 20, f ? Surface2() : Surface());
			canvas.Ring(card, 20, 1.5f, f ? ui::SetAlpha(Accent(), 0.5f) : kGlassEdge);
			Box cover = CoverBox(recent, card.x + 16, card.Bottom() - 16, card.h - 32, 112);
			cover.x += (112 - cover.w) * 0.5f;
			DrawCover(canvas, recent, cover, false, 0);
			const float tx = card.x + 16 + 112 + 18, tw = card.Right() - 18 - tx;
			const ui::TextBlock name = canvas.Text(Style({24, ui::Weight::SemiBold, 1.2f}), tx, card.y + 18, recent.entry.game.name, f ? kText : Secondary(), tw, 2);
			const std::string when = LastPlayedWords(recent.entry.lastPlayed);
			if (!when.empty())
				canvas.Text(Style({19, ui::Weight::Regular, 1.0f}), tx, card.y + 26 + name.height, when, Tertiary(), tw, 1);
			if (f)
			{
				Focus(card, 20);
				m_edenLaunchFrom = canvas.Placed(cover);
			}
		}
		EdenLeave(canvas);
	}

	// -- the Library ------------------------------------------------------------------------------------

	void Shell::EdenLibraryUpdate(const ui::Press& press)
	{
		const Button b = press.button;
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				if (b == Button::Left && (m_stateButton == 0 || buttons.empty()))
					FocusBar();
				else
					LibraryStateUpdate(press);
				return;
			}
		}
		const int side = SideIndex();
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		const int columns = SectionColumns(); // Eden's grid; the experiments' list
		const int game = LibraryGame();
		switch (b)
		{
		case Button::Square:
		{
			std::vector<std::string> names;
			for (const char* name : kSortNames)
				names.push_back(Tr(name));
			OpenPicker(TrC("tab", "Library"), Tr("Sort"), names, m_settings.ui.librarySort[side], [this, side](int choice) {
				m_settings.ui.librarySort[side] = choice;
				SaveSettings();
				LibraryRefresh();
			});
			return;
		}
		case Button::L2:
		{
			// tr: Eden's Library: which games it shows (L2), the Library's filters
			std::vector<std::string> names = {TrC("filter", "All"), Tr("Recently added"), Tr("Favourites"), Tr("Graphic packs on")};
			names.resize(LibraryFilters());
			OpenPicker(TrC("tab", "Library"), Tr("Filter"), names, m_settings.ui.libraryFilter[side], [this, side](int choice) {
				m_settings.ui.libraryFilter[side] = choice;
				SaveSettings();
				m_libraryIndex = 0;
				m_libraryFocusTitle = 0;
				LibraryRefresh();
			});
			return;
		}
		case Button::R2: OpenKeyboard(); return;
		case Button::Circle:
			if (!m_search.empty())
			{
				m_search.clear();
				LibraryRefresh();
			}
			else
				EdenGo(0);
			m_feedback.Play(ui::Cue::Back);
			return;
		case Button::Triangle:
			if (game >= 0)
			{
				m_feedback.Play(ui::Cue::Select);
				GameSettingsOpen(game);
			}
			else
				m_feedback.Play(ui::Cue::Denied);
			return;
		case Button::Options:
			if (game >= 0)
				OpenGameMenu(game);
			return;
		case Button::Cross:
			if (m_libraryIndex < offset && total > 0)
			{
				m_feedback.Play(ui::Cue::Select);
				m_pageFrom = ScreenId::Library;
				ArticOpen();
			}
			else if (game >= 0)
				Launch(game);
			else
				m_feedback.Play(ui::Cue::Denied);
			return;
		default: break;
		}
		if (total == 0)
		{
			if (b == Button::Left)
				FocusBar();
			return;
		}
		int& at = m_libraryIndex;
		at = std::clamp(at, 0, total - 1);
		auto move = [&](int to) {
			to = std::clamp(to, 0, total - 1);
			if (to == at)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				at = to;
				m_feedback.Play(ui::Cue::Focus);
			}
		};
		switch (b)
		{
		case Button::Left:
			if (at % columns == 0)
				FocusBar();
			else
				move(at - 1);
			break;
		case Button::Right:
			if (at % columns == columns - 1 || at + 1 >= total)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
				move(at + 1);
			break;
		case Button::Up:
			if (at < columns)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
				move(at - columns);
			break;
		case Button::Down:
			if (at + columns < total)
				move(at + columns);
			else if (at / columns < (total - 1) / columns)
				move(total - 1);
			else
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			break;
		default: break;
		}
		if (LibraryGame() >= 0)
			m_libraryFocusTitle = m_games[LibraryGame()].entry.game.titleId;
	}

	void Shell::EdenLibraryDraw(Canvas& canvas)
	{
		if (PhantomLayout())
			return PhantomLibraryDraw(canvas); // experiments.cpp
		if (TerminalLayout())
			return TermLibraryDraw(canvas);
		const int side = SideIndex();
		EdenArrive(canvas, 0, 0);
		EdenDrawTitle(canvas, TrC("tab", "Library"), m_games.empty() ? std::string() : TrP((int)m_games.size(), "{0} game", "{0} games"));
		EdenLeave(canvas);
		const float clockLeft = EdenDrawClock(canvas);
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				WantBackdrop(nullptr);
				EdenStateDraw(canvas, kLeft, 240, kWidth - 200);
				return;
			}
		}

		// what it shows and how: Sort, Filter and Search at the top, before the clock
		EdenArrive(canvas, 0, 0);
		{
			// tr: the Library's filters, as library.cpp names them
			const char* filters[4] = {TrC("filter", "All"), Tr("Recently added"), Tr("Favourites"), Tr("Graphic packs on")};
			const ui::TextStyle tool = Style({22, ui::Weight::Medium, 1.0f});
			float right = clockLeft - 36;
			auto item = [&](Icon icon, const std::string& label, bool lit) {
				const ui::TextBlock text = m_fonts.Layout(tool, label, 420, 1);
				right -= text.width;
				canvas.Text(text, right, kTop + 32 - text.height * 0.5f, lit ? kText : Secondary());
				canvas.Draw(icon, {right - 38, kTop + 17, 30, 30}, Secondary());
				right -= 38 + 34;
			};
			item(Icon::R2, m_search.empty() ? std::string(Tr("Search")) : TrF("Search: {0}", m_search), !m_search.empty());
			const int filter = std::clamp(m_settings.ui.libraryFilter[side], 0, LibraryFilters() - 1);
			// tr: Eden's Library, at the top: the filter in use ({0}: All, Favourites...)
			item(Icon::L2, TrF("Filter: {0}", filters[filter]), filter != 0);
			item(Icon::Square, TrF("Sort: {0}", Tr(kSortNames[std::clamp(m_settings.ui.librarySort[side], 0, 3)])), false);
		}
		EdenLeave(canvas);

		const bool wiiu = IsWiiU();
		const bool still = m_settings.ui.reduceMotion;
		const int columns = wiiu ? 5 : 4;
		const float cellW = wiiu ? 180.0f : 240.0f, coverH = wiiu ? 252.0f : 216.0f, gapX = 32;
		const float gridW = columns * cellW + (columns - 1) * gapX;
		const ui::TextStyle nameStyle = Style({22, ui::Weight::SemiBold, 1.22f});
		const ui::TextStyle metaStyle = Style({18, ui::Weight::Regular, 1.0f});
		const float pitch = coverH + 12 + nameStyle.size * nameStyle.lineHeight * 2 + 26 + 30;
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		if (total == 0)
		{
			WantBackdrop(nullptr);
			EdenArrive(canvas, 1, 24);
			const int filter = m_settings.ui.libraryFilter[side];
			canvas.Text(Style(kTitleStyle), kLeft, 300,
				m_scanning			? std::string(Tr("Looking for games…")) :
				!m_search.empty()	? TrF("Nothing matches “{0}”", m_search) :
				filter				? std::string(Tr("None here yet")) :
									  std::string(Tr("No games yet")),
				kText, gridW, 1);
			canvas.Text(Style(kBodyStyle), kLeft, 370,
				!m_search.empty() ? Tr("Circle clears the search.") :
				filter == 2		  ? Tr("Options on a game, then Favourite, puts it here.") :
				filter			  ? Tr("All shows every game.") :
									Tr("Settings > Game files says where they go."),
				Secondary(), gridW, 3);
			EdenLeave(canvas);
			EdenDetailDraw(canvas, -1);
			return;
		}
		m_libraryIndex = std::clamp(m_libraryIndex, 0, total - 1);
		const int game = LibraryGame();
		WantBackdrop(game >= 0 ? &m_games[game] : nullptr);

		// the covers, whole rows at a time: the focused row kept in sight, a row going out of the window
		// fading as it goes (no row is ever shown cut)
		const int focusRow = m_libraryIndex / columns;
		const float view = kGridBottom - kGridTop;
		const int visibleRows = std::max(1, (int)((view + 20) / pitch));
		int shownFirst = (int)std::round(m_edenScroll.target / pitch);
		if (focusRow < shownFirst)
			shownFirst = focusRow;
		if (focusRow >= shownFirst + visibleRows)
			shownFirst = focusRow - visibleRows + 1;
		m_edenScroll.target = std::max(0, shownFirst) * pitch;
		m_edenScroll.omega = still ? 60.0f : kScrollOmega;
		m_edenScroll.Update(m_dt);
		const float scroll = m_edenScroll.value;
		const int firstRow = std::max(0, (int)(scroll / pitch));
		canvas.PushClip({kLeft - 40, kGridTop - 30, gridW + 80, kGridBottom - kGridTop + 30});
		Box focusedBox{};
		for (int index = firstRow * columns; index < total; index++)
		{
			const int row = index / columns, column = index % columns;
			const Box cell{kLeft + column * (cellW + gapX), kGridTop + row * pitch - scroll, cellW, coverH};
			if (cell.y > kGridBottom)
				break;
			// a row past the window's foot, or above its top, fades out
			const float inside = std::clamp(1 - (cell.Bottom() + 92 - kGridBottom) / 80.0f, 0.0f, 1.0f) * std::clamp(1 - (kGridTop - cell.y) / 80.0f, 0.0f, 1.0f);
			if (inside <= 0.01f)
				continue;
			const bool focused = !m_railFocus && index == m_libraryIndex;
			EdenArrive(canvas, 1 + std::min(row - firstRow, 3), 20);
			canvas.PushAlpha(inside);
			if (index < offset)
			{
				// Artic Base: a game from your 3DS
				canvas.Rect(cell, kRadiusCard, Wash(focused ? 0.1f : 0.05f));
				canvas.Ring(cell, kRadiusCard, 2, ui::SetAlpha(kN3ds, 0.45f));
				canvas.Draw(Icon::Artic, {cell.CentreX() - 32, cell.y + 34, 64, 64}, Pale(kN3ds));
				canvas.Text(Style({22, ui::Weight::Regular, 1.25f}), cell.x + 16, cell.y + 118, Tr("Artic Base, over your network"), Secondary(), cell.w - 32, 2,
					ui::Align::Centre);
				canvas.Text(nameStyle, cell.x, cell.Bottom() + 12, Tr("Play from your 3DS"), focused ? kText : Secondary(), cellW, 2);
				if (focused)
					Focus(cell, kRadiusCard, kN3ds);
				canvas.PopAlpha();
				EdenLeave(canvas);
				continue;
			}
			Game& g = m_games[m_shelf[index - offset]];
			Box cover = CoverBox(g, cell.x, cell.Bottom(), coverH, cellW);
			cover.x += (cellW - cover.w) * 0.5f;
			if (focused)
				focusedBox = canvas.Placed(cover);
			else
				DrawCover(canvas, g, cover, false, 0);
			// its name on up to two lines, and a line about it under that
			const ui::TextBlock name = canvas.Text(nameStyle, cell.x, cell.Bottom() + 12, g.entry.game.name, focused ? kText : Secondary(), cellW, 2);
			const std::string meta = Join({g.report ? std::string(TrC("status", g.report->status)) : std::string(), g.entry.game.format});
			canvas.Text(metaStyle, cell.x, cell.Bottom() + 18 + name.height, meta, focused ? Pale(Accent()) : Tertiary(), cellW, 1);
			canvas.PopAlpha();
			EdenLeave(canvas);
		}
		if (focusedBox.w > 0 && game >= 0)
		{
			// over its neighbours, a little larger, from its foot (arriving with its row)
			const float lift = still ? 0.0f : 0.04f;
			const Box lifted{focusedBox.x - focusedBox.w * lift * 0.5f, focusedBox.Bottom() - focusedBox.h * (1 + lift), focusedBox.w * (1 + lift),
				focusedBox.h * (1 + lift)};
			canvas.PushAlpha(Enter(1 + std::min(m_libraryIndex / columns - firstRow, 3)));
			DrawCover(canvas, m_games[game], lifted, true, 0);
			canvas.PopAlpha();
			m_edenLaunchFrom = lifted;
		}
		canvas.PopClip();
		const int rows = (total + columns - 1) / columns;
		// where in it the window is (more rows wait under it)
		if (rows * pitch > view + 1)
		{
			const float content = rows * pitch, thumb = std::max(48.0f, view * view / content);
			const float x = kLeft + gridW + 24;
			canvas.Rect({x, kGridTop, 4, view - 120}, 2, Wash(0.08f));
			canvas.Rect({x, kGridTop + (view - 120 - thumb) * std::clamp(scroll / std::max(1.0f, content - view), 0.0f, 1.0f), 4, thumb}, 2, Wash(0.4f));
		}

		// the focused game's details
		EdenDetailDraw(canvas, game);
		// where the focus is in the Library
		const ui::TextBlock position =
			m_fonts.Layout(Style({20, ui::Weight::Medium, 1.0f, 0, false, true}), fmt::format("{} / {}", m_libraryIndex + 1, total));
		canvas.Text(position, kRight - position.width, kFooter - position.height * 0.5f, Tertiary());
	}

	void Shell::EdenDetailDraw(Canvas& canvas, int game)
	{
		const Box panel = kDetail;
		EdenArrive(canvas, 2, 0);
		canvas.Shadow(panel, kRadiusSheet, 20, 60, 0x66000000);
		canvas.Rect(panel, kRadiusSheet, m_settings.ui.highContrast ? kInk2 : ui::SetAlpha(ui::Mix(kInk1, kInk2, 0.5f), 0.9f));
		canvas.Ring(panel, kRadiusSheet, 1.5f, kGlassEdge);
		// the details fade in again as the focus moves
		if (game != m_edenDetailGame)
		{
			m_edenDetailGame = game;
			m_edenDetailAt = m_now;
		}
		const float shown = m_settings.ui.reduceMotion ? 1.0f : ui::ease::CubicOut((float)((m_now - m_edenDetailAt) / 0.22));
		const float x = panel.x + 36, w = panel.w - 72;
		const Box first{x, panel.Bottom() - 36 - 68 - 14 - 76, w, 76};
		const Box second{x, panel.Bottom() - 36 - 68, w, 68};
		auto button = [&](const Box& box, const std::string& label, Icon icon, bool primary, bool enabled) {
			canvas.PushAlpha(enabled ? 1.0f : 0.4f);
			if (primary)
				canvas.Rect(box, 20, kText);
			else
			{
				canvas.Rect(box, 20, Surface2());
				canvas.Ring(box, 20, 1.5f, kGlassEdge);
			}
			const uint32_t ink = primary ? kInk1 : kText;
			const ui::TextBlock text = m_fonts.Layout(Style({26, ui::Weight::SemiBold, 1.0f}), label, box.w - 120, 1);
			const float start = box.CentreX() - (34 + 12 + text.width) * 0.5f;
			canvas.Draw(icon, {start, box.CentreY() - 17, 34, 34}, ink);
			canvas.Text(text, start + 46, box.CentreY() - text.height * 0.5f, ink);
			canvas.PopAlpha();
		};
		if (game < 0)
		{
			// Artic Base in focus on the 3DS side, or nothing to show
			const bool artic = m_libraryIndex < ArticTiles() && Is3ds();
			canvas.Text(Style(kOverlineStyle), x, panel.y + 40, Tr("Game details"), Pale(Accent()), w, 1);
			canvas.Text(Style({34, ui::Weight::Bold, 1.15f}), x, panel.y + 76, artic ? Tr("Play from your 3DS") : Tr("No game here yet"), kText, w, 2);
			if (artic)
				canvas.Text(Style({22, ui::Weight::Regular, 1.45f}), x, panel.y + 170, Tr("Artic Base, over your network"), Secondary(), w, 4);
			button(first, artic ? Tr("Open") : Tr("Play"), Icon::Cross, true, artic);
			button(second, Tr("Game settings"), Icon::Triangle, false, false);
			EdenLeave(canvas);
			return;
		}
		Game& g = m_games[game];
		const ps5emu::Game& entry = g.entry.game;
		canvas.PushAlpha(shown);
		// its picture as a band across the top, running out into the panel: a Wii U game's boot screen,
		// else its box art
		const Box band{panel.x + 1, panel.y + 1, panel.w - 2, 168};
		const std::string bandPath = BackdropOf(g);
		const ui::Picture& picture = bandPath.empty() ? Cover(g) : m_images->Get(bandPath);
		canvas.PushClip(panel, kRadiusSheet);
		if (picture.texture && !m_settings.ui.highContrast)
			canvas.ImageCover(picture.texture, picture.width, picture.height, band, 0, ui::SetAlpha(0xffffffff, 0.9f), 0.3f);
		else
		{
			const uint32_t top = g.entry.ambient[0] ? g.entry.ambient[0] : ui::Mix(Accent(), kInk1, 0.4f);
			canvas.LinearGradient(band, 0, top | 0xff000000, kInk1 | 0xff000000, band.x, band.y, band.x, band.Bottom());
		}
		const uint32_t ground = m_settings.ui.highContrast ? kInk2 : ui::SetAlpha(ui::Mix(kInk1, kInk2, 0.5f), 0.97f);
		canvas.LinearGradient({band.x, band.y + 40, band.w, band.h - 39}, 0, ui::SetAlpha(ground, 0), ground, band.x, band.y + 40, band.x, band.Bottom());
		canvas.PopClip();

		canvas.PushOffset(0, (1 - shown) * 10);
		float y = panel.y + 184;
		// tr: Eden's Library: the heading of the focused game's panel
		canvas.Text(Style(kOverlineStyle), x, y, Tr("Game details"), Pale(Accent()), w, 1);
		y += 34;
		ui::TextStyle nameStyle = Style({34, ui::Weight::Bold, 1.15f});
		nameStyle.size = m_fonts.Fit(nameStyle, entry.name, w, 2, 28, 2);
		const ui::TextBlock name = canvas.Text(nameStyle, x, y, entry.name, kText, w, 2);
		y += name.height + 10;
		const ui::TextBlock byline = canvas.Text(Style({22, ui::Weight::Regular, 1.0f}), x, y, Byline(g), Secondary(), w, 1);
		y += byline.height + 8;
		canvas.Text(Style({18, ui::Weight::Regular, 1.0f}), x, y, entry.path.filename().string(), Tertiary(), w, 1);
		y += 36;
		// what is known for sure, as chips
		float cx = x;
		auto chip = [&](const std::string& text, const char* kind) {
			if (text.empty())
				return;
			const float width = 16 + (kind && *kind ? 17 : 0) + m_fonts.Width(Style({20, ui::Weight::SemiBold, 1.0f}), text) + 16;
			if (cx > x && cx + width > x + w)
				return; // no room left: the next line says it
			cx += Chip(canvas, cx, y, text, kind, 34) + 8;
		};
		if (IsWiiU())
			chip(entry.hasUpdate ? TrF("Update v{0}", entry.version) : fmt::format("v{}", entry.version), "");
		if (entry.dlcCount)
			chip(Tr("DLC"), "");
		if (g.entry.favourite)
			chip(Tr("Favourite"), "");
		if (!entry.format.empty())
			chip(entry.format, "");
		y += 34 + 20;
		// what it is, one line each
		struct Line
		{
			std::string name, value;
			uint32_t colour;
		};
		const int own = OwnSettings(g);
		std::vector<Line> lines;
		const std::string kind = g.report ? ps5compat::Kind(g.report->status) : "";
		// tr: Eden's Library, the focused game's lines
		lines.push_back({Tr("How it runs"), g.report ? std::string(TrC("status", g.report->status)) : std::string(Tr("Not on the list yet")),
			g.report ? ui::Mix(kind == "good" ? kGood : kind == "bad" ? kBad : kWarn, 0xffffffff, 0.35f) : Tertiary()});
		lines.push_back({Tr("Played"), Played(g).empty() ? std::string(Tr("Not played yet")) : Played(g), Played(g).empty() ? Tertiary() : kText});
		const std::string when = LastPlayedWords(g.entry.lastPlayed);
		lines.push_back({Tr("Last played"), when.empty() ? std::string(Tr("Never")) : when, when.empty() ? Tertiary() : kText});
		lines.push_back({Tr("Game settings"), own ? TrP(own, "{0} of its own", "{0} of its own") : std::string(Tr("Its side's")), own ? Pale(Accent()) : Tertiary()});
		if (IsWiiU() && m_status.coreReady)
		{
			if (g.packsOn < 0)
				g.packsOn = ps5emu::EnabledGraphicPackCount(entry.titleId);
			lines.push_back({Tr("Graphic packs"), g.packsOn > 0 ? TrP(g.packsOn, "{0} on", "{0} on") : std::string(Tr("None on")),
				g.packsOn > 0 ? Pale(Accent()) : Tertiary()});
		}
		const ui::TextStyle lineStyle = Style({22, ui::Weight::Regular, 1.0f});
		for (const Line& line : lines)
		{
			if (y + 46 > first.y - 12)
				break;
			const ui::TextBlock value = m_fonts.Layout(lineStyle, line.value, w * 0.6f, 1);
			canvas.Text(value, x + w - value.width, y + 23 - value.height * 0.5f, line.colour);
			canvas.Text(lineStyle, x, y + 23 - value.height * 0.5f, line.name, Tertiary(), w - value.width - 20, 1);
			canvas.Rect({x, y + 45, w, 1}, 0, ui::WithAlpha(kLine, 0.8f));
			y += 46;
		}
		canvas.PopOffset();
		canvas.PopAlpha();
		// its actions, as the buttons that do them
		button(first, Tr("Play"), Icon::Cross, true, true);
		button(second, own ? Tr("Game settings · custom") : Tr("Game settings"), Icon::Triangle, false, entry.titleId != 0);
		EdenLeave(canvas);
	}

	// -- Settings ---------------------------------------------------------------------------------------

	std::vector<SettingsPage> Shell::EdenBoardPages() const
	{
		std::vector<SettingsPage> pages = SettingsPages();
		std::erase_if(pages, [](const SettingsPage& page) { return page.id == "about"; }); // the rail's own
		return pages;
	}

	std::string Shell::EdenSummary(const std::string& page)
	{
		// another side's or another game's: none of them is known yet
		const std::string owner = fmt::format("{}:{:x}", SideIndex(), m_gameTitle);
		if (owner != m_edenSummaryOwner)
		{
			m_edenSummaries.clear();
			m_edenSummaryOwner = owner;
		}
		const auto found = m_edenSummaries.find(page);
		if (found != m_edenSummaries.end())
			return found->second;
		std::string summary;
		if (page.rfind("game:", 0) == 0)
		{
			// a game's page: how much of it the game has of its own
			const std::string id = page.substr(5);
			int own = 0;
			if (id == "gamesaves")
				// tr: Eden's game settings: the Saves page's line under its name
				summary = Tr("Its saves, by account");
			else
			{
				for (const Row& row : SettingRows(id))
					own += !row.setting.empty() && !row.fromSide;
				summary = own ? TrP(own, "{0} of its own", "{0} of its own") : std::string(Tr("Its side's"));
			}
		}
		else if (page == "controls")
		{
			// tr: Eden's Settings, the Controls tile: how many controllers are connected
			int connected = 0;
			for (int player = 0; player < ps5pad::kMaxPlayers; player++)
				connected += ps5pad::IsConnected(player);
			summary = TrP(connected, "{0} controller connected", "{0} controllers connected");
		}
		else if (page == "diagnostics")
			// tr: Eden's Settings, the Diagnostics tile: what the Setup check finds
			summary = SetupNeeded() ? Tr("Something needs a look") : Tr("Everything is ready");
		else
		{
			// a page of the side's: the values of its first rows, in a few words
			std::vector<std::string> parts;
			int toggles = 0, on = 0;
			std::string action;
			for (const Row& row : SettingRows(page))
			{
				if (row.dimmed)
					continue;
				std::string value;
				switch (row.kind)
				{
				case Row::Kind::Choice:
				case Row::Kind::Stepper:
				case Row::Kind::Slider: value = row.value; break;
				case Row::Kind::Segmented:
					if (row.index >= 0 && row.index < (int)row.options.size())
						value = row.options[row.index];
					break;
				case Row::Kind::Toggle:
					toggles++;
					on += row.on;
					break;
				case Row::Kind::Hold: break; // "Hold Cross" says nothing of the page
				default:
					if (action.empty())
						action = row.value;
					break;
				}
				if (!value.empty() && parts.size() < 3)
					parts.push_back(value);
			}
			// tr: Eden's Settings: how many of a page's switches are on
			if (parts.empty() && toggles)
				parts.push_back(TrF("{0} of {1} on", on, toggles));
			if (parts.empty() && !action.empty())
				parts.push_back(action);
			for (const std::string& part : parts)
				summary += (summary.empty() ? "" : "  /  ") + part;
		}
		m_edenSummaries[page] = summary;
		return summary;
	}

	void Shell::EdenBoardUpdate(const ui::Press& press)
	{
		const auto pages = EdenBoardPages();
		if (pages.empty())
			return;
		const int count = (int)pages.size();
		int at = 0;
		for (int i = 0; i < count; i++)
			if (pages[i].id == m_page)
				at = i;
		m_page = pages[at].id;
		const int before = at;
		switch (press.button)
		{
		case Button::Circle:
			m_feedback.Play(ui::Cue::Back);
			EdenGo(0);
			return;
		case Button::Up:
			if (at >= kTileColumns)
				at -= kTileColumns;
			else
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			break;
		case Button::Down:
			if (at + kTileColumns < count)
				at += kTileColumns;
			else if (at / kTileColumns < (count - 1) / kTileColumns)
				at = count - 1;
			else
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			break;
		case Button::Left:
			if (at % kTileColumns == 0)
			{
				FocusBar();
				return;
			}
			at--;
			break;
		case Button::Right:
			if (at % kTileColumns == kTileColumns - 1 || at + 1 >= count)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
				at++;
			break;
		case Button::Cross:
			if (SettingRows(m_page).empty())
				m_feedback.Play(ui::Cue::Edge);
			else
			{
				m_onRows = true;
				m_settingRow = 0;
				m_message.clear();
				m_screenAt = m_now;
				m_feedback.Play(ui::Cue::Select);
			}
			return;
		default: return;
		}
		if (at != before)
		{
			m_page = pages[at].id;
			m_feedback.Play(ui::Cue::Focus);
		}
	}

	void Shell::EdenSettingsDraw(Canvas& canvas)
	{
		if (PhantomLayout())
			return PhantomSettingsDraw(canvas); // experiments.cpp
		if (TerminalLayout())
			return TermSettingsDraw(canvas);
		if (InGameSettings() || m_onRows)
			EdenPageDraw(canvas);
		else
			EdenBoardDraw(canvas);
	}

	void Shell::EdenBoardDraw(Canvas& canvas)
	{
		const auto pages = EdenBoardPages();
		EdenArrive(canvas, 0, 0);
		EdenDrawTitle(canvas, TrC("tab", "Settings"), TrF("{0} settings", SystemName(m_side)));
		EdenLeave(canvas);
		EdenDrawClock(canvas);
		if (pages.empty())
			return;
		const int count = (int)pages.size();
		int at = 0;
		for (int i = 0; i < count; i++)
			if (pages[i].id == m_page)
				at = i;
		m_page = pages[at].id;
		// the values are read again a page a frame, so a change made elsewhere shows
		m_edenSummaries.erase(pages[m_edenSummaryNext++ % count].id);

		const int rows = (count + kTileColumns - 1) / kTileColumns;
		const float tileW = (kWidth - kTileGap * (kTileColumns - 1)) / kTileColumns;
		const float room = kFooter - 40 - kTilesTop - kStripHeight - kTileGap;
		const float tileH = std::min(224.0f, (room - (rows - 1) * kTileGap) / rows);
		const bool still = m_settings.ui.reduceMotion;
		for (int i = 0; i < count; i++)
		{
			const Box base{kLeft + (i % kTileColumns) * (tileW + kTileGap), kTilesTop + (i / kTileColumns) * (tileH + kTileGap), tileW, tileH};
			const bool focused = !m_railFocus && i == at;
			const Box tile = focused && !still ? base.Scaled(1.03f).Offset(0, -3) : base;
			EdenArrive(canvas, 1 + i, 18);
			canvas.Rect(tile, 24, focused ? Surface2() : Surface());
			canvas.Ring(tile, 24, 1.5f, focused ? ui::SetAlpha(Accent(), 0.6f) : kGlassEdge);
			canvas.Draw(pages[i].icon, {tile.x + 28, tile.y + 26, 44, 44}, focused ? Pale(Accent()) : Accent());
			const std::string summary = EdenSummary(pages[i].id);
			const ui::TextBlock line = m_fonts.Layout(Style({20, ui::Weight::Regular, 1.0f}), summary.empty() ? " " : summary, tile.w - 56, 1);
			const float lineTop = tile.Bottom() - 26 - line.height;
			canvas.Text(line, tile.x + 28, lineTop, focused ? Secondary() : Tertiary());
			ui::TextStyle nameStyle = Style({30, ui::Weight::SemiBold, 1.1f});
			nameStyle.size = m_fonts.Fit(nameStyle, pages[i].title, tile.w - 56, 1, 22, 2);
			const ui::TextBlock name = m_fonts.Layout(nameStyle, pages[i].title, tile.w - 56, 1);
			canvas.Text(name, tile.x + 28, lineTop - 8 - name.height, kText);
			if (focused)
				Focus(tile, 24);
			EdenLeave(canvas);
		}

		// under the tiles: what the focused one is about, and the version
		EdenArrive(canvas, 2, 12);
		const Box strip{kLeft, kTilesTop + rows * (tileH + kTileGap), kWidth, kStripHeight};
		canvas.Rect(strip, 20, Surface());
		canvas.Ring(strip, 20, 1.5f, kGlassEdge);
		const ui::TextBlock version = m_fonts.Layout(Style({20, ui::Weight::Medium, 1.0f}), std::string(Tr("Version")) + " " + ps5update::Readable(PS5CEMU_VERSION));
		canvas.Text(version, strip.Right() - 28 - version.width, strip.CentreY() - version.height * 0.5f, Tertiary());
		canvas.Rect({strip.x + 30, strip.CentreY() - 5, 10, 10}, 5, Accent());
		canvas.Text(Style({22, ui::Weight::Regular, 1.0f}), strip.x + 56, strip.CentreY() - 13, pages[at].subtitle, Secondary(),
			strip.w - 56 - 28 - version.width - 40, 1);
		EdenLeave(canvas);
	}

	void Shell::EdenPageDraw(Canvas& canvas)
	{
		const bool game = InGameSettings();
		const auto pages = game ? SettingsPages() : EdenBoardPages();
		if (pages.empty())
			return;
		int page = 0;
		for (int i = 0; i < (int)pages.size(); i++)
			if (pages[i].id == m_page)
				page = i;
		m_page = pages[page].id;
		const float clockLeft = EdenDrawClock(canvas);

		// ---- whose settings, and how much of them is its own ----
		EdenArrive(canvas, 0, 0);
		if (game)
		{
			// a game's: its cover, its name, how many of its settings are its own
			const int g = FindGame(m_gameTitle);
			float x = kLeft;
			if (g >= 0)
			{
				Box cover = CoverBox(m_games[g], kLeft, 36 + 92, 92, 120);
				DrawCover(canvas, m_games[g], cover, false, 0);
				x = cover.Right() + 24;
			}
			int own = 0;
			const auto found = m_settings.games.find(ps5settings::GameKey(ps5launcher::SideName(m_side), m_gameTitle));
			if (found != m_settings.games.end())
				own = (int)found->second.size();
			// tr: Eden's game settings, at the top right: how many of the game's settings are its own
			const std::string changed = own ? TrP(own, "{0} setting of its own", "{0} settings of its own") : std::string(Tr("Its side's settings"));
			const float chipW = m_fonts.Width(Style({20, ui::Weight::SemiBold, 1.0f}), changed) + 32 + (own ? 17 : 0);
			Chip(canvas, clockLeft - 28 - chipW, kTop + 15, changed, own ? "good" : "", 34);
			canvas.Text(Style(kOverlineStyle), x, 40, Tr("Game settings"), Pale(Accent()), 900, 1);
			canvas.Text(Style({44, ui::Weight::Bold, 1.1f}), x, 66, m_gameName, kText, clockLeft - 28 - chipW - 32 - x, 1);
		}
		else
			EdenDrawTitle(canvas, TrC("tab", "Settings"), TrF("{0} settings", SystemName(m_side)));
		EdenLeave(canvas);

		// ---- its pages, at the left ----
		EdenArrive(canvas, 1, 0);
		const float pitch = game ? 80.0f : 56.0f;
		for (int i = 0; i < (int)pages.size(); i++)
		{
			const float top = kKinds.y + i * pitch;
			if (top + pitch > kKinds.Bottom() + 8)
				break;
			const bool on = i == page;
			const Box item{kKinds.x, top, kKinds.w, pitch - 8};
			if (on)
			{
				canvas.Rect(item, 16, m_onRows ? Wash(0.06f) : Surface2());
				canvas.Rect({item.x + 12, item.y + 14, 4, item.h - 28}, 2, Accent());
				if (!m_onRows)
					Focus(item, 16);
			}
			canvas.Draw(pages[i].icon, {item.x + 30, item.CentreY() - 13, 26, 26}, on ? Accent() : Tertiary());
			const float tx = item.x + 72, tw = item.w - 72 - 20;
			if (game)
			{
				const ui::TextBlock name =
					canvas.Text(Style({26, on ? ui::Weight::SemiBold : ui::Weight::Medium, 1.0f}), tx, item.y + 10, pages[i].title, on ? kText : Secondary(), tw, 1);
				const std::string state = EdenSummary("game:" + pages[i].id);
				canvas.Text(Style({18, ui::Weight::Regular, 1.0f}), tx, item.y + 16 + name.height, state, on ? Pale(Accent()) : Tertiary(), tw, 1);
			}
			else
			{
				const ui::TextBlock name = m_fonts.Layout(Style({24, on ? ui::Weight::SemiBold : ui::Weight::Medium, 1.0f}), pages[i].title, tw, 1);
				canvas.Text(name, tx, item.CentreY() - name.height * 0.5f, on ? kText : Secondary());
			}
		}
		if (game)
			m_edenSummaries.erase("game:" + pages[m_edenSummaryNext++ % pages.size()].id);
		EdenLeave(canvas);

		// ---- the page: its title, what it is for, its rows ----
		EdenArrive(canvas, 2, 16);
		const SettingsPage& current = pages[page];
		canvas.Text(Style(kHeadingStyle), kMiddle.x, kMiddle.y - 4, current.title, kText, kMiddle.w, 1);
		canvas.Text(Style({20, ui::Weight::Regular, 1.3f}), kMiddle.x, kMiddle.y + 40, current.subtitle, Secondary(), kMiddle.w, 1);
		auto rows = SettingRows(m_page);
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		const float ry = kMiddle.y + 88, rowH = 96;
		const float available = kMiddle.Bottom() - ry;
		const float scroll = std::max(0.0f, (m_settingRow + 1) * (rowH + 12) - available);
		canvas.PushClip({kMiddle.x - 24, ry - 14, kMiddle.w + 48, available + 28});
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const Box box{kMiddle.x, ry + i * (rowH + 12) - scroll, kMiddle.w, rowH};
			if (box.Bottom() < ry - 20 || box.y > kMiddle.Bottom() + 20)
				continue;
			const bool focused = m_onRows && i == m_settingRow;
			DrawRow(canvas, rows[i], box, focused);
			if (focused && rows[i].kind == Row::Kind::Hold)
				HoldRing(canvas, box, m_holdProgress);
		}
		const float under = ry + rows.size() * (rowH + 12) - scroll;
		if (m_page == "remote" && m_settings.remoteScreen && !game)
			DrawRemoteScreen(canvas, {kMiddle.x, under + 6, kMiddle.w, 372});
		canvas.PopClip();
		EdenLeave(canvas);

		// ---- what the focused row is about ----
		EdenArrive(canvas, 3, 16);
		canvas.Rect(kExplain, kRadiusCard, Surface());
		canvas.Ring(kExplain, kRadiusCard, 1.5f, kGlassEdge);
		const float ex = kExplain.x + 28, ew = kExplain.w - 56;
		float ey = kExplain.y + 28;
		const bool onRow = m_onRows && !rows.empty();
		// tr: Eden's settings pages: the heading of the part that says what the focused row is about
		canvas.Text(Style(kOverlineStyle), ex, ey, Tr("About this"), Pale(Accent()), ew, 1);
		ey += 34;
		const ui::TextBlock heading = canvas.Text(Style({28, ui::Weight::SemiBold, 1.2f}), ex, ey, onRow ? rows[m_settingRow].label : current.title, kText, ew, 2);
		ey += heading.height + 14;
		std::string about = onRow ? rows[m_settingRow].description : current.subtitle;
		if (onRow && !rows[m_settingRow].help.empty() && rows[m_settingRow].help != about)
			about += (about.empty() ? "" : "\n\n") + rows[m_settingRow].help;
		const ui::TextStyle body = Style({21, ui::Weight::Regular, 1.45f});
		const int bodyLines = std::max(1, (int)((kExplain.Bottom() - 28 - ey) / (body.size * body.lineHeight)));
		const ui::TextBlock text = canvas.Text(body, ex, ey, about, Secondary(), ew, std::min(bodyLines, 14));
		ey += text.height + 22;
		// what the page says under its rows (the diagnostics, a theme file's shape), as room allows
		const std::string panel = PanelText(m_page);
		const ui::TextStyle small = Style({18, ui::Weight::Regular, 1.4f});
		const int panelLines = (int)((kExplain.Bottom() - 24 - ey) / (small.size * small.lineHeight));
		if (!panel.empty() && panelLines > 0)
			canvas.Text(small, ex, ey, panel, Tertiary(), ew, panelLines);
		EdenLeave(canvas);
	}

	// -- About --------------------------------------------------------------------------------------------

	void Shell::EdenAboutUpdate(const ui::Press& press)
	{
		switch (press.button)
		{
		case Button::Left: FocusBar(); break;
		case Button::Circle:
			m_feedback.Play(ui::Cue::Back);
			EdenGo(0);
			break;
		case Button::Up:
		case Button::Down:
		case Button::Right: m_feedback.Play(ui::Cue::Edge, press.repeat); break;
		default: break;
		}
	}

	void Shell::EdenAboutDraw(Canvas& canvas)
	{
		if (PhantomLayout())
			return PhantomAboutDraw(canvas); // experiments.cpp
		if (TerminalLayout())
			return TermAboutDraw(canvas);
		WantBackdrop(nullptr);
		EdenArrive(canvas, 0, 0);
		EdenDrawTitle(canvas, TrC("app", "About"), Tr("Who made what."));
		EdenLeave(canvas);
		EdenDrawClock(canvas);

		// the mark, the name, the version
		EdenArrive(canvas, 1, 24);
		SideTile(canvas, System::WiiU, {kLeft, 160, 78, 78}, 20, 0xffffffff);
		SideTile(canvas, System::N3ds, {kLeft + 56, 178, 78, 78}, 20, 0xf2ffffff);
		SideTile(canvas, System::Nds, {kLeft + 24, 222, 78, 78}, 20, 0xf2ffffff);
		canvas.Text(Style({56, ui::Weight::Bold, 1.0f}), kLeft + 176, 168, "PS5CEMU-HAR", kText, 900, 1);
		canvas.Text(Style({24, ui::Weight::Medium, 1.0f}), kLeft + 176, 240,
			std::string(Tr("Version")) + " " + ps5update::Readable(PS5CEMU_VERSION) + "  ·  Wii U, 3DS, DS", Secondary(), 900, 1);
		EdenLeave(canvas);

		// who made what, in a panel
		EdenArrive(canvas, 2, 24);
		const Box panel{kLeft, 336, kWidth - 460, 640};
		canvas.Rect(panel, kRadiusCard, Surface());
		canvas.Ring(panel, kRadiusCard, 1.5f, kGlassEdge);
		const ui::TextStyle body = Style({21, ui::Weight::Regular, 1.5f});
		canvas.Text(body, panel.x + 36, panel.y + 30, PanelText("about"), Secondary(), panel.w - 72, (int)((panel.h - 60) / (body.size * body.lineHeight)));
		EdenLeave(canvas);

		// the support card
		EdenArrive(canvas, 3, 24);
		DrawSupport(canvas, {kRight - 420, 336, 420, 560});
		EdenLeave(canvas);
	}

	// -- the launch and the opening ------------------------------------------------------------------------

	void Shell::EdenDrawLaunch(Canvas& canvas)
	{
		if (PhantomLayout())
			return PhantomDrawLaunch(canvas); // experiments.cpp
		if (TerminalLayout())
			return TermDrawLaunch(canvas);
		// the menu dims, the game's cover flies from where it was to the middle, a little past its size
		// before it rests, and its name comes in under it; it stays there while the game loads
		const float total = kLaunchSeconds + 0.25f;
		const float p = std::clamp((float)((m_now - m_launch.at) / total), 0.0f, 1.0f);
		const bool still = m_settings.ui.reduceMotion;
		canvas.Rect({0, 0, 1920, 1080}, 0, ui::SetAlpha(kInk0, 0.88f * ui::ease::CubicOut(p / 0.3f)));
		const float height = 430, width = height * std::clamp(m_launch.aspect, 0.5f, 1.4f);
		const Box rest{960 - width * 0.5f, 190, width, height};
		const float fly = still ? 1.0f : ui::ease::BackOut(p / 0.6f);
		const Box at = Lerp(m_launch.from, rest, fly);
		if (!m_settings.ui.highContrast)
			canvas.Glow(at, kRadiusCover, 110, ui::SetAlpha(Pale(Accent()), 0.2f * std::clamp(fly, 0.0f, 1.0f)));
		canvas.Shadow(at, kRadiusCover, 30, 70, 0xa0000000);
		if (m_launch.game >= 0 && m_launch.game < (int)m_games.size())
			DrawCover(canvas, m_games[m_launch.game], at, false, 0);
		else
			canvas.LinearGradient(at, kRadiusCover, ui::Mix(Accent(), kInk1, 0.4f), kInk1, at.x, at.y, at.x, at.Bottom());
		canvas.Ring(at, kRadiusCover, 1, ui::SetAlpha(Pale(Accent()), 0.3f));
		const float named = still ? std::min(1.0f, p / 0.3f) : ui::ease::CubicOut((p - 0.22f) / 0.45f);
		canvas.PushAlpha(named);
		canvas.PushOffset(0, (1 - named) * 18 * (still ? 0 : 1));
		// tr: Eden's launch, over the game's name: it is starting
		canvas.Text(Style(kOverlineStyle), 960 - 700, 652, Tr("Starting"), Pale(Accent()), 1400, 1, ui::Align::Centre);
		canvas.Text(Style({60, ui::Weight::Bold, 1.1f}), 960 - 700, 686, m_launch.title, kText, 1400, 1, ui::Align::Centre);
		canvas.Text(Style(kBodyStyle), 960 - 700, 772, m_launch.caption, Secondary(), 1400, 1, ui::Align::Centre);
		canvas.PopOffset();
		canvas.PopAlpha();
	}

	void Shell::EdenStartOpening()
	{
		m_openingOn = true;
		m_openingAt = -1;
	}

	bool Shell::EdenOpeningUpdate(const ui::Press& press)
	{
		if (!m_openingOn)
			return false;
		// a press: the launcher at once (the press is the opening's)
		if (press.button != Button::Count)
		{
			m_openingOn = false;
			m_screenAt = m_now - 10;
		}
		return true;
	}

	void Shell::EdenDrawOpening(Canvas& canvas)
	{
		if (PhantomLayout())
			return PhantomDrawOpening(canvas); // experiments.cpp
		if (TerminalLayout())
			return TermDrawOpening(canvas);
		// the picture the PS5 showed while the app started (sce_sys/pic0.dds, as assets/ui/opening.jpg): the
		// splash is let go of once it is drawn here, so the two are one
		static const std::string kPicture = ps5paths::Assets() + "/ui/opening.jpg";
		const ui::Picture& picture = m_images->Get(kPicture);
		const bool still = m_settings.ui.reduceMotion;
		const float reveal = still ? 0.3f : kReveal, handover = still ? 0.45f : kHandover;
		if (m_openingAt < 0)
		{
			if (!picture.texture && !picture.failed && m_now - m_start < 1.5)
			{
				// not read yet: the console's splash is still up over this
				canvas.Rect({0, 0, 1920, 1080}, 0, 0xff000000);
				return;
			}
			m_openingAt = m_now;
			m_screenAt = m_now + reveal + handover * 0.4f; // the section's parts arrive once the picture is half gone
		}
		const float t = (float)(m_now - m_openingAt);
		if (t >= reveal + handover)
		{
			m_openingOn = false;
			return;
		}
		const float leave = ui::ease::InOut((t - reveal) / handover);
		if (picture.texture)
		{
			// as the console showed it, coming a little nearer as it goes
			const float nearer = still ? 0.0f : 0.05f * ui::ease::InOut(t / (reveal + handover));
			canvas.PushAlpha(1 - leave);
			const Box screen = Box{0, 0, 1920, 1080}.Scaled(1 + nearer);
			canvas.ImageCover(picture.texture, picture.width, picture.height, screen, 0, 0xffffffff);
			canvas.PopAlpha();
		}
		if (still)
			return;
		// its three consoles go to their places on the rail, from where the picture has them, as it leaves
		const std::vector<System> sides = RailSides();
		const float fly = ui::ease::InOut((t - reveal) / handover);
		for (int k = 0; k < (int)sides.size(); k++)
		{
			const int side = SideIndex(sides[k]);
			const float size = 180;
			const Box from{kConsoles[side][0] - size * 0.5f, kConsoles[side][1] - size * 0.5f, size, size};
			const Box to = RailSideBox(k, (int)sides.size());
			const Box at = Lerp(from, to, fly);
			const float alpha = std::clamp(fly * 4.0f, 0.0f, 1.0f);
			if (alpha <= 0)
				continue;
			canvas.Glow(at, 40 - 28 * fly, 50, ui::SetAlpha(AccentOf(sides[k]), 0.4f * alpha * (1 - fly)));
			SideTile(canvas, sides[k], at, 40 - 28 * fly, ui::SetAlpha(0xffffffff, alpha));
		}
	}
}
