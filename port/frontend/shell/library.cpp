// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the Library: where every side opens, every game on it,
// found fast, the one played last in focus. Filters across the top with their counts, Sort (Square)
// and Search (Triangle); the covers on a shelf of equal rows, the focused one lifted with its name and
// facts under it; L2 and R2 jump to the previous or next letter (or year, or status); an index of the
// letters at the right. On the 3DS side the shelf's first tile is Artic Base. Before there is a game
// to show (a notice, Cemu starting, the first scan, no games yet) a card says why, with what to do
// (6.1: what Home showed).

#include "shell_internal.h"
#include "../actions.h"
#include "../../app/emulator.h"
#include "../../eden/library.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		// tr: the Library's orders (Square)
		constexpr const char* kSorts[] = {TrMark("Recently played"), TrMark("A to Z"), TrMark("Release year"), TrMark("How it runs")};

		std::vector<std::string> SortNames()
		{
			std::vector<std::string> names;
			for (const char* sort : kSorts)
				names.push_back(Tr(sort));
			return names;
		}
		constexpr float kGridTop = 236;

		int StatusRank(const ps5compat::Report* report)
		{
			if (!report)
				return 4;
			const std::string kind = ps5compat::Kind(report->status);
			return report->status == "Playable" ? 0 : kind == "good" ? 0 : kind == "warn" ? 1 : report->status == "Crashes" ? 2 : 3;
		}
	}

	int Shell::LibraryColumns() const
	{
		return IsWiiU() ? 8 : RecentLayout() ? 7 : 6;
	}

	int Shell::LibraryFilters() const
	{
		return IsWiiU() && m_status.coreReady ? 4 : 3;
	}

	int Shell::LibraryGame() const
	{
		// Meyer Oscar's, in its Recently played row
		if (RecentLayout() && m_oscarZone == 0 && m_recentIndex >= 0 && m_recentIndex < (int)m_recent.size())
			return m_recent[m_recentIndex] < (int)m_games.size() ? m_recent[m_recentIndex] : -1;
		const int at = m_libraryIndex - ArticTiles();
		const int game = at >= 0 && at < (int)m_shelf.size() ? m_shelf[at] : -1;
		return game < (int)m_games.size() ? game : -1; // a shelf of a side being left has none
	}

	bool Shell::LibraryState(std::string& kicker, std::string& title, std::string& text, std::vector<std::string>& buttons) const
	{
		buttons.clear();
		if (!Notice().empty())
		{
			kicker = Tr("Setup");
			title = Tr("Something needs a look");
			text = Notice();
			buttons = {Tr("Setup check"), Tr("Diagnostics")};
			return true;
		}
		// while Cemu's library loads, the games the catalogue has are shown (Play waits for it)
		if (!CoreReady() && (!m_status.coreStarting || m_games.empty()))
		{
			kicker = "Wii U";
			title = Tr("Starting Cemu");
			text = Tr("Its settings, graphic packs and controllers are loading, and then it looks for your games.");
			return true;
		}
		if (!m_games.empty())
			return false;
		if (m_scanning)
		{
			kicker = SystemName(m_side);
			title = Tr("Looking for games…");
			text = TrF("In {0}.", ShortPath(GamesFolder(m_side), 60));
			return true;
		}
		const std::string& folder = GamesFolder(m_side);
		kicker = Tr("Welcome");
		title = Tr("Your games go here");
		// tr: {0} is the games folder
		text = Is3ds() ? TrF("Put your 3DS games (.3ds, .cci, .cxi, .3dsx, decrypted, alone or in .zip, .7z or .rar archives) in {0}, or "
							 "choose any folder the PS5 can read, such as one on a USB drive. Folders inside it are searched too.",
							 folder) :
			IsDs()	   ? TrF("Put your DS games (.nds, alone or in .zip, .7z or .rar archives) in {0}, or choose any folder the PS5 can read, "
							 "such as one on a USB drive. Folders inside it are searched too.",
							 folder) :
			// tr: {1} is Eden's game files folder, which has keys/ and firmware/ in it
			IsSwitch() ? TrF("Put your Switch games (.nsp or .xci, alone or each in a folder of its own with its update and DLC) in {0}. "
							 "Eden needs your Switch's keys in {1}/keys and its firmware in {1}/firmware too.",
							 ps5eden::ScanFolder(folder), ps5eden::GameFilesFolder()) :
						 TrF("Put your Wii U games (.wua, .wud, .wux, or folders with code, content and meta, alone or in .zip, .7z or .rar "
							 "archives) in {0}, or choose any folder the PS5 can read, such as one on a USB drive.",
							 folder);
		buttons = {Tr("Choose a folder"), Tr("Setup check")};
		if (Is3ds())
			buttons.insert(buttons.begin() + 1, Tr("Play from your 3DS")); // Artic Base needs no games here
		if (IsSwitch())
			buttons.insert(buttons.begin() + 1, Tr("Open ProsperoEden")); // its own launcher, with its downloads
		return true;
	}

	void Shell::LibraryStateUpdate(const ui::Press& press)
	{
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		LibraryState(kicker, title, text, buttons);
		const Button b = press.button;
		const int count = (int)buttons.size();
		if (b == Button::Up || b == Button::Circle)
			FocusBar();
		else if ((b == Button::Left || b == Button::Right) && count > 0)
		{
			m_stateButton = (m_stateButton + (b == Button::Right ? 1 : count - 1)) % count;
			m_feedback.Play(ui::Cue::Focus);
		}
		else if (b == Button::Cross)
		{
			if (count == 0)
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			m_feedback.Play(ui::Cue::Select);
			// the buttons are in the menus' language: each is told by its words there
			const std::string& chosen = buttons[std::clamp(m_stateButton, 0, count - 1)];
			if (chosen == Tr("Setup check"))
				SetupOpen(false);
			else if (chosen == Tr("Play from your 3DS"))
			{
				m_pageFrom = ScreenId::Library;
				ArticOpen();
			}
			else if (chosen == Tr("Open ProsperoEden"))
				OpenEden();
			else if (chosen == Tr("Diagnostics"))
			{
				ShowTab(ScreenId::Settings);
				SettingsOpen("diagnostics");
			}
			else
			{
				m_pageFrom = ScreenId::Library;
				m_filesSide = m_side;
				FilesOpen(0);
			}
		}
	}

	void Shell::LibraryStateDraw(Canvas& canvas)
	{
		std::string kicker, title, text;
		std::vector<std::string> buttons;
		LibraryState(kicker, title, text, buttons);
		WantBackdrop(nullptr);
		m_stateButtons.clear();
		canvas.PushAlpha(Enter(1));
		const Box card{kSafeX, 300, 1100, 520};
		canvas.Text(Style(kOverlineStyle), card.x, card.y, kicker, Secondary());
		const ui::TextBlock titleBlock = canvas.Text(Style(kDisplayStyle), card.x, card.y + 36, title, kText, card.w, 2);
		const ui::TextBlock textBlock = canvas.Text(Style(kBodyStyle), card.x, card.y + 56 + titleBlock.height, text, Secondary(), 960, 5);
		float x = card.x;
		m_stateButton = std::clamp(m_stateButton, 0, std::max(0, (int)buttons.size() - 1));
		for (int i = 0; i < (int)buttons.size(); i++)
		{
			const Box b = PillButton(canvas, x, card.y + 96 + titleBlock.height + textBlock.height, buttons[i], i == 0, !m_onBar && m_stateButton == i);
			m_stateButtons.push_back(b);
			x += b.w + 20;
		}
		canvas.PopAlpha();
	}

	void Shell::LibraryRefresh()
	{
		const int side = SideIndex();
		int& filter = m_settings.ui.libraryFilter[side];
		if (filter >= LibraryFilters())
			filter = 0;
		const int shown = RecentLayout() ? 0 : filter; // Meyer Oscar's Library has no filters: every game
		const int sort = std::clamp(m_settings.ui.librarySort[side], 0, 3);
		const int focused = LibraryGame();
		if (focused >= 0)
			m_libraryFocusTitle = m_games[focused].entry.game.titleId;
		const int64_t recent = std::time(nullptr) - 30 * 86400;
		const std::string search = Lower(m_search);
		m_shelf.clear();
		for (int i = 0; i < (int)m_games.size(); i++)
		{
			Game& game = m_games[i];
			if (shown == 1 && game.entry.added < recent)
				continue;
			if (shown == 2 && !game.entry.favourite)
				continue;
			if (shown == 3 && IsWiiU())
			{
				if (game.packsOn < 0) // none known while Cemu's library loads; asked again once it has
					game.packsOn = m_status.coreReady ? ps5emu::EnabledGraphicPackCount(game.entry.game.titleId) : 0;
				if (game.packsOn == 0)
					continue;
			}
			// the keyboard's letters are Latin: a name in another script is found by GameTDB's English one too
			if (!search.empty() && Lower(game.entry.game.name).find(search) == std::string::npos &&
				Lower(game.info.title).find(search) == std::string::npos)
				continue;
			m_shelf.push_back(i);
		}
		std::stable_sort(m_shelf.begin(), m_shelf.end(), [&](int a, int b) {
			const Game& x = m_games[a];
			const Game& y = m_games[b];
			switch (sort)
			{
			case 0:
				if (x.entry.lastPlayed != y.entry.lastPlayed)
					return x.entry.lastPlayed > y.entry.lastPlayed;
				break;
			case 2:
				if (x.year != y.year)
					return (x.year ? x.year : -1) > (y.year ? y.year : -1);
				break;
			case 3:
				if (StatusRank(x.report) != StatusRank(y.report))
					return StatusRank(x.report) < StatusRank(y.report);
				break;
			default: break;
			}
			return x.sortName < y.sortName;
		});
		// the focus stays on the game it was on (or goes to the one asked for: the one played last)
		const int offset = ArticTiles();
		if (m_libraryIndex < 0 && !m_shelf.empty())
			m_libraryIndex = offset; // the first game, unless the one asked for is found below
		m_libraryIndex = std::clamp(m_libraryIndex, 0, std::max(0, (int)m_shelf.size() + offset - 1));
		for (int i = 0; i < (int)m_shelf.size(); i++)
			if (m_games[m_shelf[i]].entry.game.titleId == m_libraryFocusTitle)
				m_libraryIndex = i + offset;
		RecentRefresh();
	}

	void Shell::LibraryUpdate(const ui::Press& press)
	{
		if (SectionLayout())
		{
			EdenLibraryUpdate(press); // covers beside the game's details (eden.cpp, 6.17)
			return;
		}
		const Button b = press.button;
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				LibraryStateUpdate(press);
				return;
			}
		}
		if (RecentLayout())
		{
			RecentUpdate(press);
			return;
		}
		const int side = SideIndex();
		const int columns = LibraryColumns();
		const int offset = ArticTiles();
		const int total = (int)m_shelf.size() + offset;
		auto sortPicker = [this, side] {
			OpenPicker(TrC("tab", "Library"), Tr("Sort"), SortNames(), m_settings.ui.librarySort[side],
				[this, side](int choice) {
					m_settings.ui.librarySort[side] = choice;
					SaveSettings();
					LibraryRefresh();
				});
		};
		if (b == Button::Square)
		{
			sortPicker();
			return;
		}
		if (b == Button::Triangle)
		{
			OpenKeyboard();
			return;
		}
		if (b == Button::Circle)
		{
			// the search let go, then up to the filters, then the bar: the Library is where a side opens
			if (!m_search.empty())
			{
				m_search.clear();
				LibraryRefresh();
				m_feedback.Play(ui::Cue::Back);
			}
			else if (m_libraryZone == 1)
			{
				m_libraryZone = 0;
				m_feedback.Play(ui::Cue::Back);
			}
			else
				FocusBar();
			return;
		}
		if (m_libraryZone == 0)
		{
			const int filters = LibraryFilters();
			int& filter = m_settings.ui.libraryFilter[side];
			switch (b)
			{
			case Button::Left:
			case Button::Right:
			{
				const int next = filter + (b == Button::Right ? 1 : -1);
				if (next < 0 || next >= filters)
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				else
				{
					filter = next;
					SaveSettings();
					m_libraryIndex = 0;
					m_libraryFocusTitle = 0;
					LibraryRefresh();
					m_feedback.Play(ui::Cue::Focus);
				}
				break;
			}
			case Button::Up: FocusBar(); break;
			case Button::Down:
			case Button::Cross:
				if (total > 0)
				{
					m_libraryZone = 1;
					m_feedback.Play(ui::Cue::Focus);
				}
				else
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				break;
			default: break;
			}
			return;
		}
		if (total == 0)
		{
			if (b == Button::Up)
				m_libraryZone = 0;
			return;
		}
		int& at = m_libraryIndex;
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
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
				move(at - 1);
			break;
		case Button::Right:
			if (at % columns == columns - 1)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
				move(at + 1);
			break;
		case Button::Up:
			if (at < columns)
			{
				m_libraryZone = 0;
				m_feedback.Play(ui::Cue::Focus);
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
		case Button::L2:
		case Button::R2:
		{
			// to the previous or next group: letter, year or status, as sorted
			const int sort = m_settings.ui.librarySort[side];
			auto group = [&](int index) -> int {
				if (index < offset)
					return -1;
				const Game& game = m_games[m_shelf[index - offset]];
				if (sort == 2)
					return game.year;
				if (sort == 3)
					return StatusRank(game.report);
				if (sort == 0)
					return (index - offset) / 10;
				const char c = game.sortName.empty() ? '#' : game.sortName[0];
				return std::isalpha((unsigned char)c) ? c : '#';
			};
			const int current = group(at);
			int to = at;
			if (b == Button::R2)
			{
				while (to < total - 1 && group(to) == current)
					to++;
			}
			else
			{
				while (to > 0 && group(to - 1) == current)
					to--;
				if (to == at && to > 0)
				{
					const int previous = group(to - 1);
					to--;
					while (to > 0 && group(to - 1) == previous)
						to--;
				}
			}
			move(to);
			break;
		}
		case Button::Cross:
		{
			if (at < ArticTiles())
			{
				m_feedback.Play(ui::Cue::Select);
				m_pageFrom = ScreenId::Library;
				ArticOpen();
				break;
			}
			const int game = LibraryGame();
			if (game >= 0)
				Launch(game);
			break;
		}
		case Button::Options:
			if (LibraryGame() >= 0)
				OpenGameMenu(LibraryGame());
			break;
		default: break;
		}
		if (LibraryGame() >= 0)
			m_libraryFocusTitle = m_games[LibraryGame()].entry.game.titleId;
	}

	void Shell::LibraryDraw(Canvas& canvas)
	{
		if (SectionLayout())
		{
			EdenLibraryDraw(canvas);
			return;
		}
		DrawBar(canvas, true);
		{
			std::string kicker, title, text;
			std::vector<std::string> buttons;
			if (LibraryState(kicker, title, text, buttons))
			{
				LibraryStateDraw(canvas);
				return;
			}
		}
		if (RecentLayout())
		{
			RecentDraw(canvas);
			return;
		}
		const int side = SideIndex();
		const bool wiiu = IsWiiU();
		const int offset = ArticTiles();
		const int columns = LibraryColumns();
		const float tileW = wiiu ? 186.0f : 216.0f, tileH = wiiu ? 260.0f : 200.0f, gap = wiiu ? 24.0f : 28.0f;
		const float pitch = tileH + 112;
		const int total = (int)m_shelf.size() + offset;

		// the filters, with their counts, and Sort and Search at the right
		canvas.PushAlpha(Enter(1));
		const int64_t recent = std::time(nullptr) - 30 * 86400;
		int counts[4] = {(int)m_games.size(), 0, 0, 0};
		for (Game& game : m_games)
		{
			counts[1] += game.entry.added >= recent;
			counts[2] += game.entry.favourite;
			counts[3] += game.packsOn > 0;
		}
		// tr: the Library's filters, each with its count after it
		const char* names[4] = {TrC("filter", "All"), Tr("Recently added"), Tr("Favourites"), Tr("Graphic packs on")};
		const int filters = LibraryFilters();
		const int filter = m_settings.ui.libraryFilter[side];
		// Sort and Search at the right first: the filters have the rest of the row, their text a step
		// smaller at a time in a language whose words do not fit it
		const ui::TextStyle action = Style({24, ui::Weight::Medium, 1.0f});
		const std::string sortLabel = TrF("Sort: {0}", Tr(kSorts[std::clamp(m_settings.ui.librarySort[side], 0, 3)]));
		const float actionsWidth = m_fonts.Width(action, Tr("Search")) + 36 + 34 + m_fonts.Width(action, sortLabel) + 36;
		const float filtersRoom = 1920 - 2 * kSafeX - actionsWidth - 40 - (m_scanning || !m_search.empty() ? 260 : 0);
		ui::TextStyle pillStyle = Style({26, ui::Weight::Medium, 1.0f});
		ui::TextStyle countStyle = Style({20, ui::Weight::SemiBold, 1.0f, 0, false, true});
		auto filtersWidth = [&] {
			float width = 0;
			for (int i = 0; i < filters; i++)
				width += 26 + m_fonts.Width(pillStyle, names[i]) + 12 + m_fonts.Width(countStyle, std::to_string(counts[i])) + 26 + 14;
			return width;
		};
		for (int step = 0; step < 6 && filtersWidth() > filtersRoom; step++)
		{
			pillStyle.size -= 2;
			countStyle.size -= 1;
		}
		float x = kSafeX;
		for (int i = 0; i < filters; i++)
		{
			const bool on = i == filter;
			const std::string count = i == 3 && counts[3] == 0 ? "" : std::to_string(counts[i]);
			const float w = 26 + m_fonts.Width(pillStyle, names[i]) + (count.empty() ? 0 : 12 + m_fonts.Width(countStyle, count)) + 26;
			const Box pill{x, 136, w, 52};
			canvas.Rect(pill, 26, on ? ui::SetAlpha(Accent(), 0.22f) : Surface());
			canvas.Ring(pill, 26, 1.5f, on ? ui::SetAlpha(Accent(), 0.5f) : kGlassEdge);
			const ui::TextBlock text = m_fonts.Layout(pillStyle, names[i]);
			canvas.Text(text, x + 26, pill.CentreY() - text.height * 0.5f, on ? kText : Secondary());
			if (!count.empty())
			{
				const ui::TextBlock number = m_fonts.Layout(countStyle, count);
				canvas.Text(number, x + 26 + text.width + 12, pill.CentreY() - number.height * 0.5f, on ? Accent() : Tertiary());
			}
			if (!m_onBar && m_libraryZone == 0 && on)
				Focus(pill, 26);
			x += w + 14;
		}
		const float room = 1920 - kSafeX - actionsWidth - 20 - (x + 12);
		if (!m_search.empty())
			Chip(canvas, x + 6, 143, TrF("Search: {0}", m_search));
		if (m_scanning)
			canvas.Text(Style(kCaptionStyle), x + 12, 150, Tr("Looking for new games…"), Tertiary(), std::max(120.0f, room), 1);
		// Sort and Search
		float right = 1920 - kSafeX;
		const ui::TextBlock searchText = m_fonts.Layout(action, Tr("Search"));
		right -= searchText.width;
		canvas.Text(searchText, right, 162 - searchText.height * 0.5f, Secondary());
		canvas.Draw(Icon::Triangle, {right - 36, 148, 28, 28}, Secondary());
		right -= 36 + 34;
		const ui::TextBlock sortText = m_fonts.Layout(action, sortLabel);
		right -= sortText.width;
		canvas.Text(sortText, right, 162 - sortText.height * 0.5f, Secondary());
		canvas.Draw(Icon::Square, {right - 36, 148, 28, 28}, Secondary());
		canvas.PopAlpha();

		// the shelf
		if (total == 0)
		{
			WantBackdrop(nullptr);
			canvas.PushAlpha(Enter(2));
			canvas.Text(Style(kTitleStyle), kSafeX, 320,
				m_scanning			? std::string(Tr("Looking for games…")) :
				!m_search.empty()	? TrF("Nothing matches “{0}”", m_search) :
				filter				? std::string(Tr("None here yet")) :
									  std::string(Tr("No games yet")),
				kText, 1920 - 2 * kSafeX, 1);
			canvas.Text(Style(kBodyStyle), kSafeX, 390,
				!m_search.empty() ? Tr("Circle clears the search.") :
				filter == 2		  ? Tr("Options on a game, then Favourite, puts it here.") :
				filter			  ? Tr("All shows every game.") :
									Tr("Settings > Game files says where they go."),
				Secondary(), 900, 3);
			canvas.PopAlpha();
			return;
		}
		m_libraryIndex = std::clamp(m_libraryIndex, 0, total - 1);
		const int focusRow = m_libraryIndex / columns;
		m_libraryScroll.target = std::max(0, focusRow - 1) * pitch;
		m_libraryScroll.omega = m_settings.ui.reduceMotion ? 60.0f : kScrollOmega;
		m_libraryScroll.Update(m_dt);
		const float scroll = m_libraryScroll.value;
		const int game = LibraryGame();
		WantBackdrop(game >= 0 ? &m_games[game] : nullptr);
		canvas.PushAlpha(Enter(2));
		canvas.PushOffset(0, (1 - Enter(2)) * 30);
		canvas.PushClip({0, 210, 1920, 870});
		Box focusedBox{};
		for (int index = 0; index < total; index++)
		{
			const int row = index / columns, column = index % columns;
			const Box box{kSafeX + column * (tileW + gap), kGridTop + row * pitch - scroll, tileW, tileH};
			if (box.Bottom() < 200 || box.y > 1090)
				continue;
			const bool focused = !m_onBar && m_libraryZone == 1 && index == m_libraryIndex;
			if (focused)
			{
				focusedBox = box;
				continue; // drawn last, over its neighbours
			}
			if (index < offset)
			{
				// Artic Base: a game from your 3DS
				canvas.Rect(box, kRadiusCard, Wash(0.05f));
				canvas.Ring(box, kRadiusCard, 2, ui::SetAlpha(kN3ds, 0.45f));
				canvas.Text(Style({26, ui::Weight::SemiBold, 1.2f}), box.x + 16, box.y + box.h * 0.22f, Tr("Play from your 3DS"), kText, box.w - 32, 2,
					ui::Align::Centre);
				canvas.Text(Style({19, ui::Weight::Regular, 1.3f}), box.x + 16, box.y + box.h * 0.6f, Tr("Artic Base, over your network"), Secondary(),
					box.w - 32, 2, ui::Align::Centre);
			}
			else
				DrawCover(canvas, m_games[m_shelf[index - offset]], box, false, 0);
		}
		if (focusedBox.w > 0)
		{
			const float lift = m_settings.ui.reduceMotion ? 0.0f : 0.07f;
			if (m_libraryIndex < offset)
			{
				const Box box = focusedBox.Scaled(1 + lift);
				canvas.Rect(box, kRadiusCard, Wash(0.1f));
				canvas.Text(Style({26, ui::Weight::SemiBold, 1.2f}), box.x + 16, box.y + box.h * 0.22f, Tr("Play from your 3DS"), kText, box.w - 32, 2,
					ui::Align::Centre);
				canvas.Text(Style({19, ui::Weight::Regular, 1.3f}), box.x + 16, box.y + box.h * 0.6f, Tr("Artic Base, over your network"), Secondary(),
					box.w - 32, 2, ui::Align::Centre);
				Focus(box, kRadiusCard, kN3ds);
			}
			else
			{
				Game& g = m_games[m_shelf[m_libraryIndex - offset]];
				DrawCover(canvas, g, focusedBox, true, lift);
				// its name and facts under it
				const float under = focusedBox.Bottom() + tileH * lift * 0.5f + 22;
				const float infoX = std::min(focusedBox.x, 1920 - kSafeX - 640);
				const ui::TextBlock name = canvas.Text(Style({28, ui::Weight::SemiBold, 1.15f}), infoX, under, g.entry.game.name, kText, 640, 1);
				float fx = infoX;
				const ui::TextBlock facts = canvas.Text(Style({22, ui::Weight::Regular, 1.2f}), fx, under + name.height + 6, Byline(g), Secondary(), 420, 1);
				fx += facts.width + 14;
				if (g.report)
					fx += Chip(canvas, fx, under + name.height + 2, TrC("status", g.report->status), ps5compat::Kind(g.report->status), 34) + 10;
				if (!Played(g).empty())
					Chip(canvas, fx, under + name.height + 2, Played(g), "", 34);
			}
		}
		canvas.PopClip();
		canvas.PopOffset();
		canvas.PopAlpha();

		// the index of letters at the right, for A to Z
		if (m_settings.ui.librarySort[side] == 1 && m_shelf.size() > 12)
		{
			std::vector<char> letters;
			for (int index : m_shelf)
			{
				const char c = m_games[index].sortName.empty() ? '#' : (char)std::toupper((unsigned char)m_games[index].sortName[0]);
				const char letter = std::isalpha((unsigned char)c) ? c : '#';
				if (letters.empty() || letters.back() != letter)
					if (std::find(letters.begin(), letters.end(), letter) == letters.end())
						letters.push_back(letter);
			}
			const char current = game >= 0 && !m_games[game].sortName.empty() && std::isalpha((unsigned char)m_games[game].sortName[0]) ?
				(char)std::toupper((unsigned char)m_games[game].sortName[0]) :
				'#';
			const float step = std::min(25.0f, 640.0f / letters.size());
			float y = 540 - letters.size() * step / 2;
			canvas.PushAlpha(Enter(3));
			for (char letter : letters)
			{
				const bool on = letter == current && game >= 0;
				if (on)
					canvas.Rect({1852 - 15, y - 2, 30, 30}, 15, Accent());
				canvas.Text(Style({16, ui::Weight::SemiBold, 1.0f}), 1852, y + 6, std::string(1, letter), on ? kInk1 : Tertiary(), 0, 0, ui::Align::Centre);
				y += step;
			}
			canvas.PopAlpha();
		}
	}
}
