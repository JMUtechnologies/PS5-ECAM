// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Classic, the launcher of 2.0.0 B, the first the app had:
// ProsperoEden's layout of then in Cemu's blue and Azahar's gold (RmlUi at the time:
// tools/render-layout.py, port/frontend/launcher.cpp and tools/launcher-preview at v2.0.0b, whose
// preview drew the screens this follows). Drawn with the kit over the sections' shared navigation
// (eden.cpp), as Phantom and Terminal are, with everything the app has gained since:
//
// - the header: the side's tile, PS5 CEMU (AZAHAR, MELONDS) and WII U / PS5CEMU-HAR / its version,
//   the sections as words at the right and the sides' tiles after them (Up reaches them, Left and
//   Right go along, Down comes back), the clock; a hairline under it on Home, and one over the hints;
// - Home: Continue playing, the last game in a panel with its cover, Launch game and the side's own
//   second button (Graphic packs on the Wii U, Artic Base on the 3DS, Game settings on the DS), then
//   Recently played, four cards, and View all games;
// - the Library: Your games, a list of rows (cover, name, format) beside the focused game's details
//   (title ID, version, DLC, its file) and its graphic packs' tile;
// - Settings: Preferences, the pages as rows beside On this console's overview of each; a page, or a
//   game's settings, opens as a dialog over them;
// - About: the credits beside Getting started (where the files go) and the support card;
// - a launch of its own: the cover in the middle, the game's name and Starting under it.

