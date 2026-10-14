// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Meyer Oscar's Library, the Library of the Meyer Oscar and
// Meyer Oscar Light themes on every side. Recently played: the side's games by when they were last
// played, a row of their box art under Today, This week and Earlier, the focused one's name, play time,
// when it was played and its update under the row. Then the Library: every game on the side as a grid
// of covers or a list, with Sort (Square), Search (Triangle) and Display (R2). Every cover is the whole
// box art at its own shape, never cut to fit.

#include "shell_internal.h"
#include "../../app/emulator.h"

#include <algorithm>
#include <ctime>

namespace ps5shell
{
	using namespace ui::tokens;

	namespace
	{
		constexpr int kRecentMost = 8;
		constexpr float kRecentTop = 224, kRecentHeight = 300, kRecentGap = 24;
		constexpr float kListPitch = 104;

		// tr: the Library's orders (Square), as library.cpp names them
		constexpr const char* kSortNames[] = {TrMark("Recently played"), TrMark("A to Z"), TrMark("Release year"), TrMark("How it runs")};

		// a game's facts in a line, the empty ones left out
		std::string Facts(const std::vector<std::string>& facts)
		{
			std::string line;
			for (const std::string& fact : facts)
				if (!fact.empty())
					line += (line.empty() ? "" : " · ") + fact;
			return line;
		}

		// when a game was played, as the row groups them: 0 today, 1 this week, 2 earlier
		int Group(int64_t when)
		{
			const int64_t now = std::time(nullptr);
			std::tm today{}, then{};
			const std::time_t t0 = now, t1 = when;
			if (localtime_r(&t0, &today) && localtime_r(&t1, &then) && today.tm_yday == then.tm_yday && today.tm_year == then.tm_year)
				return 0;
			return now - when < 7 * 86400 ? 1 : 2;
		}
	}

	Box Shell::CoverBox(Game& game, float x, float bottom, float height, float maxWidth)
	{
		// the box art's own shape once it has loaded, else its case's: the Wii U's tall, the 3DS's and
		// the DS's a little wider than tall
		const ui::Picture& picture = Cover(game);
		float aspect = IsWiiU() ? 0.714f : 1.08f;
		if (picture.texture && picture.width > 0 && picture.height > 0)
			aspect = (float)picture.width / picture.height;
		float w = height * aspect, h = height;
		if (w > maxWidth)
		{
			w = maxWidth;
			h = w / aspect;
		}
		return {x, bottom - h, w, h};
	}

	void Shell::RecentRefresh()
	{
		const uint64_t focused = !m_recent.empty() && m_recentIndex < (int)m_recent.size() ? m_games[m_recent[m_recentIndex]].entry.game.titleId : 0;
		m_recent.clear();
		for (int i = 0; i < (int)m_games.size(); i++)
			if (m_games[i].entry.lastPlayed > 0)
				m_recent.push_back(i);
		std::stable_sort(m_recent.begin(), m_recent.end(), [&](int a, int b) { return m_games[a].entry.lastPlayed > m_games[b].entry.lastPlayed; });
		if ((int)m_recent.size() > kRecentMost)
			m_recent.resize(kRecentMost);
		// the focus stays on its game; a side opening (no game in focus yet) starts on the one played last
		m_recentIndex = 0;
		for (int i = 0; i < (int)m_recent.size(); i++)
			if (m_games[m_recent[i]].entry.game.titleId == (focused ? focused : m_libraryFocusTitle))
				m_recentIndex = i;
		if (m_recent.empty())
			m_oscarZone = 1;
	}

