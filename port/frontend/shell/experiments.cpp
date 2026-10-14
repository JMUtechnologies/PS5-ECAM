// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the experimental launchers, Phantom and Terminal.
// They share Eden's sections and navigation (eden.cpp: the rail with Home, Library, Settings and
// About, L1 and R1 between them, the sides at its foot; Home's and the Library's focus) and draw every
// part their own way:
//
// - Phantom, after a heist game's menus: black, white and the side's one loud colour; everything on
//   slanted plates; the rail's names in large capitals; Home's game name on alternating bands, its
//   cover on a turning burst; the Library as a list of names, the focused one thrown forward, its cover
//   on a burst beside them; Settings as the list of pages and their rows; bands slashing across as it
//   opens; a burst and a band with the game's name as one starts.
// - Terminal, after a hacker's console: panels with their titles on their edges, a status line, the
//   sections as a file tree, Home as the last session and its history, the Library as a table with the
//   focused game's details beside it, Settings as a tree of pages, a log of what was done along the
//   foot, scanlines and a tube's vignette over everything; a boot log as it opens, a run log as a game
//   starts.
//
// Porpoise-X's Terminal and Broadcast themes (scanlines over the whole screen) are drawn there by a
// shader; here the kit's shapes do the same, as the kit draws everything else.

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
		// the rail's geometry, as eden.cpp has it (the navigation is shared)
		constexpr float kRail = 120, kRailWide = 372, kRailTile = 72, kRailPitch = 90;
		constexpr float kLeft = 184, kRight = 1824, kWidth = kRight - kLeft, kTop = 44, kFooter = 1026;
		constexpr int kSections = 4;
		constexpr int kPlay = 0, kGameSettings = 1, kFirstRecent = 2, kViewAll = 6;
		constexpr float kPi = 3.14159265f;

		// tr: the Library's orders (Square), as library.cpp names them
		constexpr const char* kSortNames[] = {TrMark("Recently played"), TrMark("A to Z"), TrMark("Release year"), TrMark("How it runs")};
		// the sections as Terminal's tree names them: a path, the same in every language
		constexpr const char* kPaths[kSections] = {"~/home", "~/library", "~/settings", "~/about"};

		float RailTileTop(int index)
		{
			return 540 - (kRailPitch * (kSections - 1) + kRailTile) * 0.5f + kRailPitch * index;
		}

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
			return section == 0 ? Tr("Home") : section == 1 ? TrC("tab", "Library") : section == 2 ? TrC("tab", "Settings") : TrC("app", "About");
		}

		ui::Icon SectionIcon(int section)
		{
			return section == 0 ? ui::Icon::Home : section == 1 ? ui::Icon::Grid : section == 2 ? ui::Icon::Gear : ui::Icon::Info;
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

		// text in lines no wider than width, at most maxLines (the last one cut by the layout if it must be)
		std::vector<std::string> Wrap(ui::Fonts& fonts, const ui::TextStyle& style, const std::string& text, float width, int maxLines)
		{
			std::vector<std::string> lines;
			std::string line;
			size_t at = 0;
			while (at < text.size())
			{
				size_t end = text.find(' ', at);
				if (end == std::string::npos)
					end = text.size();
				const std::string word = text.substr(at, end - at);
				const std::string tried = line.empty() ? word : line + " " + word;
				if (!line.empty() && fonts.Width(style, tried) > width && (int)lines.size() + 1 < maxLines)
				{
					lines.push_back(line);
					line = word;
				}
				else
					line = tried;
				at = end + 1;
			}
			if (!line.empty())
				lines.push_back(line);
			return lines;
		}
	}

	int Shell::SectionColumns() const
	{
		return EdenLayout() ? (IsWiiU() ? 5 : 4) : 1;
	}

	void Shell::TermLog(const std::string& line)
	{
		m_termLog.push_back(line);
		if (m_termLog.size() > 24)
			m_termLog.erase(m_termLog.begin());
		m_termLogAt = m_now;
	}

	// == Phantom ===========================================================================================

	void Shell::PhantomPlate(Canvas& canvas, const Box& b, float skew, uint32_t colour)
	{
		// its top edge skew to the right of its foot
		canvas.Triangle(b.x, b.Bottom(), b.x + skew, b.y, b.Right() + skew, b.y, colour);
		canvas.Triangle(b.x, b.Bottom(), b.Right() + skew, b.y, b.Right(), b.Bottom(), colour);
	}

	void Shell::PhantomBurst(Canvas& canvas, float cx, float cy, float radius, uint32_t colour, float turn)
	{
		constexpr int kSpikes = 16;
		const float inner = radius * 0.6f;
		for (int i = 0; i < kSpikes; i++)
		{
			const float a0 = turn + i * 2 * kPi / kSpikes, a1 = a0 + kPi / kSpikes, a2 = a0 + 2 * kPi / kSpikes;
			const float outer = radius * (i % 3 == 0 ? 1.0f : i % 3 == 1 ? 0.84f : 0.92f);
			canvas.Triangle(cx + std::cos(a0) * inner, cy + std::sin(a0) * inner, cx + std::cos(a1) * outer, cy + std::sin(a1) * outer,
				cx + std::cos(a2) * inner, cy + std::sin(a2) * inner, colour);
			canvas.Triangle(cx, cy, cx + std::cos(a0) * inner, cy + std::sin(a0) * inner, cx + std::cos(a2) * inner, cy + std::sin(a2) * inner, colour);
		}
	}

	float Shell::PhantomTag(Canvas& canvas, float x, float y, const std::string& text, uint32_t ground, uint32_t ink, float size)
	{
		if (text.empty())
			return 0;
		const ui::TextStyle style = Style({size, ui::Weight::Bold, 1.0f, 1.5f, true});
		const ui::TextBlock block = m_fonts.Layout(style, text, 900, 1);
		const float h = std::round(size * 1.7f), pad = std::round(size * 0.7f), skew = h * 0.3f;
		const Box box{x, y, block.width + pad * 2, h};
		PhantomPlate(canvas, box, skew, ground);
		canvas.Text(block, x + pad + skew * 0.5f, y + (h - block.height) * 0.5f, ink);
		return box.w + skew;
	}

	void Shell::PhantomBackdrop(Canvas& canvas)
	{
		// the screens' shade: dark at the left where the words are, the artwork and the bands at the right;
		// a field of halftone dots in the side's colour in the lower right
		canvas.LinearGradient({0, 0, 1920, 1080}, 0, ui::SetAlpha(kInk0, 0.92f), ui::SetAlpha(kInk0, 0.35f), 0, 0, 1920, 0);
		const uint32_t accent = Accent();
		for (float y = 620; y < 1080; y += 34)
			for (float x = 1060 + std::fmod(y, 68.0f) * 0.5f; x < 1920; x += 34)
			{
				const float t = std::clamp((x - 1060) / 860.0f * 0.6f + (y - 620) / 460.0f * 0.4f, 0.0f, 1.0f);
				const float r = 1.5f + 6.5f * t;
				canvas.Rect({x - r, y - r, r * 2, r * 2}, r, ui::SetAlpha(accent, 0.22f));
			}
	}

	void Shell::PhantomDrawRail(Canvas& canvas)
	{
		const bool still = m_settings.ui.reduceMotion;
		m_railOpen.target = m_railFocus ? 1.0f : 0.0f;
		m_railOpen.omega = still ? 60.0f : 18.0f;
		m_railOpen.Update(m_dt);
		const float open = std::clamp(m_railOpen.value, 0.0f, 1.0f);
		const float width = kRail + (kRailWide + 120 - kRail) * open;
		const int section = EdenSectionIndex();
		const uint32_t accent = Accent();
		float slide = 0;
		if (m_openingOn && !still)
			slide = -width * (1 - ui::ease::CubicOut(m_openingAt < 0 ? 0.0f : (float)((m_now - m_openingAt - 0.35) / 0.4)));
		canvas.PushOffset(slide, 0);
		// open: the screen beside it dimmed, so what it covers half of reads as behind it
		if (open > 0.01f)
			canvas.Rect({width - 60, 0, 1920 - width + 60, 1080}, 0, ui::SetAlpha(0xff000000, 0.6f * open));
		// a black band, its right edge cut at a slant, a stripe of the side's colour along it
		PhantomPlate(canvas, {0, 0, width, 1080}, -60, ui::SetAlpha(0xff000000, 0.95f + 0.05f * open));
		PhantomPlate(canvas, {width - 4, 0, 10, 1080}, -60, accent);
		SideTile(canvas, System::WiiU, {28, 38, 36, 36}, 4, 0xffffffff);
		SideTile(canvas, System::N3ds, {54, 47, 36, 36}, 4, 0xebffffff);
		SideTile(canvas, System::Nds, {39, 68, 36, 36}, 4, 0xebffffff);

		for (int i = 0; i < kSections; i++)
		{
			const bool lit = i == section;
			const bool focused = m_railFocus && m_railIndex == i;
			const Box tile{(kRail - kRailTile) * 0.5f, RailTileTop(i), kRailTile, kRailTile};
			if (focused)
				PhantomPlate(canvas, tile.Offset(6, 6), 14, 0xffffffff);
			if (lit || focused)
				PhantomPlate(canvas, tile, 14, focused ? accent : ui::SetAlpha(accent, 0.85f));
			canvas.Draw(SectionIcon(i), {tile.x + 20, tile.y + 20, 32, 32}, lit || focused ? 0xff000000 : 0xffffffff);
			if (open > 0.01f)
			{
				// the names in large capitals, each on its plate, stepping right as they go down
				canvas.PushAlpha(open);
				const ui::TextStyle style = Style({focused ? 44.0f : 36.0f, ui::Weight::Bold, 1.0f, 1.0f, true});
				const ui::TextBlock name = m_fonts.Layout(style, SectionName(i), kRailWide - 60, 1);
				const float nx = tile.Right() + 22 + i * 12 + (focused ? 18 : 0);
				const Box plate{nx - 14, tile.CentreY() - name.height * 0.5f - 8, name.width + 28, name.height + 16};
				if (focused)
					PhantomPlate(canvas, plate.Offset(7, 7), 16, accent);
				PhantomPlate(canvas, plate, 16, focused ? 0xffffffff : lit ? accent : 0xff000000);
				canvas.Text(name, nx + 6, plate.y + 8, focused || lit ? 0xff000000 : 0xffffffff);
				canvas.PopAlpha();
			}
		}
		const std::vector<System> sides = RailSides();
		for (int k = 0; k < (int)sides.size(); k++)
		{
			const Box box = RailSideBox(k, (int)sides.size());
			const bool on = sides[k] == m_side;
			const bool focused = m_railFocus && m_railIndex == kSections + k;
			if (on || focused)
				PhantomPlate(canvas, box.Inset(-7), 10, focused ? 0xffffffff : AccentOf(sides[k]));
			SideTile(canvas, sides[k], box, 6, on || focused ? 0xffffffff : 0x8cffffff);
			if (open > 0.01f)
			{
				canvas.PushAlpha(open);
				canvas.Text(Style({26, ui::Weight::Bold, 1.0f, 1.0f, true}), box.Right() + 30, box.CentreY() - 14, SideTitle(sides[k]),
					on || focused ? 0xffffffff : 0x99ffffff, 300, 1);
				canvas.PopAlpha();
			}
		}
		canvas.PopOffset();
	}

	void Shell::PhantomDrawHints(Canvas& canvas, const std::vector<Hint>& hints)
	{
		// black tags along the foot, from the right; smaller when a language's words would take them under
		// the rail
		ui::TextStyle style = Style({20, ui::Weight::Bold, 1.0f, 1.0f, true});
		auto width = [&] {
			float total = 0;
			for (const Hint& hint : hints)
				if (!hint.text.empty())
					total += 16 + (hint.icon == Icon::R1 ? 66 : 32) + 10 + m_fonts.Width(style, hint.text) + 20 + 18;
			return total;
		};
		while (width() > kWidth && style.size > 14)
			style.size -= 1;
		float x = kRight;
		const float y = kFooter - 22;
		for (auto it = hints.rbegin(); it != hints.rend(); ++it)
		{
			if (it->text.empty())
				continue;
			const ui::TextBlock text = m_fonts.Layout(style, it->text, 500, 1);
			const float icons = it->icon == Icon::R1 ? 66 : 32;
			const float w = 16 + icons + 10 + text.width + 20;
			x -= w;
			PhantomPlate(canvas, {x, y, w, 44}, 12, 0xe6000000);
			float ix = x + 16;
			if (it->icon == Icon::R1)
			{
				canvas.Draw(Icon::L1, {ix, y + 8, 28, 28}, 0xffffffff);
				ix += 34;
			}
			canvas.Draw(it->icon, {ix, y + 8, 28, 28}, it->icon == Icon::Cross ? Accent() : 0xffffffff);
			canvas.Text(text, x + 16 + icons + 10, y + 22 - text.height * 0.5f, 0xffffffff);
			x -= 18;
		}
	}

	void Shell::PhantomStateDraw(Canvas& canvas, float y)
	{
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		LibraryState(kicker, title, text, buttons);
		m_stateButtons.clear();
		canvas.PushAlpha(Enter(1));
		PhantomTag(canvas, kLeft, y, kicker, Accent(), 0xff000000, 22);
		const ui::TextStyle hero = Style({72, ui::Weight::Bold, 1.0f, 1.0f, true});
		float ly = y + 60;
		int i = 0;
		for (const std::string& line : Wrap(m_fonts, hero, title, 1300, 2))
		{
			const ui::TextBlock block = m_fonts.Layout(hero, line, 1400, 1);
			const Box band{kLeft + i * 30.0f - 16, ly, block.width + 40, block.height + 14};
			PhantomPlate(canvas, band, 22, i % 2 ? 0xffffffff : 0xff000000);
			canvas.Text(block, band.x + 24, ly + 7, i % 2 ? 0xff000000 : 0xffffffff);
			ly += band.h + 8;
			i++;
		}
		const ui::TextBlock body = canvas.Text(Style(kBodyStyle), kLeft, ly + 20, text, 0xffffffff, 1000, 5);
		m_stateButton = std::clamp(m_stateButton, 0, std::max(0, (int)buttons.size() - 1));
		float bx = kLeft;
		for (int b = 0; b < (int)buttons.size(); b++)
		{
			const bool focused = !m_railFocus && m_stateButton == b;
			const float w = PhantomTag(canvas, bx, ly + 50 + body.height, buttons[b], focused ? Accent() : b == 0 ? 0xffffffff : 0xff000000,
				focused || b == 0 ? 0xff000000 : 0xffffffff, 28);
			m_stateButtons.push_back({bx, ly + 50 + body.height, w, 48});
			bx += w + 20;
		}
		canvas.PopAlpha();
	}

	void Shell::PhantomHomeDraw(Canvas& canvas)
	{
		const bool still = m_settings.ui.reduceMotion;
		const float t = still ? 0.0f : (float)(m_now - m_start);
		const uint32_t accent = Accent();
		// the top: the app, its version, the side; the clock at the right
		EdenArrive(canvas, 0, -16);
		float tx = kLeft;
		tx += PhantomTag(canvas, tx, kTop + 8, "PS5CEMU-HAR", 0xffffffff, 0xff000000, 22) + 10;
		tx += PhantomTag(canvas, tx, kTop + 8, ps5update::Readable(PS5CEMU_VERSION), 0xff000000, 0xffffffff, 22) + 10;
		PhantomTag(canvas, tx, kTop + 8, SideTitle(m_side), accent, 0xff000000, 22);
		{
			const std::string clock = Clock();
			const float w = m_fonts.Width(Style({30, ui::Weight::Bold, 1.0f, 1.5f, true}), clock) + 2 * 21 + 15;
			PhantomTag(canvas, kRight - w, kTop + 2, clock, 0xff000000, 0xffffffff, 30);
		}
		EdenLeave(canvas);

		std::string kicker, title, text;
		std::vector<std::string> buttons;
		if (LibraryState(kicker, title, text, buttons))
		{
			WantBackdrop(nullptr);
			PhantomStateDraw(canvas, 200);
			return;
		}
		const int game = EdenContinueGame();
		const std::vector<int> recents = EdenRecentGames(game);
		const int count = (int)recents.size();
		if (m_homeFocus >= kFirstRecent && m_homeFocus < kViewAll && m_homeFocus - kFirstRecent >= count)
			m_homeFocus = count ? kFirstRecent + count - 1 : kViewAll;
		const bool onRecent = m_homeFocus >= kFirstRecent && m_homeFocus < kFirstRecent + count;
		Game* g = game >= 0 ? &m_games[game] : nullptr;
		WantBackdrop(onRecent ? &m_games[recents[m_homeFocus - kFirstRecent]] : g);
		const bool here = !m_railFocus;

		// the cover on its burst, at the right, under the words
		{
			const float e = Enter(2);
			canvas.PushAlpha(e);
			canvas.PushOffset(still ? 0.0f : (1 - e) * 80, 0);
			const float cx = 1540, cy = 440;
			PhantomBurst(canvas, cx, cy, 318 + (still ? 0 : 8 * std::sin(t * 2.0f)), accent, t * 0.15f);
			PhantomBurst(canvas, cx, cy, 226, 0xff000000, -t * 0.1f);
			if (g)
			{
				Box art = CoverBox(*g, 0, 140 + 500, 500, 420);
				art.x = cx - art.w * 0.5f;
				canvas.Rect(art.Offset(18, 18), 0, 0xff000000);
				canvas.Rect(art.Inset(-10), 0, 0xffffffff);
				DrawCover(canvas, *g, art, false, 0);
				if (m_homeFocus <= kGameSettings)
					m_edenLaunchFrom = canvas.Placed(art);
			}
			else
				SideTile(canvas, m_side, {cx - 150, cy - 150, 300, 300}, 10, 0xffffffff);
			canvas.PopOffset();
			canvas.PopAlpha();
		}

		// the game to continue: its name on bands, black and white in turn
		EdenArrive(canvas, 1, 26);
		PhantomTag(canvas, kLeft, 150, g ? Tr("Continue playing") : Tr("Welcome"), accent, 0xff000000, 22);
		const std::string heroText = g ? g->entry.game.name : std::string(Tr("Your next adventure"));
		ui::TextStyle hero = Style({78, ui::Weight::Bold, 1.0f, 1.0f, true});
		std::vector<std::string> lines = Wrap(m_fonts, hero, heroText, 930, 3);
		while (lines.size() > 2 && hero.size > 48)
		{
			hero.size -= 6;
			lines = Wrap(m_fonts, hero, heroText, 930, 3);
		}
		float ly = 210;
		for (int i = 0; i < (int)lines.size() && i < 3; i++)
		{
			const ui::TextBlock block = m_fonts.Layout(hero, lines[i], 1080, 1);
			const Box band{kLeft + i * 34.0f - 16, ly, block.width + 44, block.height + 12};
			PhantomPlate(canvas, band, 24, i % 2 ? 0xffffffff : 0xff000000);
			canvas.Text(block, band.x + 26, ly + 6, i % 2 ? 0xff000000 : 0xffffffff);
			ly += band.h + 6;
		}
		EdenLeave(canvas);

		// what is known of it, as tags
		EdenArrive(canvas, 2, 22);
		float cx = kLeft;
		const float chipsTop = ly + 22;
		if (g)
		{
			const ps5emu::Game& entry = g->entry.game;
			if (g->report)
				cx += PhantomTag(canvas, cx, chipsTop, TrC("status", g->report->status), 0xffffffff, 0xff000000, 18) + 8;
			if (IsWiiU())
				cx += PhantomTag(canvas, cx, chipsTop, entry.hasUpdate ? TrF("Update v{0}", entry.version) : fmt::format("v{}", entry.version), 0xff000000,
						  0xffffffff, 18) +
					8;
			if (!Played(*g).empty())
				cx += PhantomTag(canvas, cx, chipsTop, Played(*g), 0xff000000, 0xffffffff, 18) + 8;
			const std::string when = LastPlayedWords(g->entry.lastPlayed);
			if (!when.empty())
				PhantomTag(canvas, cx, chipsTop, TrF("Last played {0}", when), 0xff000000, 0xffffffff, 18);
		}
		else
			PhantomTag(canvas, cx, chipsTop, Tr("Choose a game from your library."), 0xff000000, 0xffffffff, 18);
		EdenLeave(canvas);

		// its actions: slanted plates, the focused one thrown forward on a white one
		EdenArrive(canvas, 3, 22);
		const float by = chipsTop + 74;
		auto action = [&](float x, Icon icon, const std::string& label, bool focused, bool primary) {
			const ui::TextStyle style = Style({34, ui::Weight::Bold, 1.0f, 1.5f, true});
			const ui::TextBlock text = m_fonts.Layout(style, label, 520, 1);
			const Box box{x, by, 30 + 34 + 14 + text.width + 34, 80};
			const Box drawn = focused && !still ? box.Offset(-6, -6) : box;
			if (focused)
				PhantomPlate(canvas, box.Offset(10, 10), 20, primary ? 0xffffffff : accent);
			PhantomPlate(canvas, drawn, 20, focused ? (primary ? accent : 0xffffffff) : primary ? ui::SetAlpha(accent, 0.75f) : 0xe6000000);
			const uint32_t ink = focused || primary ? 0xff000000 : 0xffffffff;
			canvas.Draw(icon, {drawn.x + 30, drawn.CentreY() - 17, 34, 34}, ink);
			canvas.Text(text, drawn.x + 30 + 34 + 14, drawn.CentreY() - text.height * 0.5f, ink);
			return box.w + 20;
		};
		const float playW = action(kLeft, Icon::Cross, g ? Tr("Play") : Tr("Open library"), here && m_homeFocus == kPlay, true);
		if (g)
		{
			action(kLeft + playW + 12, Icon::Triangle, Tr("Game settings"), here && m_homeFocus == kGameSettings, false);
			canvas.Text(Style({21, ui::Weight::SemiBold, 1.0f, 1.0f, true}), kLeft, by + 106, EdenRunsWith(*g), 0xccffffff, 1000, 1);
		}
		EdenLeave(canvas);

		// the games played before it, at the foot
		EdenArrive(canvas, 5, 26);
		PhantomTag(canvas, kLeft, 790, Tr("Recently played"), 0xffffffff, 0xff000000, 20);
		{
			const bool f = here && m_homeFocus == kViewAll;
			const std::string label = Tr("View all games");
			const float w = m_fonts.Width(Style({20, ui::Weight::Bold, 1.0f, 1.5f, true}), label) + 2 * 14 + 10;
			if (f)
				PhantomPlate(canvas, {kRight - 40 - w + 7, 790 + 7, w, 34}, 10, 0xffffffff);
			PhantomTag(canvas, kRight - 40 - w, 790, label, f ? accent : 0xff000000, f ? 0xff000000 : 0xffffffff, 20);
		}
		const float cardW = 360, gap = 34;
		if (recents.empty())
			canvas.Text(Style({24, ui::Weight::SemiBold, 1.0f, 1.0f, true}), kLeft, 860, Tr("The games you play appear here."), 0xb3ffffff, kWidth, 1);
		for (int i = 0; i < count; i++)
		{
			Game& recent = m_games[recents[i]];
			const bool f = here && m_homeFocus == kFirstRecent + i;
			const Box base{kLeft + i * (cardW + gap), 846, cardW, 128};
			const Box card = f && !still ? base.Offset(0, -10) : base;
			if (f)
				PhantomPlate(canvas, card.Offset(9, 9), 26, 0xffffffff);
			PhantomPlate(canvas, card, 26, f ? accent : 0xeb000000);
			Box cover = CoverBox(recent, card.x + 30, card.Bottom() - 14, card.h - 28, 96);
			canvas.Rect(cover.Inset(-4), 0, 0xffffffff);
			DrawCover(canvas, recent, cover, false, 0);
			canvas.Text(Style({22, ui::Weight::Bold, 1.15f, 0.5f, true}), card.x + 30 + 96 + 20, card.y + 20, recent.entry.game.name,
				f ? 0xff000000 : 0xffffffff, card.w - 30 - 96 - 20 - 10, 3);
			if (f)
				m_edenLaunchFrom = canvas.Placed(cover);
		}
		EdenLeave(canvas);
	}

	void Shell::PhantomLibraryDraw(Canvas& canvas)
	{
		const bool still = m_settings.ui.reduceMotion;
		const float t = still ? 0.0f : (float)(m_now - m_start);
		const uint32_t accent = Accent();
		const int side = SideIndex();
		EdenArrive(canvas, 0, 0);
		const ui::TextBlock heading = canvas.Text(Style({68, ui::Weight::Bold, 1.0f, 2.0f, true}), kLeft, 26, TrC("tab", "Library"), 0xffffffff, 700, 1);
		float headingEnd = kLeft + heading.width;
		if (!m_games.empty())
			headingEnd += 24 + PhantomTag(canvas, kLeft + heading.width + 24, 48, TrP((int)m_games.size(), "{0} game", "{0} games"), accent, 0xff000000, 20);
		{
			// tr: the Library's filters, as library.cpp names them
			const char* filters[4] = {TrC("filter", "All"), Tr("Recently added"), Tr("Favourites"), Tr("Graphic packs on")};
			const int filter = std::clamp(m_settings.ui.libraryFilter[side], 0, LibraryFilters() - 1);
			const std::string tools[3] = {TrF("Sort: {0}", Tr(kSortNames[std::clamp(m_settings.ui.librarySort[side], 0, 3)])), TrF("Filter: {0}", filters[filter]),
				m_search.empty() ? std::string(Tr("Search")) : TrF("Search: {0}", m_search)};
			const Icon icons[3] = {Icon::Square, Icon::L2, Icon::R2};
			float widths[3], total = 0;
			for (int i = 0; i < 3; i++)
			{
				widths[i] = m_fonts.Width(Style({18, ui::Weight::Bold, 1.0f, 1.5f, true}), tools[i]) + 2 * 13 + 9;
				total += widths[i] + 36 + 18;
			}
			// beside the heading when there is room for them, else under it at the right (a longer language)
			const float ty = kRight - total < headingEnd + 40 ? 108.0f : 52.0f;
			float x = kRight;
			for (int i = 2; i >= 0; i--)
			{
				x -= widths[i];
				PhantomTag(canvas, x, ty, tools[i], i == 1 && filter ? accent : 0xff000000, i == 1 && filter ? 0xff000000 : 0xffffffff, 18);
				x -= 36;
				canvas.Draw(icons[i], {x, ty + 2, 28, 28}, 0xffffffff);
				x -= 18;
			}
		}
		EdenLeave(canvas);
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				WantBackdrop(nullptr);
				PhantomStateDraw(canvas, 220);
				return;
			}
		}
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		if (total == 0)
		{
			WantBackdrop(nullptr);
			EdenArrive(canvas, 1, 24);
			PhantomTag(canvas, kLeft, 220, !m_search.empty() ? TrF("Nothing matches “{0}”", m_search) : std::string(Tr("No games yet")), accent,
				0xff000000, 36);
			EdenLeave(canvas);
			return;
		}
		m_libraryIndex = std::clamp(m_libraryIndex, 0, total - 1);
		const int game = LibraryGame();
		WantBackdrop(game >= 0 ? &m_games[game] : nullptr);

		// the focused game's cover on its burst, at the right
		{
			const float e = Enter(2);
			canvas.PushAlpha(e);
			canvas.PushOffset(still ? 0.0f : (1 - e) * 80, 0);
			const float cx = 1470, cy = 450;
			PhantomBurst(canvas, cx, cy, 320 + (still ? 0 : 8 * std::sin(t * 2.0f)), accent, t * 0.15f);
			PhantomBurst(canvas, cx, cy, 230, 0xff000000, -t * 0.1f);
			if (game >= 0)
			{
				Game& g = m_games[game];
				Box art = CoverBox(g, 0, 160 + 480, 480, 440);
				art.x = cx - art.w * 0.5f;
				canvas.Rect(art.Offset(18, 18), 0, 0xff000000);
				canvas.Rect(art.Inset(-10), 0, 0xffffffff);
				DrawCover(canvas, g, art, false, 0);
				m_edenLaunchFrom = canvas.Placed(art);
				// its facts on a black plate under it, in capitals
				const Box card{1060, 712, 700, 220};
				PhantomPlate(canvas, card.Offset(10, 10), 30, accent);
				PhantomPlate(canvas, card, 30, 0xf2000000);
				const ui::TextStyle fact = Style({22, ui::Weight::Bold, 1.0f, 1.0f, true});
				float fy = card.y + 22;
				const int own = OwnSettings(g);
				const std::string facts[4] = {Byline(g), TrC("status", g.report ? g.report->status : std::string()),
					Played(g).empty() ? std::string(Tr("Not played yet")) : Played(g),
					own ? TrP(own, "{0} setting of its own", "{0} settings of its own") : std::string(Tr("Its side's settings"))};
				for (int i = 0; i < 4; i++)
				{
					if (facts[i].empty())
						continue;
					canvas.Text(fact, card.x + 54 + (3 - i) * 7.0f, fy, facts[i], i == 1 ? accent : 0xffffffff, card.w - 90, 1);
					fy += 46;
				}
			}
			canvas.PopOffset();
			canvas.PopAlpha();
		}

		// the list of names, the focused one thrown forward
		constexpr float kTop0 = 150, kPitch = 72, kBottom = 960;
		const float view = kBottom - kTop0;
		float target = m_edenScroll.target;
		if (m_libraryIndex * kPitch < target)
			target = m_libraryIndex * kPitch;
		if ((m_libraryIndex + 1) * kPitch - target > view)
			target = (m_libraryIndex + 1) * kPitch - view;
		m_edenScroll.target = std::max(0.0f, target);
		m_edenScroll.omega = still ? 60.0f : kScrollOmega;
		m_edenScroll.Update(m_dt);
		const float scroll = m_edenScroll.value;
		canvas.PushClip({0, kTop0 - 20, 1100, view + 40});
		for (int index = std::max(0, (int)(scroll / kPitch) - 1); index < total; index++)
		{
			const float y = kTop0 + index * kPitch - scroll;
			if (y > kBottom + 10)
				break;
			const bool focused = !m_railFocus && index == m_libraryIndex;
			const std::string name = index < offset ? std::string(Tr("Play from your 3DS")) : m_games[m_shelf[index - offset]].entry.game.name;
			const std::string format = index < offset ? std::string("ARTIC") : m_games[m_shelf[index - offset]].entry.game.format;
			EdenArrive(canvas, 1 + std::min(std::max(0, index - (int)(scroll / kPitch)), 8), 18);
			const float stagger = (index % 3) * 16.0f;
			if (focused)
			{
				const Box row{kLeft + 26 + stagger, y - 6, 860, 70};
				PhantomPlate(canvas, row.Offset(9, 9), 22, 0xffffffff);
				PhantomPlate(canvas, row, 22, accent);
				canvas.Text(Style({32, ui::Weight::Bold, 1.0f, 1.0f, true}), row.x + 30, row.y + 15, name, 0xff000000, row.w - 160, 1);
				canvas.Text(Style({18, ui::Weight::Bold, 1.0f, 1.5f, true}), row.Right() - 110, row.y + 25, format, 0xff000000, 100, 1);
			}
			else
			{
				const Box row{kLeft + stagger, y, 800, 58};
				PhantomPlate(canvas, row, 18, 0xd9000000);
				canvas.Text(Style({26, ui::Weight::Bold, 1.0f, 1.0f, true}), row.x + 26, row.y + 14, name, 0xffffffff, row.w - 150, 1);
				canvas.Text(Style({16, ui::Weight::Bold, 1.0f, 1.5f, true}), row.Right() - 100, row.y + 21, format, ui::SetAlpha(accent, 0.9f), 90, 1);
			}
			EdenLeave(canvas);
		}
		canvas.PopClip();
		// where the focus is in the list: on the focused game's facts' plate, clear of the hints
		PhantomTag(canvas, 1660, 690, fmt::format("{} / {}", m_libraryIndex + 1, total), 0xffffffff, 0xff000000, 20);
	}

	void Shell::PhantomSettingsDraw(Canvas& canvas)
	{
		const bool game = InGameSettings();
		const auto pages = game ? SettingsPages() : EdenBoardPages();
		const uint32_t accent = Accent();
		if (pages.empty())
			return;
		int page = 0;
		for (int i = 0; i < (int)pages.size(); i++)
			if (pages[i].id == m_page)
				page = i;
		m_page = pages[page].id;
		EdenArrive(canvas, 0, 0);
		if (game)
		{
			PhantomTag(canvas, kLeft, 30, Tr("Game settings"), accent, 0xff000000, 22);
			canvas.Text(Style({52, ui::Weight::Bold, 1.0f, 1.0f, true}), kLeft, 78, m_gameName, 0xffffffff, 1500, 1);
		}
		else
		{
			const ui::TextBlock heading = canvas.Text(Style({68, ui::Weight::Bold, 1.0f, 2.0f, true}), kLeft, 26, TrC("tab", "Settings"), 0xffffffff, 900, 1);
			PhantomTag(canvas, kLeft + heading.width + 24, 48, TrF("{0} settings", SystemName(m_side)), accent, 0xff000000, 20);
		}
		EdenLeave(canvas);

		// the pages, as plates
		EdenArrive(canvas, 1, 0);
		const float top = 170, pitch = game ? 74.0f : 60.0f;
		for (int i = 0; i < (int)pages.size(); i++)
		{
			const float y = top + i * pitch;
			if (y > 980)
				break;
			const bool on = i == page;
			const float stagger = (i % 3) * 10.0f;
			Box plate{kLeft + stagger, y, 420, pitch - 10};
			if (on)
			{
				plate = plate.Offset(18, 0);
				if (!m_onRows)
					PhantomPlate(canvas, plate.Offset(8, 8), 16, 0xffffffff);
			}
			PhantomPlate(canvas, plate, 16, on ? accent : 0xd9000000);
			canvas.Draw(pages[i].icon, {plate.x + 22, plate.CentreY() - 13, 26, 26}, on ? 0xff000000 : accent);
			canvas.Text(Style({22, ui::Weight::Bold, 1.0f, 1.0f, true}), plate.x + 62, plate.CentreY() - 12, pages[i].title, on ? 0xff000000 : 0xffffffff,
				plate.w - 80, 1);
		}
		EdenLeave(canvas);

		// its rows
		EdenArrive(canvas, 2, 16);
		const float px = 700, pw = kRight - px;
		PhantomTag(canvas, px, 160, pages[page].title, 0xffffffff, 0xff000000, 22);
		canvas.Text(Style({20, ui::Weight::SemiBold, 1.0f}), px, 212, pages[page].subtitle, 0xccffffff, pw, 1);
		auto rows = SettingRows(m_page);
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		const float ry = 256, rowH = 96, available = 980 - ry;
		const float scroll = std::max(0.0f, (m_settingRow + 1) * (rowH + 12) - available);
		canvas.PushClip({px - 24, ry - 14, pw + 48, available + 28});
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const Box box{px, ry + i * (rowH + 12) - scroll, pw, rowH};
			if (box.Bottom() < ry - 20 || box.y > 1000)
				continue;
			const bool focused = m_onRows && i == m_settingRow;
			DrawRow(canvas, rows[i], box, focused);
			if (focused && rows[i].kind == Row::Kind::Hold)
				HoldRing(canvas, box, m_holdProgress);
		}
		const float under = ry + rows.size() * (rowH + 12) - scroll;
		if (m_page == "remote" && m_settings.remoteScreen && !game)
			DrawRemoteScreen(canvas, {px, under + 6, pw, 372});
		else if (!game)
		{
			const std::string panel = PanelText(m_page);
			if (!panel.empty())
				canvas.Text(Style({20, ui::Weight::Regular, 1.45f}), px, under + 8, panel, 0xb3ffffff, pw, std::max(1, (int)((980 - under) / 29)));
		}
		canvas.PopClip();
		EdenLeave(canvas);
	}

	void Shell::PhantomAboutDraw(Canvas& canvas)
	{
		WantBackdrop(nullptr);
		const uint32_t accent = Accent();
		EdenArrive(canvas, 0, 0);
		canvas.Text(Style({68, ui::Weight::Bold, 1.0f, 2.0f, true}), kLeft, 26, TrC("app", "About"), 0xffffffff, 900, 1);
		EdenLeave(canvas);
		EdenArrive(canvas, 1, 24);
		const ui::TextStyle hero = Style({96, ui::Weight::Bold, 1.0f, 2.0f, true});
		const ui::TextBlock name = m_fonts.Layout(hero, "PS5CEMU-HAR", 1200, 1);
		PhantomPlate(canvas, {kLeft - 16 + 9, 150 + 9, name.width + 50, name.height + 16}, 30, accent);
		PhantomPlate(canvas, {kLeft - 16, 150, name.width + 50, name.height + 16}, 30, 0xffffffff);
		canvas.Text(name, kLeft + 16, 158, 0xff000000);
		PhantomTag(canvas, kLeft + 40, 150 + name.height + 34, std::string(Tr("Version")) + " " + ps5update::Readable(PS5CEMU_VERSION), 0xff000000,
			0xffffffff, 22);
		EdenLeave(canvas);
		EdenArrive(canvas, 2, 24);
		const Box panel{kLeft, 380, kWidth - 480, 600};
		PhantomPlate(canvas, panel, 30, 0xeb000000);
		canvas.Text(Style({21, ui::Weight::Regular, 1.5f}), panel.x + 60, panel.y + 30, PanelText("about"), 0xe6ffffff, panel.w - 110, 17);
		EdenLeave(canvas);
		EdenArrive(canvas, 3, 24);
		DrawSupport(canvas, {kRight - 420, 380, 420, 560});
		EdenLeave(canvas);
	}

	void Shell::PhantomDrawLaunch(Canvas& canvas)
	{
		const float total = kLaunchSeconds + 0.25f;
		const float p = std::clamp((float)((m_now - m_launch.at) / total), 0.0f, 1.0f);
		const bool still = m_settings.ui.reduceMotion;
		const uint32_t accent = Accent();
		canvas.Rect({0, 0, 1920, 1080}, 0, ui::SetAlpha(0xff000000, 0.86f * ui::ease::CubicOut(p / 0.25f)));
		// a burst opening behind the middle, the cover flying onto it
		const float grow = still ? 1.0f : ui::ease::BackOut(p / 0.55f);
		PhantomBurst(canvas, 960, 400, 420 * grow, accent, (float)(m_now - m_launch.at) * 0.6f);
		PhantomBurst(canvas, 960, 400, 290 * grow, 0xff000000, -(float)(m_now - m_launch.at) * 0.4f);
		const float height = 400, width = height * std::clamp(m_launch.aspect, 0.5f, 1.4f);
		const Box rest{960 - width * 0.5f, 200, width, height};
		const Box at = Lerp(m_launch.from, rest, still ? 1.0f : ui::ease::CubicOut(p / 0.5f));
		canvas.Rect(at.Inset(-10), 0, 0xffffffff);
		if (m_launch.game >= 0 && m_launch.game < (int)m_games.size())
			DrawCover(canvas, m_games[m_launch.game], at, false, 0);
		// its name on a black band sliding in across the screen, Starting over it
		const float band = still ? 1.0f : ui::ease::CubicOut((p - 0.2f) / 0.4f);
		const ui::TextStyle style = Style({64, ui::Weight::Bold, 1.0f, 2.0f, true});
		const ui::TextBlock name = m_fonts.Layout(style, m_launch.title, 1500, 1);
		const float bx = -2000 + band * 2000;
		PhantomPlate(canvas, {bx - 200, 680 + 10, 2400, name.height + 40}, 60, accent);
		PhantomPlate(canvas, {bx - 200, 680, 2400, name.height + 40}, 60, 0xff000000);
		canvas.Text(name, bx + 960 - name.width * 0.5f, 700, 0xffffffff);
		canvas.PushAlpha(band);
		PhantomTag(canvas, 960 - 120, 630, Tr("Starting"), 0xffffffff, 0xff000000, 22);
		canvas.Text(Style({24, ui::Weight::Bold, 1.0f, 1.5f, true}), 960 - 600, 700 + name.height + 50, m_launch.caption, 0xffffffff, 1200, 1,
			ui::Align::Centre);
		canvas.PopAlpha();
	}

	void Shell::PhantomDrawOpening(Canvas& canvas)
	{
		// bands slashing across a black screen, the launcher behind them as they pass
		const bool still = m_settings.ui.reduceMotion;
		if (m_openingAt < 0)
		{
			m_openingAt = m_now;
			m_screenAt = m_now + (still ? 0.1 : 0.4);
		}
		const float t = (float)(m_now - m_openingAt), length = still ? 0.3f : 0.95f;
		if (t >= length)
		{
			m_openingOn = false;
			return;
		}
		const float dark = still ? 1 - t / length : 1 - ui::ease::CubicOut((t - 0.3f) / 0.3f);
		canvas.Rect({0, 0, 1920, 1080}, 0, ui::SetAlpha(0xff000000, std::clamp(dark, 0.0f, 1.0f)));
		if (still)
			return;
		const uint32_t colours[3] = {Accent(), 0xffffffff, 0xff000000};
		for (int k = 0; k < 3; k++)
		{
			const float x = -2600 + ui::ease::InOut((t - k * 0.07f) / 0.7f) * 5000;
			PhantomPlate(canvas, {x, 0, 900 - k * 200.0f, 1080}, 700, colours[k]);
		}
	}

	// == Terminal ==========================================================================================

	void Shell::TermPanel(Canvas& canvas, const Box& box, const std::string& title)
	{
		canvas.Rect(box, 0, m_settings.ui.highContrast ? kInk1 : ui::SetAlpha(kInk1, 0.86f));
		canvas.Ring(box, 0, 1.5f, ui::SetAlpha(Accent(), 0.5f));
		if (title.empty())
			return;
		// its title cut into its top edge: [ title ]
		const ui::TextStyle style = Style({18, ui::Weight::SemiBold, 1.0f, 1.0f, false, true});
		const ui::TextBlock text = m_fonts.Layout(style, "[ " + title + " ]", box.w - 60, 1);
		canvas.Rect({box.x + 22, box.y - text.height * 0.5f, text.width + 16, text.height}, 0, kInk0);
		canvas.Text(text, box.x + 30, box.y - text.height * 0.5f, Accent());
	}

	Box Shell::TermButton(Canvas& canvas, float x, float y, Icon icon, const std::string& label, bool focused, bool primary)
	{
		const ui::TextStyle style = Style({24, ui::Weight::SemiBold, 1.0f, 0.5f, false, true});
		const ui::TextBlock text = m_fonts.Layout(style, label, 500, 1);
		const Box box{x, y, 20 + 30 + 14 + text.width + 24, 56};
		const uint32_t accent = Accent();
		if (focused)
			canvas.Rect(box, 0, accent);
		else
			canvas.Ring(box, 0, 1.5f, primary ? accent : ui::SetAlpha(accent, 0.5f));
		const uint32_t ink = focused ? kInk0 : primary ? accent : kText;
		canvas.Ring({box.x + 16, box.CentreY() - 17, 34, 34}, 0, 1.5f, ink);
		canvas.Draw(icon, {box.x + 20, box.CentreY() - 13, 26, 26}, ink);
		canvas.Text(text, box.x + 20 + 30 + 14, box.CentreY() - text.height * 0.5f, ink);
		return box;
	}

	void Shell::TermStatus(Canvas& canvas)
	{
		// one line along the top: the app, the side as a node and its emulator, the games, the clock
		const ui::TextStyle style = Style({20, ui::Weight::Medium, 1.0f, 0.5f, false, true});
		const std::string node = Lower(SideTitle(m_side));
		const std::string left = fmt::format("PS5CEMU-HAR {}   |   node {}@{}   |   {}", ps5update::Readable(PS5CEMU_VERSION), node, Lower(EmulatorName(m_side)),
			TrP((int)m_games.size(), "{0} game", "{0} games"));
		canvas.Text(style, kLeft, 18, left, Accent(), 1300, 1);
		std::string pads;
		for (int player = 0; player < ps5pad::kMaxPlayers; player++)
			if (ps5pad::IsConnected(player))
				pads += fmt::format("P{} ", player + 1);
		const ui::TextBlock right = m_fonts.Layout(style, pads + "|   " + Clock(), 400, 1);
		canvas.Text(right, kRight - right.width, 18, kText);
		canvas.Rect({kRail + 20, 52, 1920 - kRail - 40, 1.5f}, 0, ui::SetAlpha(Accent(), 0.4f));
	}

	void Shell::TermBackdrop(Canvas& canvas)
	{
		// the screen nearly black: the game's picture only a glow behind the panels
		canvas.Rect({0, 0, 1920, 1080}, 0, ui::SetAlpha(kInk0, 0.8f));
	}

	void Shell::TermDrawCrt(Canvas& canvas)
	{
		// scanlines, a brighter band rolling down, the tube's dark corners, and (unless Reduce motion) a
		// flicker too slight to notice but as a tube
		for (float y = 0; y < 1080; y += 4)
			canvas.Rect({0, y, 1920, 1.6f}, 0, 0x2e000000);
		const bool still = m_settings.ui.reduceMotion;
		const float t = (float)(m_now - m_start);
		if (!still)
		{
			const float band = std::fmod(t * 140.0f, 1500.0f) - 200;
			canvas.LinearGradient({0, band, 1920, 180}, 0, ui::SetAlpha(Accent(), 0), ui::SetAlpha(Accent(), 0.035f), 0, band, 0, band + 180);
			canvas.Rect({0, 0, 1920, 1080}, 0, ui::SetAlpha(0xff000000, 0.025f * (0.5f + 0.5f * std::sin(t * 53.0f))));
		}
		canvas.RadialGradient({0, 0, 1920, 1080}, 0, 0x00000000, 0x80000000, 960, 540, 1250, 860, 0.62f);
	}

	void Shell::TermDrawRail(Canvas& canvas)
	{
		const bool still = m_settings.ui.reduceMotion;
		m_railOpen.target = m_railFocus ? 1.0f : 0.0f;
		m_railOpen.omega = still ? 60.0f : 22.0f;
		m_railOpen.Update(m_dt);
		const float open = std::clamp(m_railOpen.value, 0.0f, 1.0f);
		const float width = kRail + (kRailWide - kRail) * open;
		const int section = EdenSectionIndex();
		const uint32_t accent = Accent();
		if (open > 0.01f)
			canvas.Rect({width, 0, 1920 - width, 1080}, 0, ui::SetAlpha(kInk0, 0.6f * open));
		canvas.Rect({0, 0, width, 1080}, 0, ui::SetAlpha(kInk0, 0.97f + 0.03f * open));
		canvas.Rect({width - 1.5f, 0, 1.5f, 1080}, 0, ui::SetAlpha(accent, 0.5f));
		const ui::TextStyle mono = Style({22, ui::Weight::SemiBold, 1.0f, 0.5f, false, true});
		canvas.Text(Style({20, ui::Weight::Bold, 1.0f, 0.5f, false, true}), 24, 40, open > 0.5f ? "~/ PS5CEMU-HAR" : "~/", accent, width - 40, 1);
		// the tree's trunk
		const float trunk = (kRail - kRailTile) * 0.5f - 10;
		canvas.Rect({trunk, RailTileTop(0) + kRailTile * 0.5f, 1.5f, kRailPitch * (kSections - 1)}, 0, ui::SetAlpha(accent, 0.4f));
		for (int i = 0; i < kSections; i++)
		{
			const bool lit = i == section;
			const bool focused = m_railFocus && m_railIndex == i;
			const Box tile{(kRail - kRailTile) * 0.5f, RailTileTop(i), kRailTile + (width - kRail), kRailTile};
			canvas.Rect({trunk, tile.CentreY(), 10, 1.5f}, 0, ui::SetAlpha(accent, 0.4f));
			if (focused)
				canvas.Rect(tile, 0, accent);
			else if (lit)
			{
				canvas.Rect(tile, 0, ui::SetAlpha(accent, 0.14f));
				canvas.Ring(tile, 0, 1.5f, accent);
			}
			else
				canvas.Ring({tile.x, tile.y, kRailTile, kRailTile}, 0, 1.5f, ui::SetAlpha(accent, 0.3f));
			canvas.Draw(SectionIcon(i), {tile.x + 20, tile.y + 20, 32, 32}, focused ? kInk0 : lit ? accent : ui::SetAlpha(kText, 0.7f));
			if (open > 0.01f)
			{
				canvas.PushAlpha(open);
				canvas.Text(mono, tile.x + kRailTile + 4, tile.CentreY() - 12, kPaths[i], focused ? kInk0 : lit ? accent : kText, kRailWide - 140, 1);
				canvas.PopAlpha();
			}
		}
		const std::vector<System> sides = RailSides();
		if (open > 0.01f)
		{
			canvas.PushAlpha(open);
			canvas.Text(Style({18, ui::Weight::SemiBold, 1.0f, 2.0f, true, true}), 24, RailSideBox(0, (int)sides.size()).y - 40, "nodes", ui::SetAlpha(kText, 0.6f),
				300, 1);
			canvas.PopAlpha();
		}
		for (int k = 0; k < (int)sides.size(); k++)
		{
			const Box box = RailSideBox(k, (int)sides.size());
			const bool on = sides[k] == m_side;
			const bool focused = m_railFocus && m_railIndex == kSections + k;
			if (focused)
				canvas.Rect(box.Inset(-6), 0, AccentOf(sides[k]));
			else if (on)
				canvas.Ring(box.Inset(-6), 0, 2, AccentOf(sides[k]));
			SideTile(canvas, sides[k], box, 0, on || focused ? 0xffffffff : 0x73ffffff);
			if (open > 0.01f)
			{
				canvas.PushAlpha(open);
				canvas.Text(mono, box.Right() + 28, box.CentreY() - 12, fmt::format("{}@{}", Lower(SideTitle(sides[k])), Lower(EmulatorName(sides[k]))),
					on ? AccentOf(sides[k]) : kText, kRailWide - box.Right() - 40, 1);
				canvas.PopAlpha();
			}
		}
	}

	void Shell::TermDrawHints(Canvas& canvas, const std::vector<Hint>& hints)
	{
		// the log's last line at the left, a cursor blinking after it; the keys at the right, smaller when a
		// language's words would run into the log
		ui::TextStyle style = Style({20, ui::Weight::Medium, 1.0f, 0.5f, false, true});
		const float y = kFooter - 14;
		const float open = (kRailWide - kRail) * std::clamp(m_railOpen.value, 0.0f, 1.0f);
		canvas.Rect({kRail + 20 + open, y - 18, 1920 - kRail - 40 - open, 1.5f}, 0, ui::SetAlpha(Accent(), 0.4f));
		auto width = [&] {
			float total = 0;
			for (const Hint& hint : hints)
				if (!hint.text.empty())
					total += m_fonts.Width(style, Lower(hint.text)) + 8 + 28 + (hint.icon == Icon::R1 ? 32 : 0) + 30;
			return total;
		};
		while (width() > kWidth - 380 && style.size > 15)
			style.size -= 1;
		const std::string last = m_termLog.empty() ? std::string("ready") : m_termLog.back();
		const bool cursor = m_settings.ui.reduceMotion || std::fmod(m_now, 1.0) < 0.55;
		const float shift = (kRailWide - kRail) * std::clamp(m_railOpen.value, 0.0f, 1.0f); // beside the rail, as wide as it is open
		canvas.Text(style, kLeft + shift, y, "> " + last + (cursor ? " _" : ""), Accent(), std::max(120.0f, kWidth - shift - width() - 40), 1);
		float x = kRight;
		for (auto it = hints.rbegin(); it != hints.rend(); ++it)
		{
			if (it->text.empty())
				continue;
			const ui::TextBlock text = m_fonts.Layout(style, Lower(it->text), 400, 1);
			x -= text.width;
			canvas.Text(text, x, y, kText);
			x -= 8 + 28;
			canvas.Ring({x, y - 3, 28, 28}, 0, 1.2f, ui::SetAlpha(Accent(), 0.7f));
			canvas.Draw(it->icon, {x + 3, y, 22, 22}, Accent());
			if (it->icon == Icon::R1)
			{
				x -= 32;
				canvas.Ring({x, y - 3, 28, 28}, 0, 1.2f, ui::SetAlpha(Accent(), 0.7f));
				canvas.Draw(Icon::L1, {x + 3, y, 22, 22}, Accent());
			}
			x -= 30;
		}
	}

	void Shell::TermStateDraw(Canvas& canvas, const Box& panel)
	{
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		LibraryState(kicker, title, text, buttons);
		m_stateButtons.clear();
		canvas.PushAlpha(Enter(1));
		TermPanel(canvas, panel, Lower(kicker));
		const ui::TextStyle mono = Style({22, ui::Weight::Medium, 1.45f, 0.5f, false, true});
		canvas.Text(mono, panel.x + 40, panel.y + 40, "> status", ui::SetAlpha(kText, 0.6f), panel.w - 80, 1);
		const ui::TextBlock head = canvas.Text(Style({52, ui::Weight::Bold, 1.1f}), panel.x + 40, panel.y + 84, title, Accent(), panel.w - 80, 2);
		const ui::TextBlock body = canvas.Text(mono, panel.x + 40, panel.y + 104 + head.height, text, kText, panel.w - 80, 6);
		m_stateButton = std::clamp(m_stateButton, 0, std::max(0, (int)buttons.size() - 1));
		float bx = panel.x + 40;
		for (int b = 0; b < (int)buttons.size(); b++)
		{
			const Box box = TermButton(canvas, bx, panel.y + 140 + head.height + body.height, b == 0 ? Icon::Cross : Icon::ChevronRight, Lower(buttons[b]),
				!m_railFocus && m_stateButton == b, b == 0);
			m_stateButtons.push_back(box);
			bx += box.w + 20;
		}
		canvas.PopAlpha();
	}

	void Shell::TermHomeDraw(Canvas& canvas)
	{
		TermStatus(canvas);
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		if (LibraryState(kicker, title, text, buttons))
		{
			WantBackdrop(nullptr);
			TermStateDraw(canvas, {kLeft, 90, kWidth, 860});
			return;
		}
		const int game = EdenContinueGame();
		const std::vector<int> recents = EdenRecentGames(game);
		const int count = (int)recents.size();
		if (m_homeFocus >= kFirstRecent && m_homeFocus < kViewAll && m_homeFocus - kFirstRecent >= count)
			m_homeFocus = count ? kFirstRecent + count - 1 : kViewAll;
		const bool onRecent = m_homeFocus >= kFirstRecent && m_homeFocus < kFirstRecent + count;
		Game* g = game >= 0 ? &m_games[game] : nullptr;
		WantBackdrop(onRecent ? &m_games[recents[m_homeFocus - kFirstRecent]] : g);
		const bool here = !m_railFocus;
		const uint32_t accent = Accent();
		const ui::TextStyle mono = Style({22, ui::Weight::Medium, 1.0f, 0.5f, false, true});
		const ui::TextStyle dim = Style({20, ui::Weight::Medium, 1.0f, 0.5f, false, true});

		// the last session: the game, what is known of it, run and config
		EdenArrive(canvas, 1, 16);
		const Box session{kLeft, 90, 1010, 560};
		TermPanel(canvas, session, "session");
		canvas.Text(dim, session.x + 36, session.y + 34, g ? "> resume --last" : "> ls ~/library", ui::SetAlpha(kText, 0.6f), session.w - 72, 1);
		const ui::TextBlock name =
			canvas.Text(Style({50, ui::Weight::Bold, 1.1f}), session.x + 36, session.y + 70, g ? g->entry.game.name : std::string(Tr("Your next adventure")), accent,
				session.w - 72, 2);
		float y = session.y + 90 + name.height;
		if (g)
		{
			const ps5emu::Game& entry = g->entry.game;
			const int own = OwnSettings(*g);
			if (IsWiiU() && m_status.coreReady && g->packsOn < 0)
				g->packsOn = ps5emu::EnabledGraphicPackCount(entry.titleId);
			const std::vector<std::pair<std::string, std::string>> facts = {
				{"id", IsDs() ? entry.gameId : Hex(entry.titleId)},
				{"status", g->report ? std::string(TrC("status", g->report->status)) : std::string(Tr("Not on the list yet"))},
				{"version", IsWiiU() ? (entry.hasUpdate ? TrF("Update v{0}", entry.version) : fmt::format("v{}", entry.version)) : entry.format},
				{"played", Played(*g).empty() ? std::string(Tr("Not played yet")) : Played(*g)},
				{"last", LastPlayedWords(g->entry.lastPlayed).empty() ? std::string(Tr("Never")) : LastPlayedWords(g->entry.lastPlayed)},
				{"config", own ? TrP(own, "{0} setting of its own", "{0} settings of its own") : std::string(Tr("Its side's settings"))},
				{"packs", IsWiiU() && m_status.coreReady ? (g->packsOn > 0 ? TrP(g->packsOn, "{0} on", "{0} on") : std::string(Tr("None on"))) : std::string()}};
			for (const auto& [key, value] : facts)
			{
				if (value.empty())
					continue;
				canvas.Text(mono, session.x + 36, y, key, ui::SetAlpha(kText, 0.55f), 160, 1);
				canvas.Text(mono, session.x + 200, y, value, kText, session.w - 240, 1);
				y += 34;
			}
		}
		else
			canvas.Text(mono, session.x + 36, y, Tr("Choose a game from your library."), kText, session.w - 72, 2);
		const float by = session.Bottom() - 56 - 32;
		const Box run = TermButton(canvas, session.x + 36, by, Icon::Cross, Lower(g ? Tr("Play") : Tr("Open library")), here && m_homeFocus == kPlay, true);
		if (g)
			TermButton(canvas, run.Right() + 18, by, Icon::Triangle, Lower(Tr("Game settings")), here && m_homeFocus == kGameSettings, false);
		EdenLeave(canvas);

		// its picture, through the tube
		EdenArrive(canvas, 2, 16);
		const Box image{1230, 90, kRight - 1230, 560};
		TermPanel(canvas, image, "image");
		if (g)
		{
			Box art = CoverBox(*g, 0, image.Bottom() - 36, image.h - 72, image.w - 72);
			art.x = image.CentreX() - art.w * 0.5f;
			DrawCover(canvas, *g, art, false, 0);
			canvas.Rect(art, 0, ui::SetAlpha(accent, 0.12f));
			for (float ly = art.y; ly < art.Bottom(); ly += 3)
				canvas.Rect({art.x, ly, art.w, 1}, 0, 0x40000000);
			if (m_homeFocus <= kGameSettings)
				m_edenLaunchFrom = canvas.Placed(art);
		}
		else
			SideTile(canvas, m_side, {image.CentreX() - 120, image.CentreY() - 120, 240, 240}, 0, 0xffffffff);
		EdenLeave(canvas);

		// what was played before, and the way to the whole library
		EdenArrive(canvas, 3, 16);
		const Box history{kLeft, 690, kWidth, 280};
		TermPanel(canvas, history, "history");
		const float boxW = 316, gap = 18;
		if (recents.empty())
			canvas.Text(mono, history.x + 36, history.y + 44, Tr("The games you play appear here."), ui::SetAlpha(kText, 0.6f), history.w - 72, 1);
		for (int i = 0; i < count; i++)
		{
			Game& recent = m_games[recents[i]];
			const bool f = here && m_homeFocus == kFirstRecent + i;
			const Box box{history.x + 30 + i * (boxW + gap), history.y + 36, boxW, history.h - 66};
			if (f)
				canvas.Rect(box, 0, accent);
			else
				canvas.Ring(box, 0, 1.5f, ui::SetAlpha(accent, 0.4f));
			const uint32_t ink = f ? kInk0 : kText;
			canvas.Text(dim, box.x + 18, box.y + 16, fmt::format("[{}]", i + 1), f ? kInk0 : accent, 60, 1);
			Box cover = CoverBox(recent, box.Right() - 18 - 90, box.Bottom() - 18, box.h - 36, 90);
			DrawCover(canvas, recent, cover, false, 0);
			canvas.Text(Style({22, ui::Weight::SemiBold, 1.2f}), box.x + 18, box.y + 50, recent.entry.game.name, ink, box.w - 36 - 100, 3);
			const std::string when = LastPlayedWords(recent.entry.lastPlayed);
			if (!when.empty())
				canvas.Text(dim, box.x + 18, box.Bottom() - 38, "last: " + when, f ? kInk0 : ui::SetAlpha(kText, 0.6f), box.w - 36 - 100, 1);
			if (f)
				m_edenLaunchFrom = canvas.Placed(cover);
		}
		{
			const bool f = here && m_homeFocus == kViewAll;
			const Box box{history.Right() - 30 - 250, history.y + 36, 250, history.h - 66};
			if (f)
				canvas.Rect(box, 0, accent);
			else
				canvas.Ring(box, 0, 1.5f, ui::SetAlpha(accent, 0.6f));
			canvas.Text(mono, box.x + 20, box.y + 18, "> ls ~/library", f ? kInk0 : accent, box.w - 40, 1);
			canvas.Text(Style({22, ui::Weight::SemiBold, 1.2f}), box.x + 20, box.y + 60, Tr("View all games"), f ? kInk0 : kText, box.w - 40, 3);
		}
		EdenLeave(canvas);
	}

	void Shell::TermLibraryDraw(Canvas& canvas)
	{
		TermStatus(canvas);
		const int side = SideIndex();
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				WantBackdrop(nullptr);
				TermStateDraw(canvas, {kLeft, 90, kWidth, 860});
				return;
			}
		}
		const bool still = m_settings.ui.reduceMotion;
		const uint32_t accent = Accent();
		const ui::TextStyle mono = Style({21, ui::Weight::Medium, 1.0f, 0.3f, false, true});
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		m_libraryIndex = std::clamp(m_libraryIndex, 0, std::max(0, total - 1));
		const int game = total ? LibraryGame() : -1;
		WantBackdrop(game >= 0 ? &m_games[game] : nullptr);

		// the table
		EdenArrive(canvas, 1, 12);
		const Box table{kLeft, 90, 1020, 880};
		{
			const char* filters[4] = {"all", "recent", "favourites", "packs"};
			const char* sorts[4] = {"played", "name", "year", "status"};
			const int filter = std::clamp(m_settings.ui.libraryFilter[side], 0, LibraryFilters() - 1);
			std::string title = fmt::format("~/library  sort={}  filter={}", sorts[std::clamp(m_settings.ui.librarySort[side], 0, 3)], filters[filter]);
			if (!m_search.empty())
				title += "  grep=" + m_search;
			TermPanel(canvas, table, title);
		}
		const float cName = table.x + 30, cFormat = table.x + 590, cStatus = table.x + 700, cLast = table.x + 870;
		const ui::TextStyle head = Style({17, ui::Weight::SemiBold, 1.0f, 2.0f, true, true});
		const uint32_t faint = ui::SetAlpha(kText, 0.55f);
		canvas.Text(head, cName, table.y + 30, "name", faint, 400, 1);
		canvas.Text(head, cFormat, table.y + 30, "fmt", faint, 100, 1);
		canvas.Text(head, cStatus, table.y + 30, "status", faint, 160, 1);
		canvas.Text(head, cLast, table.y + 30, "last", faint, 130, 1);
		canvas.Rect({table.x + 20, table.y + 60, table.w - 40, 1}, 0, ui::SetAlpha(accent, 0.3f));
		constexpr float kPitch = 40;
		const float top = table.y + 70, view = table.Bottom() - 20 - top;
		if (total == 0)
			canvas.Text(mono, cName, top + 10, !m_search.empty() ? TrF("Nothing matches “{0}”", m_search) : std::string(Tr("No games yet")), kText, 900, 1);
		float target = m_edenScroll.target;
		if (m_libraryIndex * kPitch < target)
			target = m_libraryIndex * kPitch;
		if ((m_libraryIndex + 1) * kPitch - target > view)
			target = (m_libraryIndex + 1) * kPitch - view;
		m_edenScroll.target = std::max(0.0f, target);
		m_edenScroll.omega = still ? 60.0f : 24.0f;
		m_edenScroll.Update(m_dt);
		const float scroll = m_edenScroll.value;
		canvas.PushClip({table.x, top - 4, table.w, view + 8});
		for (int index = std::max(0, (int)(scroll / kPitch)); index < total; index++)
		{
			const float y = top + index * kPitch - scroll;
			if (y > table.Bottom())
				break;
			const bool focused = !m_railFocus && index == m_libraryIndex;
			if (focused)
				canvas.Rect({table.x + 16, y, table.w - 32, kPitch - 4}, 0, accent);
			const uint32_t ink = focused ? kInk0 : kText;
			if (index < offset)
			{
				canvas.Text(mono, cName, y + 8, Tr("Play from your 3DS"), ink, cFormat - cName - 20, 1);
				canvas.Text(mono, cFormat, y + 8, "artic", focused ? kInk0 : faint, 100, 1);
				continue;
			}
			const Game& g = m_games[m_shelf[index - offset]];
			canvas.Text(mono, cName, y + 8, g.entry.game.name, ink, cFormat - cName - 20, 1);
			canvas.Text(mono, cFormat, y + 8, Lower(g.entry.game.format), focused ? kInk0 : faint, 100, 1);
			if (g.report)
			{
				const std::string kind = ps5compat::Kind(g.report->status);
				const uint32_t colour = focused ? kInk0 : kind == "good" ? kGood : kind == "bad" ? kBad : kWarn;
				canvas.Text(mono, cStatus, y + 8, Lower(TrC("status", g.report->status)), colour, cLast - cStatus - 16, 1);
			}
			else
				canvas.Text(mono, cStatus, y + 8, "-", focused ? kInk0 : faint, 60, 1);
			const std::string when = LastPlayedWords(g.entry.lastPlayed);
			canvas.Text(mono, cLast, y + 8, when.empty() ? "-" : when, focused ? kInk0 : faint, table.Right() - 24 - cLast, 1);
		}
		canvas.PopClip();
		canvas.Text(Style({18, ui::Weight::Medium, 1.0f, 0.5f, false, true}), table.Right() - 140, table.Bottom() - 14,
			total ? fmt::format("{:>3}/{}", m_libraryIndex + 1, total) : std::string("0/0"), accent, 120, 1);
		EdenLeave(canvas);

		// the focused game's details
		EdenArrive(canvas, 2, 12);
		const Box info{1230, 90, kRight - 1230, 880};
		TermPanel(canvas, info, "info");
		if (game >= 0)
		{
			Game& g = m_games[game];
			const ps5emu::Game& entry = g.entry.game;
			Box art = CoverBox(g, 0, info.y + 36 + 380, 380, info.w - 72);
			art.x = info.CentreX() - art.w * 0.5f;
			DrawCover(canvas, g, art, false, 0);
			canvas.Rect(art, 0, ui::SetAlpha(accent, 0.12f));
			for (float ly = art.y; ly < art.Bottom(); ly += 3)
				canvas.Rect({art.x, ly, art.w, 1}, 0, 0x40000000);
			m_edenLaunchFrom = canvas.Placed(art);
			float y = art.Bottom() + 24;
			const ui::TextBlock name = canvas.Text(Style({28, ui::Weight::Bold, 1.15f}), info.x + 32, y, entry.name, accent, info.w - 64, 2);
			y += name.height + 14;
			const int own = OwnSettings(g);
			if (IsWiiU() && m_status.coreReady && g.packsOn < 0)
				g.packsOn = ps5emu::EnabledGraphicPackCount(entry.titleId);
			const std::vector<std::pair<std::string, std::string>> facts = {{"by", Byline(g)},
				{"file", entry.path.filename().string()},
				{"played", Played(g).empty() ? std::string(Tr("Not played yet")) : Played(g)},
				{"config", own ? TrP(own, "{0} of its own", "{0} of its own") : std::string(Tr("Its side's"))},
				{"packs", IsWiiU() && m_status.coreReady ? (g.packsOn > 0 ? TrP(g.packsOn, "{0} on", "{0} on") : std::string(Tr("None on"))) : std::string()}};
			const ui::TextStyle small = Style({19, ui::Weight::Medium, 1.0f, 0.3f, false, true});
			for (const auto& [key, value] : facts)
			{
				if (value.empty() || y > info.Bottom() - 110)
					continue;
				canvas.Text(small, info.x + 32, y, key, faint, 100, 1);
				canvas.Text(small, info.x + 130, y, value, kText, info.w - 162, 1);
				y += 30;
			}
			// what the buttons do, as commands (Cross and Triangle are the keys)
			const Box run = TermButton(canvas, info.x + 32, info.Bottom() - 84, Icon::Cross, Lower(Tr("Play")), false, true);
			TermButton(canvas, run.Right() + 16, info.Bottom() - 84, Icon::Triangle, Lower(Tr("Game settings")), false, false);
		}
		EdenLeave(canvas);
	}

	void Shell::TermSettingsDraw(Canvas& canvas)
	{
		TermStatus(canvas);
		const bool game = InGameSettings();
		const auto pages = game ? SettingsPages() : EdenBoardPages();
		if (pages.empty())
			return;
		int page = 0;
		for (int i = 0; i < (int)pages.size(); i++)
			if (pages[i].id == m_page)
				page = i;
		m_page = pages[page].id;
		const uint32_t accent = Accent();
		const ui::TextStyle mono = Style({21, ui::Weight::Medium, 1.0f, 0.3f, false, true});

		// the tree of pages
		EdenArrive(canvas, 1, 12);
		const Box tree{kLeft, 90, 440, 880};
		TermPanel(canvas, tree, game ? "settings: " + Lower(m_gameName) : std::string("~/settings/") + Lower(SideTitle(m_side)));
		for (int i = 0; i < (int)pages.size(); i++)
		{
			const float y = tree.y + 36 + i * 46;
			if (y > tree.Bottom() - 40)
				break;
			const bool on = i == page;
			if (on && !m_onRows)
				canvas.Rect({tree.x + 16, y - 6, tree.w - 32, 40}, 0, accent);
			const uint32_t ink = on && !m_onRows ? kInk0 : on ? accent : kText;
			canvas.Text(mono, tree.x + 32, y, (on ? "> " : "  ") + Lower(pages[i].title) + "/", ink, tree.w - 64, 1);
		}
		EdenLeave(canvas);

		// the page's rows
		EdenArrive(canvas, 2, 12);
		const Box panel{648, 90, kRight - 648, 880};
		TermPanel(canvas, panel, Lower(pages[page].title));
		canvas.Text(Style({19, ui::Weight::Medium, 1.0f, 0.3f, false, true}), panel.x + 32, panel.y + 30, "# " + pages[page].subtitle, ui::SetAlpha(kText, 0.6f),
			panel.w - 64, 1);
		auto rows = SettingRows(m_page);
		m_settingRow = std::clamp(m_settingRow, 0, std::max(0, (int)rows.size() - 1));
		const float ry = panel.y + 76, rowH = 96, available = panel.Bottom() - 170 - ry;
		const float scroll = std::max(0.0f, (m_settingRow + 1) * (rowH + 12) - available);
		canvas.PushClip({panel.x + 8, ry - 12, panel.w - 16, available + 24});
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const Box box{panel.x + 28, ry + i * (rowH + 12) - scroll, panel.w - 56, rowH};
			if (box.Bottom() < ry - 20 || box.y > panel.Bottom())
				continue;
			const bool focused = m_onRows && i == m_settingRow;
			DrawRow(canvas, rows[i], box, focused);
			if (focused && rows[i].kind == Row::Kind::Hold)
				HoldRing(canvas, box, m_holdProgress);
		}
		const float under = ry + rows.size() * (rowH + 12) - scroll;
		if (m_page == "remote" && m_settings.remoteScreen && !game)
			DrawRemoteScreen(canvas, {panel.x + 28, under + 6, panel.w - 56, 372});
		canvas.PopClip();
		// the focused row's help, or the page's own words, as a comment under the rows
		canvas.Rect({panel.x + 20, panel.Bottom() - 160, panel.w - 40, 1}, 0, ui::SetAlpha(accent, 0.3f));
		std::string help = m_onRows && !rows.empty() ? rows[m_settingRow].help : PanelText(m_page);
		if (help.empty() && m_onRows && !rows.empty())
			help = rows[m_settingRow].description;
		canvas.Text(Style({18, ui::Weight::Regular, 1.4f, 0.2f, false, true}), panel.x + 32, panel.Bottom() - 144, help, ui::SetAlpha(kText, 0.7f), panel.w - 64, 5);
		EdenLeave(canvas);
	}

	void Shell::TermAboutDraw(Canvas& canvas)
	{
		TermStatus(canvas);
		WantBackdrop(nullptr);
		EdenArrive(canvas, 1, 12);
		const Box panel{kLeft, 90, kWidth - 500, 880};
		TermPanel(canvas, panel, "cat ~/about");
		canvas.Text(Style({48, ui::Weight::Bold, 1.1f}), panel.x + 36, panel.y + 40, "PS5CEMU-HAR", Accent(), panel.w - 72, 1);
		canvas.Text(Style({22, ui::Weight::Medium, 1.0f, 0.5f, false, true}), panel.x + 36, panel.y + 104,
			std::string(Tr("Version")) + " " + ps5update::Readable(PS5CEMU_VERSION) + "  |  wii u, 3ds, ds", kText, panel.w - 72, 1);
		canvas.Text(Style({20, ui::Weight::Regular, 1.5f, 0.2f, false, true}), panel.x + 36, panel.y + 160, PanelText("about"), ui::SetAlpha(kText, 0.8f),
			panel.w - 72, 22);
		EdenLeave(canvas);
		EdenArrive(canvas, 2, 12);
		DrawSupport(canvas, {kRight - 460, 90, 460, 600});
		EdenLeave(canvas);
	}

	void Shell::TermDrawLaunch(Canvas& canvas)
	{
		// a run log in a panel, the cover beside it, a bar of #s filling
		const float total = kLaunchSeconds + 0.25f;
		const float p = std::clamp((float)((m_now - m_launch.at) / total), 0.0f, 1.0f);
		const bool still = m_settings.ui.reduceMotion;
		const uint32_t accent = Accent();
		canvas.Rect({0, 0, 1920, 1080}, 0, ui::SetAlpha(kInk0, 0.92f * ui::ease::CubicOut(p / 0.2f)));
		const Box panel{360, 220, 1200, 620};
		canvas.PushAlpha(still ? 1.0f : ui::ease::CubicOut(p / 0.2f));
		TermPanel(canvas, panel, "exec");
		const float height = 520, width = height * std::clamp(m_launch.aspect, 0.5f, 1.4f) * 0.8f;
		const Box rest{panel.Right() - 50 - width, panel.y + 50, width, height * 0.8f};
		const Box at = Lerp(m_launch.from, rest, still ? 1.0f : ui::ease::CubicOut(p / 0.45f));
		if (m_launch.game >= 0 && m_launch.game < (int)m_games.size())
			DrawCover(canvas, m_games[m_launch.game], at, false, 0);
		const ui::TextStyle mono = Style({24, ui::Weight::Medium, 1.0f, 0.3f, false, true});
		const std::string lines[4] = {fmt::format("> run \"{}\"", m_launch.title), "  " + Lower(m_launch.caption),
			fmt::format("  [{}{}]", std::string((size_t)(p * 24), '#'), std::string(24 - (size_t)(p * 24), '.')), p > 0.85f ? "  ok" : ""};
		for (int i = 0; i < 4; i++)
			if (still || p > i * 0.15f)
				canvas.Text(mono, panel.x + 40, panel.y + 60 + i * 52, lines[i], i == 0 ? accent : kText, rest.x - panel.x - 70, 1);
		canvas.PopAlpha();
	}

	void Shell::TermDrawOpening(Canvas& canvas)
	{
		// a boot log, a line at a time, then the launcher
		const bool still = m_settings.ui.reduceMotion;
		if (m_openingAt < 0)
		{
			m_openingAt = m_now;
			m_screenAt = m_now + (still ? 0.1 : 1.0);
		}
		const float t = (float)(m_now - m_openingAt), length = still ? 0.3f : 1.35f;
		if (t >= length)
		{
			m_openingOn = false;
			return;
		}
		const float fade = still ? 1 - t / length : 1 - ui::ease::CubicOut((t - 1.0f) / 0.35f);
		canvas.PushAlpha(std::clamp(fade, 0.0f, 1.0f));
		canvas.Rect({0, 0, 1920, 1080}, 0, kInk0);
		std::vector<std::string> lines = {fmt::format("PS5CEMU-HAR {} (tty0)", ps5update::Readable(PS5CEMU_VERSION)), "", "mount /data ................ ok"};
		for (System side : RailSides())
			lines.push_back(fmt::format("node {:<6} {:<8} ........... ready", Lower(SideTitle(side)), Lower(EmulatorName(side))));
		lines.push_back(fmt::format("library .................... {} games", m_games.size()));
		lines.push_back("");
		lines.push_back("> cd ~/home");
		const ui::TextStyle mono = Style({24, ui::Weight::Medium, 1.0f, 0.3f, false, true});
		const int shown = still ? (int)lines.size() : std::min((int)lines.size(), (int)(t / 0.09f) + 1);
		for (int i = 0; i < shown; i++)
			canvas.Text(mono, 160, 200 + i * 40, lines[i], i == 0 ? Accent() : kText, 1500, 1);
		if (std::fmod(t, 0.5f) < 0.3f)
			canvas.Rect({160, 200 + shown * 40.0f + 4, 14, 26}, 0, Accent());
		canvas.PopAlpha();
	}
}