#include "shell_internal.h"
#include "../actions.h"
#include "../../app/emulator.h"
#include "../../app/paths.h"
#include "../../app/updates.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		constexpr float kL = 120, kR = 1800;			 // the screens' margins, as 2.0.0 B had them
		constexpr int kSections = 4;
		constexpr int kPlay = 0, kGameSettings = 1, kFirstRecent = 2, kViewAll = 6;
		constexpr float kFootLine = 952, kHintsY = 966; // the hairline over the hints, and the hints
		constexpr Box kLeftPanel{110, 190, 815, 715}, kRightPanel{983, 190, 815, 715};
		constexpr Box kDialog{500, 170, 920, 760}; // a page of Settings, over them

		const char* SectionWord(int section)
		{
			return section == 0 ? Tr("Home") : section == 1 ? TrC("tab", "Library") : section == 2 ? TrC("tab", "Settings") : TrC("app", "About");
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

		Box Lerp(const Box& a, const Box& b, float t)
		{
			return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t};
		}

		// 2.0.0 B's words for its spaced capitals: tracked, in capitals
		ui::TextStyle Caps(float size)
		{
			return {size, ui::Weight::Medium, 1.0f, size * 0.2f, true};
		}
	}

	// -- what every screen has -----------------------------------------------------------------------

	void Shell::ClassicPanel(Canvas& canvas, const Box& box, float radius)
	{
		canvas.Rect(box, radius, m_settings.ui.highContrast ? kInk1 : ui::SetAlpha(ui::Mix(Accent(), 0xff000000, 0.79f), 0.86f));
		canvas.Ring(box, radius, 1.5f, Wash(0.09f));
	}

	Box Shell::ClassicButton(Canvas& canvas, const Box& box, const std::string& label, bool focused, bool primary)
	{
		// 2.0.0 B's buttons: a dark glass bar, the focused one filled with the side's colour from below
		const uint32_t accent = Accent();
		if (focused)
		{
			canvas.LinearGradient(box, 10, ui::Mix(accent, kInk0, 0.45f), ui::Mix(accent, kInk0, 0.72f), box.x, box.y, box.Right(), box.Bottom());
			canvas.Ring(box, 10, 2.5f, ui::Mix(accent, 0xffffffff, 0.15f));
		}
		else
		{
			canvas.Rect(box, 10, primary ? ui::SetAlpha(ui::Mix(accent, kInk0, 0.6f), 0.55f) : Wash(0.07f));
			canvas.Ring(box, 10, 1.5f, primary ? ui::SetAlpha(accent, 0.55f) : Wash(0.14f));
		}
		const ui::TextStyle style = Style({26, ui::Weight::Medium, 1.0f});
		const ui::TextBlock text = m_fonts.Layout(style, label, box.w - 32, 1);
		canvas.Text(text, box.CentreX() - text.width * 0.5f, box.CentreY() - text.height * 0.5f, kText);
		return box;
	}

	void Shell::ClassicTitle(Canvas& canvas, const std::string& title, const std::string& copy)
	{
		// the page's name large at the top left, its line under it (Your games, Settings, About)
		canvas.Text(Style({52, ui::Weight::Medium, 1.1f}), 108, 62, title, kText, 860, 1);
		canvas.Text(Style({22, ui::Weight::Regular, 1.2f}), 110, 132, copy, Secondary(), 860, 1);
	}

	void Shell::ClassicBackdrop(Canvas& canvas)
	{
		// the Homebrew Launchers' grounds 2.0.0 B had: the side's own colour over its bubbles, waves or
		// pixels; a shade over them where a screen of lists lies
		const Box screen{0, 0, 1920, 1080};
		const uint32_t accent = Accent();
		canvas.LinearGradient(screen, 0, ui::SetAlpha(ui::Mix(accent, kInk0, 0.42f), 0.5f), ui::SetAlpha(ui::Mix(accent, kInk0, 0.7f), 0.55f), 0, 0, 0,
			1080);
		m_edenShade.target = m_screen == ScreenId::Home ? 0.0f : 1.0f;
		m_edenShade.omega = m_settings.ui.reduceMotion ? 60.0f : 14.0f;
		m_edenShade.Update(m_dt);
		canvas.Rect(screen, 0, ui::SetAlpha(kInk0, 0.42f * std::clamp(m_edenShade.value, 0.0f, 1.0f)));
	}

	void Shell::ClassicDrawRail(Canvas& canvas)
	{
		// the header: the side and the app at the left (Home's; the other screens have their title there),
		// the sections and the sides at the right, the clock at the far right
		const int section = EdenSectionIndex();
		const uint32_t accent = Accent();
		if (section == 0)
		{
			SideTile(canvas, m_side, {kL, 72, 72, 72}, 14, 0xffffffff);
			const std::string name = std::string("PS5 ") + Upper(EmulatorName(m_side));
			canvas.Text(Style({40, ui::Weight::Medium, 1.0f, 6.0f}), 216, 74, name, kText, 640, 1);
			const std::string line = Upper(SystemName(m_side)) + " / PS5CEMU-HAR / v" + ps5update::Readable(PS5CEMU_VERSION);
			canvas.Text(Style({20, ui::Weight::Medium, 1.0f, 4.0f}), 217, 124, line, Secondary(), 700, 1);
			canvas.Rect({kL, 159, kR - kL, 1.5f}, 0, Wash(0.14f));
		}
		// the clock
		const ui::TextStyle clockStyle = Style({30, ui::Weight::Regular, 1.0f, 0, false, true});
		const ui::TextBlock clock = m_fonts.Layout(clockStyle, Clock(), 200, 1);
		canvas.Text(clock, kR - clock.width, 94, kText);
		// the sides' tiles, right to left from the clock, then the sections' words
		const std::vector<System> sides = RailSides();
		float x = kR - clock.width - 48;
		const float tile = 48, ty = 112 - tile * 0.5f;
		if (sides.size() > 1)
			for (int k = (int)sides.size() - 1; k >= 0; k--)
			{
				x -= tile;
				const Box box{x, ty, tile, tile};
				const bool on = sides[k] == m_side;
				const bool focused = m_railFocus && m_railIndex == kSections + k;
				if (focused)
					canvas.Ring(box.Inset(-6), 14, 3, kText);
				else if (on)
					canvas.Ring(box.Inset(-5), 13, 2, ui::SetAlpha(AccentOf(sides[k]), 0.9f));
				SideTile(canvas, sides[k], box, 10, on || focused ? 0xffffffff : 0x8cffffff);
				x -= 18;
			}
		if (sides.size() > 1)
		{
			x -= 14;
			canvas.Rect({x, 92, 1.5f, 40}, 0, Wash(0.18f));
			x -= 34;
		}
		const ui::TextStyle words = Style({26, ui::Weight::Regular, 1.0f});
		for (int i = kSections - 1; i >= 0; i--)
		{
			const ui::TextBlock text = m_fonts.Layout(words, SectionWord(i), 300, 1);
			x -= text.width;
			const bool lit = i == section;
			const bool focused = m_railFocus && m_railIndex == i;
			if (focused)
			{
				const Box pill{x - 20, 112 - 26, text.width + 40, 52};
				canvas.Rect(pill, 26, ui::SetAlpha(ui::Mix(accent, kInk0, 0.5f), 0.9f));
				canvas.Ring(pill, 26, 2, ui::Mix(accent, 0xffffffff, 0.2f));
			}
			canvas.Text(text, x, 112 - text.height * 0.5f, lit || focused ? kText : Secondary());
			if (lit && !focused)
				canvas.Rect({x, 112 + text.height * 0.5f + 6, text.width, 3}, 1.5f, accent);
			x -= 50;
		}
	}

	void Shell::ClassicDrawHints(Canvas& canvas, const std::vector<Hint>& hints)
	{
		// along the foot from the left, over a hairline; on Home, the side's state at the right
		canvas.Rect({kL, kFootLine, kR - kL, 1.5f}, 0, Wash(0.14f));
		ui::TextStyle style = Style({24, ui::Weight::Regular, 1.0f});
		std::string status;
		if (m_screen == ScreenId::Home)
		{
			status = TrP((long long)m_games.size(), "{0} game", "{0} games");
			if (IsWiiU())
				status += " / " + std::string(m_status.coreReady ? EmulatorName(m_side) : Tr("Cemu is still starting"));
		}
		const float room = kR - kL - (status.empty() ? 0 : m_fonts.Width(Style({20, ui::Weight::Regular, 1.0f}), status) + 40);
		auto width = [&] {
			float total = 0;
			for (const Hint& hint : hints)
				if (!hint.text.empty())
					total += 26 + 12 + m_fonts.Width(style, hint.text) + 44 + (hint.icon == Icon::R1 ? 34 : 0);
			return total;
		};
		while (width() > room && style.size > 17)
			style.size -= 1;
		float x = kL;
		for (const Hint& hint : hints)
		{
			if (hint.text.empty())
				continue;
			if (hint.icon == Icon::R1)
			{
				canvas.Draw(Icon::L1, {x, kHintsY + 2, 28, 28}, Secondary());
				x += 34;
			}
			canvas.Draw(hint.icon, {x, kHintsY + 2, 28, 28}, Secondary());
			x += 26 + 12;
			const ui::TextBlock text = m_fonts.Layout(style, hint.text, 500, 1);
			canvas.Text(text, x, kHintsY + 16 - text.height * 0.5f, Secondary());
			x += text.width + 44;
		}
		if (!status.empty())
		{
			const ui::TextBlock text = m_fonts.Layout(Style({20, ui::Weight::Regular, 1.0f}), status, 600, 1);
			canvas.Text(text, kR - text.width, kHintsY + 16 - text.height * 0.5f, Tertiary());
		}
	}

	void Shell::ClassicStateDraw(Canvas& canvas, const Box& panel)
	{
		// a notice, Cemu starting, the scan, no games: in the hero's place, with its buttons
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		LibraryState(kicker, title, text, buttons);
		m_stateButtons.clear();
		ClassicPanel(canvas, panel, 16);
		SideTile(canvas, m_side, {panel.x + 48, panel.y + 70, 300, 300}, 28, 0xffffffff);
		const float x = panel.x + 440, width = panel.Right() - 48 - x;
		canvas.Text(Style(Caps(22)), x, panel.y + 74, kicker, Accent(), width, 1);
		const ui::TextBlock head = canvas.Text(Style({48, ui::Weight::Medium, 1.15f}), x, panel.y + 116, title, kText, width, 2);
		const ui::TextBlock body = canvas.Text(Style({24, ui::Weight::Regular, 1.4f}), x, panel.y + 136 + head.height, text, Secondary(), width, 4);
		m_stateButton = std::clamp(m_stateButton, 0, std::max(0, (int)buttons.size() - 1));
		float bx = x;
		for (int b = 0; b < (int)buttons.size(); b++)
		{
			const float w = std::max(266.0f, m_fonts.Width(Style({26, ui::Weight::Medium, 1.0f}), buttons[b]) + 64);
			const Box box = ClassicButton(canvas, {bx, panel.y + 170 + head.height + body.height, w, 66}, buttons[b], !m_railFocus && m_stateButton == b, b == 0);
			m_stateButtons.push_back(box);
			bx += w + 22;
		}
	}

	// -- Home -------------------------------------------------------------------------------------------

	void Shell::ClassicHomeDraw(Canvas& canvas)
	{
		WantBackdrop(nullptr); // 2.0.0 B's ground: the side's own, never a game's picture
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				EdenArrive(canvas, 1, 14);
				ClassicStateDraw(canvas, {122, 202, 1676, 474});
				EdenLeave(canvas);
				return;
			}
		}
		const int game = EdenContinueGame();
		const std::vector<int> recents = EdenRecentGames(game);
		const int count = (int)recents.size();
		if (m_homeFocus >= kFirstRecent && m_homeFocus < kViewAll && m_homeFocus - kFirstRecent >= count)
			m_homeFocus = count ? kFirstRecent + count - 1 : kViewAll;
		const bool here = !m_railFocus;
		const uint32_t accent = Accent();
		Game* g = game >= 0 ? &m_games[game] : nullptr;
		m_edenLaunchFrom = {};

		// Continue playing: the last game, its cover, its buttons
		EdenArrive(canvas, 1, 14);
		const Box hero{122, 202, 1676, 474};
		ClassicPanel(canvas, hero, 16);
		const Box square{168, 272, 336, 336};
		if (g)
		{
			canvas.Rect(square, 26, ui::SetAlpha(ui::Mix(accent, kInk0, 0.62f), 0.9f));
			Box art = CoverBox(*g, 0, square.Bottom(), square.h, square.w);
			art.x = square.CentreX() - art.w * 0.5f;
			art.y = square.CentreY() - art.h * 0.5f;
			DrawCover(canvas, *g, art, false, 0);
			if (here && m_homeFocus <= kGameSettings)
				m_edenLaunchFrom = canvas.Placed(art);
		}
		else
			SideTile(canvas, m_side, square, 26, 0xffffffff);
		const float x = 560, width = hero.Right() - 60 - x;
		canvas.Text(Style(Caps(22)), x, 268, Tr("Continue playing"), ui::Mix(accent, 0xffffffff, 0.3f), width, 1);
		canvas.Text(Style({52, ui::Weight::Medium, 1.1f}), x, 312, g ? g->entry.game.name : std::string(Tr("Your next adventure")), kText, width, 1);
		std::string meta;
		if (g)
		{
			const ps5emu::Game& entry = g->entry.game;
			meta = Tr("Last game played");
			if (IsWiiU())
			{
				meta += " / " + (entry.hasUpdate ? TrF("Update v{0}", entry.version) : fmt::format("v{}", entry.version));
				if (entry.dlcCount)
					meta += " / DLC";
			}
			else if (!entry.publisher.empty())
				meta += " / " + entry.publisher;
		}
		else
			meta = Tr("Choose a game from your library.");
		canvas.Text(Style({26, ui::Weight::Regular, 1.2f}), x, 428, meta, Secondary(), width, 1);
		// Launch game, and the side's own second button
		const Box launch{562, 506, 266, 66};
		ClassicButton(canvas, launch, g ? Tr("Launch game") : TrC("tab", "Library"), here && m_homeFocus == kPlay, true);
		if (g)
		{
			const std::string second = IsWiiU() ? Tr("Graphic packs") : Is3ds() ? Tr("Artic Base") : Tr("Game settings");
			const float w = std::max(266.0f, m_fonts.Width(Style({26, ui::Weight::Medium, 1.0f}), second) + 64);
			ClassicButton(canvas, {launch.Right() + 22, 506, w, 66}, second, here && m_homeFocus == kGameSettings, false);
		}
		canvas.Text(Style({20, ui::Weight::Regular, 1.2f}), x, 622,
			TrF("{0}, the {1} emulator, on PlayStation 5", EmulatorName(m_side), SystemName(m_side)), Tertiary(), width, 1);
		EdenLeave(canvas);

		// Recently played, and the way to every game
		EdenArrive(canvas, 2, 14);
		const ui::TextStyle caps = Style(Caps(20));
		canvas.Text(caps, kL, 717, Tr("Recently played"), Secondary(), 800, 1);
		{
			const bool f = here && m_homeFocus == kViewAll;
			const ui::TextBlock all = m_fonts.Layout(caps, Tr("View all games"), 600, 1);
			const Box pill{1776 - all.width - 20, 704, all.width + 40, 46};
			if (f)
			{
				canvas.Rect(pill, 23, ui::SetAlpha(ui::Mix(accent, kInk0, 0.5f), 0.9f));
				canvas.Ring(pill, 23, 2, ui::Mix(accent, 0xffffffff, 0.2f));
			}
			canvas.Text(all, 1776 - all.width, 717, f ? kText : Secondary());
		}
		if (recents.empty())
			canvas.Text(Style({24, ui::Weight::Regular, 1.2f}), kL, 790, Tr("The games you play appear here."), Tertiary(), 1200, 1);
		for (int i = 0; i < count && i < 4; i++)
		{
			Game& recent = m_games[recents[i]];
			const bool f = here && m_homeFocus == kFirstRecent + i;
			const Box card{122 + i * 424.0f, 762, 395, 140};
			if (f)
			{
				canvas.LinearGradient(card, 10, ui::SetAlpha(ui::Mix(accent, kInk0, 0.45f), 0.95f), ui::SetAlpha(ui::Mix(accent, kInk0, 0.72f), 0.95f), card.x,
					card.y, card.Right(), card.Bottom());
				canvas.Ring(card, 10, 2.5f, ui::Mix(accent, 0xffffffff, 0.15f));
			}
			else
			{
				canvas.Rect(card, 10, Wash(0.06f));
				canvas.Ring(card, 10, 1.5f, Wash(0.11f));
			}
			const Box slot{card.x + 22, card.y + 22, 96, 96};
			Box art = CoverBox(recent, 0, slot.Bottom(), slot.h, slot.w);
			art.x = slot.CentreX() - art.w * 0.5f;
			art.y = slot.CentreY() - art.h * 0.5f;
			DrawCover(canvas, recent, art, false, 0);
			canvas.Text(Style({22, ui::Weight::Regular, 1.25f}), card.x + 142, card.y + 28, recent.entry.game.name, kText, card.w - 164, 3);
			if (f)
				m_edenLaunchFrom = canvas.Placed(art);
		}
		EdenLeave(canvas);
	}

	// -- the Library --------------------------------------------------------------------------------------

	void Shell::ClassicLibraryDraw(Canvas& canvas)
	{
		WantBackdrop(nullptr);
		ClassicTitle(canvas, Tr("Your games"), m_search.empty() ? std::string(Tr("Select a game to begin")) : TrF("Nothing matches “{0}”", m_search));
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				EdenArrive(canvas, 1, 12);
				ClassicStateDraw(canvas, {110, 190, 1688, 715});
				EdenLeave(canvas);
				return;
			}
		}
		const bool still = m_settings.ui.reduceMotion;
		const uint32_t accent = Accent();
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		m_libraryIndex = std::clamp(m_libraryIndex, 0, std::max(0, total - 1));
		const int game = total ? LibraryGame() : -1;
		m_edenLaunchFrom = {};

		// the list
		EdenArrive(canvas, 1, 12);
		ClassicPanel(canvas, kLeftPanel, 22);
		canvas.Text(Style(Caps(20)), kLeftPanel.x + 28, kLeftPanel.y + 22, TrC("tab", "Library"), Secondary(), 600, 1);
		constexpr float kPitch = 88, kRowH = 72;
		const float top = kLeftPanel.y + 62, view = 7 * kPitch - (kPitch - kRowH);
		float target = m_edenScroll.target;
		if (m_libraryIndex * kPitch < target)
			target = m_libraryIndex * kPitch;
		if (m_libraryIndex * kPitch + kRowH - target > view)
			target = m_libraryIndex * kPitch + kRowH - view;
		m_edenScroll.target = std::max(0.0f, target);
		m_edenScroll.omega = still ? 60.0f : 24.0f;
		m_edenScroll.Update(m_dt);
		const float scroll = m_edenScroll.value;
		canvas.PushClip({kLeftPanel.x, top - 6, kLeftPanel.w, view + 12});
		const ui::TextStyle name = Style({28, ui::Weight::Regular, 1.0f});
		const ui::TextStyle meta = Style({22, ui::Weight::Regular, 1.0f});
		for (int index = std::max(0, (int)(scroll / kPitch)); index < total; index++)
		{
			const Box row{kLeftPanel.x + 30, top + index * kPitch - scroll, 755, kRowH};
			if (row.y > top + view)
				break;
			const bool focused = !m_railFocus && index == m_libraryIndex;
			if (focused)
			{
				canvas.LinearGradient(row, 10, ui::SetAlpha(ui::Mix(accent, kInk0, 0.45f), 0.95f), ui::SetAlpha(ui::Mix(accent, kInk0, 0.75f), 0.95f), row.x,
					row.y, row.Right(), row.y);
				canvas.Ring(row, 10, 2, ui::Mix(accent, 0xffffffff, 0.15f));
			}
			else
			{
				canvas.Rect(row, 10, Wash(0.05f));
				canvas.Ring(row, 10, 1.2f, Wash(0.08f));
			}
			const Box slot{row.x + 18, row.y + 8, 56, 56};
			if (index < offset)
			{
				SideTile(canvas, System::N3ds, slot, 8, 0xffffffff);
				canvas.Text(name, row.x + 90, row.CentreY() - 15, Tr("Play from your 3DS"), kText, 520, 1);
				canvas.Text(meta, row.Right() - 28 - m_fonts.Width(meta, "ARTIC"), row.CentreY() - 12, "ARTIC", Secondary(), 120, 1);
				continue;
			}
			Game& g = m_games[m_shelf[index - offset]];
			Box art = CoverBox(g, 0, slot.Bottom(), slot.h, slot.w);
			art.x = slot.CentreX() - art.w * 0.5f;
			art.y = slot.CentreY() - art.h * 0.5f;
			DrawCover(canvas, g, art, false, 0);
			canvas.Text(name, row.x + 90, row.CentreY() - 15, g.entry.game.name, kText, 520, 1);
			const std::string format = Upper(g.entry.game.format);
			canvas.Text(meta, row.Right() - 28 - std::min(110.0f, m_fonts.Width(meta, format)), row.CentreY() - 12, format, Secondary(), 110, 1);
		}
		canvas.PopClip();
		// the scroll bar and where the focus is
		if (total > 7)
		{
			const Box track{kLeftPanel.Right() - 18, top, 5, view};
			canvas.Rect(track, 2.5f, Wash(0.1f));
			const float thumb = std::max(40.0f, view * 7 / total);
			const float at = total > 1 ? (view - thumb) * m_libraryIndex / (total - 1) : 0;
			canvas.Rect({track.x, track.y + at, track.w, thumb}, 2.5f, accent);
		}
		{
			const ui::TextBlock where = m_fonts.Layout(Style(Caps(20)), total ? TrF("{0} of {1}", m_libraryIndex + 1, total) : std::string("0"), 300, 1);
			canvas.Text(where, kLeftPanel.Right() - 28 - where.width, kLeftPanel.Bottom() - 38, Secondary());
		}
		EdenLeave(canvas);

		// the focused game's details
		EdenArrive(canvas, 2, 12);
		ClassicPanel(canvas, kRightPanel, 22);
		const float x = kRightPanel.x + 33, width = kRightPanel.w - 66;
		canvas.Text(Style(Caps(20)), x, kRightPanel.y + 22, Tr("Game details"), Secondary(), width, 1);
		if (game >= 0)
		{
			Game& g = m_games[game];
			const ps5emu::Game& entry = g.entry.game;
			canvas.Text(Style({34, ui::Weight::Regular, 1.15f}), x, kRightPanel.y + 62, entry.name, kText, width, 2);
			const Box slot{x, 372, 288, 288};
			Box art = CoverBox(g, 0, slot.Bottom(), slot.h, slot.w);
			art.x = slot.CentreX() - art.w * 0.5f;
			art.y = slot.CentreY() - art.h * 0.5f;
			DrawCover(canvas, g, art, false, 0);
			if (!m_railFocus)
				m_edenLaunchFrom = canvas.Placed(art);
			// title ID, version, DLC (the Wii U); publisher and format (the handhelds); its file
			std::vector<std::pair<std::string, std::string>> facts;
			if (IsWiiU())
			{
				facts = {{Tr("Title ID"), Hex(entry.titleId)},
					{Tr("Version"), entry.hasUpdate ? TrF("Update v{0}", entry.version) : fmt::format("v{}", entry.version)},
					{"DLC", entry.dlcCount ? TrP((long long)entry.dlcCount, "{0} installed", "{0} installed") : std::string(Tr("None"))}};
			}
			else
				facts = {{Tr("Title ID"), IsDs() ? entry.gameId : Hex(entry.titleId)}, {Tr("Publisher"), entry.publisher.empty() ? "-" : entry.publisher},
					{Tr("Format"), entry.format}};
			const float fx = 1336, fw = kRightPanel.Right() - 22 - fx;
			float y = 392;
			const ui::TextStyle label = Style(Caps(20));
			const ui::TextStyle value = Style({22, ui::Weight::Regular, 1.0f});
			for (const auto& [key, text] : facts)
			{
				canvas.Text(label, fx, y, key, Secondary(), 220, 1);
				const ui::TextBlock v = m_fonts.Layout(value, text, fw - 180, 1);
				canvas.Text(v, kRightPanel.Right() - 22 - v.width, y - 2, kText);
				y += 58;
			}
			canvas.Text(label, fx, y + 18, Tr("Files"), Secondary(), 300, 1);
			canvas.Text(Style({22, ui::Weight::Regular, 1.35f}), fx, y + 54, ShortPath(entry.path.string(), 60), kText, fw, 2);
			// its graphic packs (the Wii U's), else its own settings
			const Box tile{1023, 740, 733, 88};
			canvas.Rect(tile, 12, Wash(0.06f));
			canvas.Ring(tile, 12, 1.5f, Wash(0.1f));
			std::string what, value2, hint;
			if (IsWiiU() && m_status.coreReady)
			{
				if (g.packsOn < 0)
					g.packsOn = ps5emu::EnabledGraphicPackCount(entry.titleId);
				what = Tr("Graphic packs");
				value2 = g.packsOn > 0 ? TrP(g.packsOn, "{0} on", "{0} on") : std::string(Tr("None on"));
				hint = Tr("Choose this game's graphic packs.");
			}
			else
			{
				const int own = OwnSettings(g);
				what = Tr("Game settings");
				value2 = own ? TrP(own, "{0} of its own", "{0} of its own") : std::string(Tr("Its side's"));
				hint = Tr("Choose this game's own settings.");
			}
			canvas.Text(Style({28, ui::Weight::Regular, 1.0f}), tile.x + 25, tile.CentreY() - 15, what, kText, 420, 1);
			const ui::TextBlock v = m_fonts.Layout(Style({28, ui::Weight::Regular, 1.0f}), value2, 280, 1);
			canvas.Text(v, tile.Right() - 25 - v.width, tile.CentreY() - 15, kText);
			canvas.Draw(Icon::Triangle, {1024, 850, 26, 26}, Secondary());
			canvas.Text(Style({22, ui::Weight::Regular, 1.0f}), 1062, 850, hint, Secondary(), 700, 1);
		}
		else if (offset > 0 && m_libraryIndex < offset)
		{
			canvas.Text(Style({34, ui::Weight::Regular, 1.15f}), x, kRightPanel.y + 62, Tr("Play from your 3DS"), kText, width, 2);
			SideTile(canvas, System::N3ds, {x, 372, 288, 288}, 26, 0xffffffff);
			canvas.Text(Style({24, ui::Weight::Regular, 1.4f}), 1336, 392, Tr("Artic Base"), kText, 440, 1);
		}
		EdenLeave(canvas);
	}

	// -- Settings -------------------------------------------------------------------------------------------

	void Shell::ClassicSettingsDraw(Canvas& canvas)
	{
		WantBackdrop(nullptr);
		const bool game = InGameSettings();
		const auto pages = game ? SettingsPages() : EdenBoardPages();
		if (pages.empty())
			return;
		int page = 0;
		for (int i = 0; i < (int)pages.size(); i++)
			if (pages[i].id == m_page)
				page = i;
		m_page = pages[page].id;
		const bool still = m_settings.ui.reduceMotion;
		const uint32_t accent = Accent();
		const bool dialog = game || m_onRows;
		ClassicTitle(canvas, game ? m_gameName : std::string(TrC("tab", "Settings")), game ? std::string(Tr("Game settings")) : std::string(Tr("Fine-tune your experience")));

		// Preferences: the pages
		EdenArrive(canvas, 1, 12);
		ClassicPanel(canvas, kLeftPanel, 22);
		canvas.Text(Style(Caps(20)), kLeftPanel.x + 28, kLeftPanel.y + 22, Tr("Preferences"), Secondary(), 600, 1);
		constexpr float kPitch = 104, kRowH = 88;
		const float top = kLeftPanel.y + 62, view = kLeftPanel.Bottom() - 24 - top;
		const float scroll = std::max(0.0f, (page + 1) * kPitch - view);
		canvas.PushClip({kLeftPanel.x, top - 6, kLeftPanel.w, view + 12});
		for (int i = 0; i < (int)pages.size(); i++)
		{
			const Box row{kLeftPanel.x + 42, top + i * kPitch - scroll, 731, kRowH};
			if (row.Bottom() < top - 10 || row.y > top + view)
				continue;
			const bool on = i == page;
			const bool focused = on && !m_railFocus && !dialog;
			if (focused || (on && dialog))
			{
				canvas.LinearGradient(row, 12, ui::SetAlpha(ui::Mix(accent, kInk0, 0.45f), focused ? 0.95f : 0.5f),
					ui::SetAlpha(ui::Mix(accent, kInk0, 0.78f), focused ? 0.95f : 0.5f), row.x, row.y, row.Right(), row.y);
				canvas.Ring(row, 12, 2, ui::SetAlpha(ui::Mix(accent, 0xffffffff, 0.15f), focused ? 1.0f : 0.5f));
			}
			else
			{
				canvas.Rect(row, 12, Wash(0.05f));
				canvas.Ring(row, 12, 1.2f, Wash(0.08f));
			}
			canvas.Text(Style({28, ui::Weight::Regular, 1.0f}), row.x + 34, row.CentreY() - 15, pages[i].title, kText, row.w - 68, 1);
		}
		canvas.PopClip();
		EdenLeave(canvas);

		// On this console: what each page holds
		EdenArrive(canvas, 2, 12);
		ClassicPanel(canvas, kRightPanel, 22);
		{
			const float x = kRightPanel.x + 33, width = kRightPanel.w - 66;
			canvas.Text(Style(Caps(20)), x, kRightPanel.y + 24, game ? std::string(Tr("Game settings")) : std::string(Tr("On this console")), Secondary(), width,
				1);
			canvas.Text(Style({44, ui::Weight::Regular, 1.1f}), x, kRightPanel.y + 70, game ? m_gameName : std::string(Tr("Make it yours")), kText, width, 1);
			const ui::TextBlock copy = canvas.Text(Style({26, ui::Weight::Regular, 1.35f}), x, kRightPanel.y + 148,
				game ? Tr("Settings of its own, over its side's.") : Tr("Adjust the essentials without leaving your library behind."), Secondary(), width, 2);
			float y = kRightPanel.y + 180 + copy.height;
			const int first = std::clamp(page - 3, 0, std::max(0, (int)pages.size() - 7));
			for (int i = first; i < (int)pages.size() && y < kRightPanel.Bottom() - 50; i++)
			{
				canvas.Text(Style(Caps(20)), x, y, pages[i].title, i == page ? accent : Secondary(), 210, 1);
				canvas.Text(Style({22, ui::Weight::Regular, 1.0f}), x + 228, y - 2, pages[i].subtitle, kText, width - 228, 1);
				y += 64;
			}
		}
		EdenLeave(canvas);

		// a page's rows: a dialog over the two
		if (!dialog)
			return;
		canvas.Rect({0, 0, 1920, 1080}, 0, ui::SetAlpha(kInk0, 0.55f));
		canvas.PushAlpha(still ? 1.0f : std::clamp((float)(m_now - m_screenAt) / 0.18f, 0.0f, 1.0f));
		canvas.Rect(kDialog, 22, m_settings.ui.highContrast ? kInk1 : ui::SetAlpha(kInk1, 0.97f));
		canvas.Ring(kDialog, 22, 1.5f, Wash(0.12f));
		canvas.Text(Style({52, ui::Weight::Medium, 1.1f}), kDialog.x + 40, kDialog.y + 34, pages[page].title, kText, kDialog.w - 80, 1);
		canvas.Text(Style({22, ui::Weight::Regular, 1.2f}), kDialog.x + 40, kDialog.y + 106, pages[page].subtitle, Secondary(), kDialog.w - 80, 1);
		auto rows = SettingRows(m_page);
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		const float ry = kDialog.y + 156, rowH = 96, gap = 14, available = kDialog.Bottom() - 150 - ry;
		const float rowScroll = std::max(0.0f, (m_settingRow + 1) * (rowH + gap) - available);
		canvas.PushClip({kDialog.x + 8, ry - 10, kDialog.w - 16, available + 20});
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const Box box{kDialog.x + 40, ry + i * (rowH + gap) - rowScroll, kDialog.w - 80, rowH};
			if (box.Bottom() < ry - 20 || box.y > ry + available + 20)
				continue;
			const bool focused = m_onRows && i == m_settingRow;
			DrawRow(canvas, rows[i], box, focused);
			if (focused && rows[i].kind == Row::Kind::Hold)
				HoldRing(canvas, box, m_holdProgress);
		}
		const float under = ry + rows.size() * (rowH + gap) - rowScroll;
		if (m_page == "remote" && m_settings.remoteScreen && !game)
			DrawRemoteScreen(canvas, {kDialog.x + 40, under + 6, kDialog.w - 80, 372});
		canvas.PopClip();
		// the focused row's help, under the rows
		canvas.Rect({kDialog.x + 40, kDialog.Bottom() - 132, kDialog.w - 80, 1.5f}, 0, Wash(0.12f));
		std::string help = m_onRows && !rows.empty() ? rows[m_settingRow].help : PanelText(m_page);
		if (help.empty() && m_onRows && !rows.empty())
			help = rows[m_settingRow].description;
		canvas.Text(Style({20, ui::Weight::Regular, 1.4f}), kDialog.x + 40, kDialog.Bottom() - 116, help, Secondary(), kDialog.w - 80, 3);
		canvas.PopAlpha();
	}

	// -- About ---------------------------------------------------------------------------------------------

	void Shell::ClassicAboutDraw(Canvas& canvas)
	{
		WantBackdrop(nullptr);
		ClassicTitle(canvas, std::string(TrC("app", "About")) + " PS5CEMU-HAR", Tr("Credits and setup"));
		EdenArrive(canvas, 1, 12);
		ClassicPanel(canvas, kLeftPanel, 22);
		{
			const float x = kLeftPanel.x + 34, width = kLeftPanel.w - 68;
			canvas.Text(Style(Caps(20)), x, kLeftPanel.y + 24, Tr("Project credits"), Secondary(), width, 1);
			canvas.Text(Style({44, ui::Weight::Regular, 1.1f}), x, kLeftPanel.y + 64, TrF("Powered by {0}", EmulatorName(m_side)), kText, width, 1);
			canvas.Text(Style({21, ui::Weight::Regular, 1.45f}), x, kLeftPanel.y + 136, PanelText("about"), Secondary(), width, 18);
		}
		EdenLeave(canvas);
		EdenArrive(canvas, 2, 12);
		ClassicPanel(canvas, kRightPanel, 22);
		{
			const float x = kRightPanel.x + 33, width = kRightPanel.w - 66;
			canvas.Text(Style(Caps(20)), x, kRightPanel.y + 24, Tr("Getting started"), Secondary(), width, 1);
			canvas.Text(Style({40, ui::Weight::Regular, 1.1f}), x, kRightPanel.y + 64, Tr("Supply your own files"), kText, width, 1);
			const std::string saves = IsWiiU() ? std::string(ps5paths::kMlc) :
				Is3ds()							   ? std::string(ps5paths::kRoot) + "/azahar/sdmc" :
													 std::string(ps5paths::kRoot) + "/melonds/saves";
			std::vector<std::pair<std::string, std::string>> paths = {{Tr("Games"), GamesFolder(m_side)}, {Tr("Saves"), saves}};
			if (IsWiiU())
				paths.insert(paths.begin() + 1, {Tr("Keys"), std::string(ps5paths::kRoot) + "/keys.txt"});
			// the paths in a column at the left, the support card beside them
			const Box card{kRightPanel.Right() - 30 - 380, kRightPanel.y + 130, 380, 560};
			const float column = card.x - 30 - x;
			float y = kRightPanel.y + 150;
			for (const auto& [key, path] : paths)
			{
				canvas.Text(Style(Caps(18)), x, y, key, Secondary(), column, 1);
				const ui::TextBlock value = canvas.Text(Style({22, ui::Weight::Regular, 1.3f}), x, y + 34, path, kText, column, 3);
				y += 34 + value.height + 30;
			}
			DrawSupport(canvas, card);
		}
		EdenLeave(canvas);
	}

	// -- the launch ------------------------------------------------------------------------------------------

	void Shell::ClassicDrawLaunch(Canvas& canvas)
	{
		// 2.0.0 B's: the screen dims to the side's ground, the cover in the middle, the name and Starting
		const float total = kLaunchSeconds + 0.25f;
		const float p = std::clamp((float)((m_now - m_launch.at) / total), 0.0f, 1.0f);
		const bool still = m_settings.ui.reduceMotion;
		const float in = still ? 1.0f : ui::ease::CubicOut(p / 0.35f);
		canvas.Rect({0, 0, 1920, 1080}, 0, ui::SetAlpha(ui::Mix(Accent(), 0xff000000, 0.72f), 0.9f * in));
		const Box rest{816, 300, 288, 288};
		if (m_launch.game >= 0 && m_launch.game < (int)m_games.size())
		{
			Game& g = m_games[m_launch.game];
			Box art = CoverBox(g, 0, rest.Bottom(), rest.h, rest.w);
			art.x = rest.CentreX() - art.w * 0.5f;
			art.y = rest.CentreY() - art.h * 0.5f;
			const Box at = m_launch.from.w > 0 && !still ? Lerp(m_launch.from, art, ui::ease::CubicOut(p / 0.5f)) : art;
			DrawCover(canvas, g, at, false, 0);
		}
		else
			SideTile(canvas, m_side, rest, 26, 0xffffffff);
		canvas.PushAlpha(in);
		const ui::TextBlock title = m_fonts.Layout(Style({48, ui::Weight::Regular, 1.1f}), m_launch.title, 1500, 1);
		canvas.Text(title, 960 - title.width * 0.5f, 624, kText);
		const ui::TextBlock caption = m_fonts.Layout(Style({24, ui::Weight::Regular, 1.0f}), m_launch.caption, 1200, 1);
		canvas.Text(caption, 960 - caption.width * 0.5f, 700, Secondary());
		canvas.PopAlpha();
	}

	void Shell::ClassicDrawOpening(Canvas& canvas)
	{
		// 2.0.0 B had none: the launcher at once
		(void)canvas;
		m_openingOn = false;
	}
}