	void Shell::RecentUpdate(const ui::Press& press)
	{
		const Button b = press.button;
		const int side = SideIndex();
		const bool list = m_settings.ui.libraryDisplay[side] == 1;
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		const int columns = list ? 1 : LibraryColumns();
		if (m_recent.empty())
			m_oscarZone = 1;
		switch (b)
		{
		case Button::Square:
			OpenPicker(TrC("tab", "Library"), Tr("Sort"), [] {
				std::vector<std::string> names;
				for (const char* name : kSortNames)
					names.push_back(Tr(name));
				return names;
			}(), m_settings.ui.librarySort[side], [this, side](int choice) {
				m_settings.ui.librarySort[side] = choice;
				SaveSettings();
				LibraryRefresh();
			});
			return;
		case Button::Triangle: OpenKeyboard(); return;
		case Button::R2:
			// a grid of covers or a list, kept for the side
			m_settings.ui.libraryDisplay[side] = list ? 0 : 1;
			SaveSettings();
			m_feedback.Play(ui::Cue::Toggle);
			return;
		case Button::Circle:
			if (!m_search.empty())
			{
				m_search.clear();
				LibraryRefresh();
				m_feedback.Play(ui::Cue::Back);
			}
			else if (m_oscarZone == 1 && !m_recent.empty())
			{
				m_oscarZone = 0;
				m_feedback.Play(ui::Cue::Back);
			}
			else
				FocusBar();
			return;
		case Button::Options:
			if (LibraryGame() >= 0)
				OpenGameMenu(LibraryGame());
			return;
		case Button::Cross:
			if (m_oscarZone == 1 && m_libraryIndex < offset)
			{
				m_feedback.Play(ui::Cue::Select);
				m_pageFrom = ScreenId::Library;
				ArticOpen();
			}
			else if (LibraryGame() >= 0)
				Launch(LibraryGame());
			else
				m_feedback.Play(ui::Cue::Denied);
			return;
		default: break;
		}
		if (m_oscarZone == 0)
		{
			const int count = (int)m_recent.size();
			switch (b)
			{
			case Button::Left:
			case Button::Right:
			{
				const int next = m_recentIndex + (b == Button::Right ? 1 : -1);
				if (next < 0 || next >= count)
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
				{
					m_recentIndex = next;
					m_feedback.Play(ui::Cue::Focus);
				}
				break;
			}
			case Button::Up: FocusBar(); break;
			case Button::Down:
				if (total > 0)
				{
					m_oscarZone = 1;
					m_libraryIndex = std::clamp(m_libraryIndex, 0, total - 1);
					m_feedback.Play(ui::Cue::Focus);
				}
				else
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				break;
			default: break;
			}
		}
		else if (total > 0)
		{
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
				if (list || at % columns == 0)
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
					move(at - 1);
				break;
			case Button::Right:
				if (list || at % columns == columns - 1)
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
					move(at + 1);
				break;
			case Button::Up:
				if (at < columns)
				{
					if (!m_recent.empty())
					{
						m_oscarZone = 0;
						m_feedback.Play(ui::Cue::Focus);
					}
					else
						FocusBar();
				}
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
			case Button::L2: move(0); break; // the first game (R2 is Display here)
			default: break;
			}
		}
		else if (b == Button::Up)
			FocusBar();
		if (LibraryGame() >= 0)
			m_libraryFocusTitle = m_games[LibraryGame()].entry.game.titleId;
	}

	void Shell::RecentDraw(Canvas& canvas)
	{
		const int side = SideIndex();
		const bool list = m_settings.ui.libraryDisplay[side] == 1;
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		const bool recent = !m_recent.empty();
		if (!recent)
			m_oscarZone = 1;
		const int columns = list ? 1 : LibraryColumns();
		const float cellW = IsWiiU() ? 196.0f : 216.0f, cellH = IsWiiU() ? 274.0f : 200.0f;
		const float gapX = IsWiiU() ? 22.0f : 24.0f, pitch = list ? kListPitch : cellH + 26;
		const float headTop = recent ? 590.0f : 132.0f, gridTop = headTop + 62;
		const int game = LibraryGame();
		WantBackdrop(game >= 0 ? &m_games[game] : nullptr);

		// the page scrolls as one: past the Library's first row, Recently played goes up and away, the
		// Library's heading at the top; then only as far as the focused row needs to be seen
		const int focusRow = m_oscarZone == 1 ? std::max(0, m_libraryIndex) / columns : 0;
		float target = m_libraryScroll.target;
		if (m_oscarZone == 0 || focusRow == 0)
			target = 0;
		else
		{
			const float top = gridTop + focusRow * pitch, bottom = top + (list ? kListPitch - 12 : cellH);
			if (bottom - target > 960)
				target = bottom - 960;
			if (top - target < 196)
				target = top - 196;
			target = std::max(target, headTop - 132);
		}
		m_libraryScroll.target = target;
		m_libraryScroll.omega = m_settings.ui.reduceMotion ? 60.0f : kScrollOmega;
		m_libraryScroll.Update(m_dt);
		const float scroll = m_libraryScroll.value;

		canvas.PushAlpha(Enter(1));
		canvas.PushClip({0, 116, 1920, 964});
		canvas.PushOffset(0, -scroll);
		const ui::TextStyle heading = Style({32, ui::Weight::SemiBold, 1.0f});
		const ui::TextStyle tool = Style({22, ui::Weight::Medium, 1.0f});

		// Recently played
		Box focusedBox{};
		if (recent)
		{
			canvas.Text(heading, kSafeX, 132, Tr("Recently played"), kText, 900, 1);
			// tr: when the games in Recently played were last played: the labels over the row
			const char* groups[3] = {Tr("Today"), Tr("This week"), Tr("Earlier")};
			float x = kSafeX;
			int lastGroup = -1;
			for (int i = 0; i < (int)m_recent.size(); i++)
			{
				Game& g = m_games[m_recent[i]];
				const Box box = CoverBox(g, x, kRecentTop + kRecentHeight, kRecentHeight, 420);
				const int group = Group(g.entry.lastPlayed);
				if (group != lastGroup)
				{
					canvas.Text(Style(kOverlineStyle), box.x, 176, groups[group], Tertiary(), 0, 1);
					lastGroup = group;
				}
				if (box.x < 1920)
				{
					const bool focused = !m_onBar && m_oscarZone == 0 && i == m_recentIndex;
					if (focused)
						focusedBox = box;
					else
						DrawCover(canvas, g, box, false, 0);
				}
				x = box.Right() + kRecentGap + (!m_onBar && m_oscarZone == 0 && i == m_recentIndex ? 12 : 0);
			}
			if (focusedBox.w > 0)
			{
				// lifted from its bottom left, so it rises clear of its neighbours
				const float lift = m_settings.ui.reduceMotion ? 0.0f : 0.05f;
				const Box lifted{focusedBox.x, focusedBox.Bottom() - focusedBox.h * (1 + lift), focusedBox.w * (1 + lift), focusedBox.h * (1 + lift)};
				DrawCover(canvas, m_games[m_recent[m_recentIndex]], lifted, true, 0);
			}
			// the focused game, under the row: its name, play time, when it was last played, its update and DLC
			Game& g = m_games[m_recent[std::clamp(m_recentIndex, 0, (int)m_recent.size() - 1)]];
			const ui::TextBlock name = canvas.Text(Style({26, ui::Weight::SemiBold, 1.0f}), kSafeX, 544, g.entry.game.name, kText, 900, 1);
			std::vector<std::string> facts = {Played(g), LastPlayedWords(g.entry.lastPlayed)};
			const ps5emu::Game& entry = g.entry.game;
			if (IsWiiU() && entry.hasUpdate)
				// tr: the update a Wii U game has installed ({0}: its version, as 208)
				facts.push_back(TrF("Update v{0}", entry.version));
			if (entry.dlcCount)
				facts.push_back(TrP(entry.dlcCount, "{0} DLC", "{0} DLC"));
			canvas.Text(Style({22, ui::Weight::Regular, 1.0f}), kSafeX + name.width + 16, 548, Facts(facts), Secondary(), 1920 - 2 * kSafeX - name.width - 16, 1);
		}

		// the Library: its heading and count, then Sort, Search and Display at the right
		const ui::TextBlock title = canvas.Text(heading, kSafeX, headTop, TrC("tab", "Library"), kText, 600, 1);
		// tr: how many games the Library has
		canvas.Text(tool, kSafeX + title.width + 24, headTop + 9, TrP((int)m_games.size(), "{0} game", "{0} games"), Tertiary(), 300, 1);
		float right = 1920 - kSafeX;
		{
			// Display: a list or a grid, the one in use lit
			const Box segment{right - 96, headTop - 2, 96, 44};
			canvas.Rect(segment, 12, Surface());
			canvas.Ring(segment, 12, 1.5f, kGlassEdge);
			for (int i = 0; i < 2; i++)
			{
				const Box cell{segment.x + 4 + i * 44, segment.y + 4, 44, 36};
				const bool on = (i == 0) == list;
				if (on)
					canvas.Rect(cell, 8, ui::SetAlpha(Accent(), 0.24f));
				const uint32_t ink = on ? kText : Tertiary();
				if (i == 0)
					for (int line = 0; line < 3; line++)
						canvas.Rect({cell.x + 12, cell.y + 10 + line * 7, 20, 3}, 1.5f, ink);
				else
					for (int square = 0; square < 4; square++)
						canvas.Rect({cell.x + 12 + (square % 2) * 11, cell.y + 8 + (square / 2) * 11, 9, 9}, 2, ink);
			}
			right = segment.x - 14;
			const ui::TextBlock display = m_fonts.Layout(tool, Tr("Display"));
			right -= display.width;
			canvas.Text(display, right, headTop + 20 - display.height * 0.5f, Secondary());
			canvas.Draw(Icon::R2, {right - 38, headTop + 5, 30, 30}, Secondary());
			right -= 38 + 34;
			const ui::TextBlock search = m_fonts.Layout(tool, m_search.empty() ? std::string(Tr("Search")) : TrF("Search: {0}", m_search));
			right -= search.width;
			canvas.Text(search, right, headTop + 20 - search.height * 0.5f, m_search.empty() ? Secondary() : kText);
			canvas.Draw(Icon::Triangle, {right - 38, headTop + 5, 30, 30}, Secondary());
			right -= 38 + 34;
			const ui::TextBlock sort = m_fonts.Layout(tool, TrF("Sort: {0}", Tr(kSortNames[std::clamp(m_settings.ui.librarySort[side], 0, 3)])));
			right -= sort.width;
			canvas.Text(sort, right, headTop + 20 - sort.height * 0.5f, Secondary());
			canvas.Draw(Icon::Square, {right - 38, headTop + 5, 30, 30}, Secondary());
		}

		if (total == 0)
		{
			canvas.Text(Style(kBodyStyle), kSafeX, gridTop + 10,
				!m_search.empty() ? TrF("Nothing matches “{0}”. Circle clears the search.", m_search) :
				m_scanning		  ? std::string(Tr("Looking for games…")) :
									std::string(Tr("No games yet. Settings > Game files says where they go.")),
				Secondary(), 1200, 2);
		}
		Box libraryFocus{};
		int libraryFocusIndex = -1;
		for (int index = 0; index < total; index++)
		{
			const int row = index / columns, column = index % columns;
			const float top = gridTop + row * pitch;
			if (top - scroll > 1080 || top + pitch - scroll < 116)
				continue;
			const bool focused = !m_onBar && m_oscarZone == 1 && index == m_libraryIndex;
			if (list)
			{
				// a row each: a small cover, the name, play time and when it was played, how it runs
				const Box rowBox{kSafeX, top, 1920 - 2 * kSafeX - 40, kListPitch - 12};
				canvas.Rect(rowBox, kRadiusCard, focused ? Surface2() : Surface());
				if (focused)
					Focus(rowBox, kRadiusCard);
				if (index < offset)
				{
					canvas.Text(Style(kBodyStyle), rowBox.x + 120, rowBox.CentreY() - 16, Tr("Play from your 3DS"), kText, 900, 1);
					continue;
				}
				Game& g = m_games[m_shelf[index - offset]];
				const Box cover = CoverBox(g, rowBox.x + 14, rowBox.Bottom() - 10, rowBox.h - 20, 90);
				DrawCover(canvas, g, cover.Offset((90 - cover.w) * 0.5f, 0), false, 0);
				canvas.Text(Style({26, ui::Weight::SemiBold, 1.0f}), rowBox.x + 124, rowBox.y + 18, g.entry.game.name, kText, 1000, 1);
				const std::string facts = Facts({Played(g), LastPlayedWords(g.entry.lastPlayed)});
				canvas.Text(Style({21, ui::Weight::Regular, 1.0f}), rowBox.x + 124, rowBox.y + 54, facts.empty() ? Byline(g) : facts, Secondary(), 1000, 1);
				if (g.report)
					Chip(canvas, rowBox.Right() - 260, rowBox.CentreY() - 17, TrC("status", g.report->status), ps5compat::Kind(g.report->status), 34);
				continue;
			}
			const Box cell{kSafeX + column * (cellW + gapX), top, cellW, cellH};
			if (index < offset)
			{
				// Artic Base: a game from your 3DS
				canvas.Rect(cell, kRadiusCard, Wash(focused ? 0.1f : 0.05f));
				canvas.Ring(cell, kRadiusCard, 2, ui::SetAlpha(kN3ds, 0.45f));
				canvas.Text(Style({24, ui::Weight::SemiBold, 1.2f}), cell.x + 14, cell.y + cell.h * 0.22f, Tr("Play from your 3DS"), kText, cell.w - 28, 2, ui::Align::Centre);
				if (focused)
					Focus(cell, kRadiusCard, kN3ds);
				continue;
			}
			Game& g = m_games[m_shelf[index - offset]];
			Box box = CoverBox(g, cell.x, cell.Bottom(), cellH, cellW);
			box = box.Offset((cellW - box.w) * 0.5f, 0);
			if (focused)
			{
				libraryFocus = box;
				libraryFocusIndex = index;
			}
			else
				DrawCover(canvas, g, box, false, 0);
		}
		if (libraryFocusIndex >= offset && libraryFocus.w > 0)
			DrawCover(canvas, m_games[m_shelf[libraryFocusIndex - offset]], libraryFocus, true, m_settings.ui.reduceMotion ? 0.0f : 0.05f);
		canvas.PopOffset();
		canvas.PopClip();

		// the Library fades into the page under its last row seen, over the hints, as the mockup has it
		canvas.LinearGradient({0, 880, 1920, 200}, 0, ui::SetAlpha(kInk0, 0), ui::SetAlpha(kInk0, 1), 0, 880, 0, 1020);
		// where in the Library the page is: a thin bar at the right, while it has more than is seen
		const int rows = (total + columns - 1) / std::max(1, columns);
		const float content = rows * pitch, viewTop = std::max(gridTop - scroll, 196.0f), view = 960 - viewTop;
		if (total > 0 && content > view + 1 && view > 80)
		{
			const float seen = std::clamp(scroll + viewTop - gridTop, 0.0f, content - view);
			const float thumb = std::max(48.0f, view * view / content);
			const float x = 1920 - 52 - 4;
			canvas.Rect({x, viewTop, 4, view}, 2, Wash(0.08f));
			canvas.Rect({x, viewTop + (view - thumb) * seen / std::max(1.0f, content - view), 4, thumb}, 2, Wash(0.4f));
		}

		// in the grid, the focused game's name and facts at the bottom left, over the fade
		if (!list && m_oscarZone == 1 && game >= 0)
		{
			Game& g = m_games[game];
			const ui::TextBlock name = canvas.Text(Style({26, ui::Weight::SemiBold, 1.0f}), kSafeX, 1006, g.entry.game.name, kText, 760, 1);
			canvas.Text(Style({22, ui::Weight::Regular, 1.0f}), kSafeX + name.width + 16, 1010, Facts({Played(g), LastPlayedWords(g.entry.lastPlayed)}),
				Secondary(), 300, 1);
		}
		canvas.PopAlpha();
	}
}
