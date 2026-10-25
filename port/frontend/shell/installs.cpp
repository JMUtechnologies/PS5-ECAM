// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: Updates and DLC, the Wii U's. What the drives plugged in, the
// PS5's /data and the games folder have to install is found by itself (ps5emu::StartFindingInstallables)
// and listed by game, each with what is installed now; Cross installs one, Square every new one, one
// after another, the page's head saying how far the whole has gone; Triangle the folder browser for
// anything elsewhere. The page holds the focus while an install runs, as the folder browser's do.

#include "shell_internal.h"
#include "../actions.h"
#include "../../app/emulator.h"
#include "../../app/paths.h"
#include "../../ps5/log.h"

#include <algorithm>
#include <set>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;
	using Kind = ps5emu::InstallCandidate::Kind;

	namespace
	{
		constexpr float kTop = 300, kBottom = 1000, kListX = 96, kListW = 1100;
		constexpr float kHeadH = 70, kRowH = 88;
		constexpr float kAsideX = 1250, kAsideW = 574;

		bool IsNew(const ps5emu::Installable& item)
		{
			return !item.packed && (item.installedVersion < 0 || item.version > item.installedVersion);
		}

		int KindOrder(Kind kind)
		{
			return kind == Kind::Update ? 0 : kind == Kind::Dlc ? 1 : 2;
		}

		std::string WhatItIs(const ps5emu::Installable& item)
		{
			// tr: an update found to install ({0}: its version, as 208)
			return item.kind == Kind::Update ? TrF("Update v{0}", item.version) : item.kind == Kind::Dlc ? std::string(Tr("DLC")) : std::string(Tr("The game"));
		}

		std::string Status(const ps5emu::Installable& item)
		{
			if (item.packed)
				return Tr("Runs from where it is: no install needed");
			if (item.installedVersion < 0)
				return Tr("Not installed");
			if (item.version > item.installedVersion)
				// tr: {0} is the version installed now, older than the one found
				return TrF("v{0} installed: this is newer", item.installedVersion);
			if (item.version < item.installedVersion)
				// tr: {0} is the version installed now, newer than the one found
				return TrF("v{0} installed: this is older", item.installedVersion);
			return Tr("Installed");
		}

		std::string DriveWords(const std::string& drive)
		{
			return drive.starts_with("/mnt/") ? DriveName(drive) : std::string(Tr("The PS5"));
		}
	}

	void Shell::UpdatesOpen(uint64_t game, ScreenId from)
	{
		m_updates.only = game;
		m_updates.from = from;
		m_updates.selected = 0;
		m_updates.scroll = 0;
		m_updates.message.clear();
		// where to look: every drive plugged in, the games folder, the app's folder and /data
		std::vector<ps5emu::FindRoot> roots;
		const std::vector<std::string> drives = ConnectedDrives();
		auto driveOf = [&](const std::string& folder) {
			for (const std::string& drive : drives)
				if (folder == drive || folder.starts_with(drive + "/"))
					return drive;
			return std::string("/data");
		};
		for (const std::string& drive : drives)
			roots.push_back({drive, drive, 4});
		if (!m_settings.gamesFolder.empty())
			roots.push_back({m_settings.gamesFolder, driveOf(m_settings.gamesFolder), 4});
		// One extra level for cemu/updates/<game>/<update> and cemu/dlc sources.
		roots.push_back({ps5paths::CemuRoot(), driveOf(ps5paths::CemuRoot()), 4});
		roots.push_back({ps5paths::Root(), driveOf(ps5paths::Root()), 4});
		roots.push_back({"/data", "/data", 2});
		ps5emu::StartFindingInstallables(roots);
		m_updates.looking = true;
		m_updates.found.clear();
		m_updates.rows.clear();
		ps5log::Line("[launcher] Updates and DLC: looking on {} drives, the games folder and /data", drives.size());
		Show(ScreenId::Updates);
		m_feedback.Play(ui::Cue::Select);
	}

	void Shell::UpdatesSort()
	{
		// what stays in focus: the same folder
		const std::string focused = m_updates.selected < (int)m_updates.rows.size() ? m_updates.found[m_updates.rows[m_updates.selected]].folder : "";
		auto gameName = [&](const ps5emu::Installable& item) {
			const int game = FindGame(item.baseTitleId);
			if (game >= 0)
				return Lower(m_games[game].entry.game.name);
			return item.name.empty() ? Hex(item.baseTitleId) : Lower(item.name);
		};
		m_updates.rows.clear();
		for (int i = 0; i < (int)m_updates.found.size(); i++)
			if (!m_updates.only || m_updates.found[i].baseTitleId == m_updates.only)
				m_updates.rows.push_back(i);
		std::stable_sort(m_updates.rows.begin(), m_updates.rows.end(), [&](int a, int b) {
			const auto& x = m_updates.found[a];
			const auto& y = m_updates.found[b];
			if (x.baseTitleId != y.baseTitleId)
				return gameName(x) < gameName(y);
			if (KindOrder(x.kind) != KindOrder(y.kind))
				return KindOrder(x.kind) < KindOrder(y.kind);
			if (x.version != y.version)
				return x.version > y.version; // the newest first
			return x.folder < y.folder;
		});
		m_updates.selected = 0;
		for (int i = 0; i < (int)m_updates.rows.size(); i++)
			if (m_updates.found[m_updates.rows[i]].folder == focused)
				m_updates.selected = i;
	}

	void Shell::UpdatesInstallNext()
	{
		while (!m_updates.queue.empty())
		{
			std::string error;
			if (ps5emu::StartInstall(m_updates.queue.front(), error))
			{
				m_updates.installing = true;
				m_updates.lastCopied = 0;
				m_updates.lastAt = m_now;
				return;
			}
			// this one cannot (already where it goes, gone from the drive): the next
			ps5log::Line("[launcher] {} not installed: {}", m_updates.queue.front(), error);
			m_updates.message = error;
			m_updates.queue.erase(m_updates.queue.begin());
			m_updates.finished++;
		}
		m_updates.installing = false;
	}

	void Shell::PollUpdates()
	{
		if (m_updates.looking)
		{
			std::string where;
			const bool looking = ps5emu::FindingInstallables(&where);
			m_updates.where = where;
			// the list as it grows, a few times a second
			if (!looking || m_frames % 15 == 0)
			{
				m_updates.found = ps5emu::FoundInstallables();
				UpdatesSort();
			}
			m_updates.looking = looking;
		}
		if (!m_updates.installing)
			return;
		const auto status = ps5emu::GetInstallStatus();
		using State = ps5emu::InstallStatus::State;
		if (status.state == State::Running)
		{
			// the speed over the last seconds, smoothed
			if (m_now - m_updates.lastAt >= 1.0)
			{
				const float now = (float)((status.copied - std::min(status.copied, m_updates.lastCopied)) / (m_now - m_updates.lastAt));
				m_updates.speed = m_updates.speed > 0 ? m_updates.speed * 0.6f + now * 0.4f : now;
				m_updates.lastCopied = status.copied;
				m_updates.lastAt = m_now;
			}
			return;
		}
		const std::string folder = m_updates.queue.empty() ? "" : m_updates.queue.front();
		if (!m_updates.queue.empty())
			m_updates.queue.erase(m_updates.queue.begin());
		m_updates.finished++;
		m_updates.runDone += status.total;
		if (status.state == State::Done)
			m_updates.message.clear();
		else if (status.state == State::Cancelled)
		{
			m_updates.queue.clear();
			m_updates.message = Tr("Stopped. What was installed before is back as it was.");
		}
		else
		{
			m_updates.queue.clear();
			m_updates.message = TrF("It could not be installed: {0}", status.message);
		}
		if (!m_updates.queue.empty() && !m_updates.stopping)
		{
			UpdatesInstallNext();
			return;
		}
		// the run is over: what is installed now, and the library's facts (Update v208, 1 DLC)
		m_updates.installing = m_updates.stopping = false;
		m_updates.queue.clear();
		ps5emu::RefreshInstalledVersions(m_updates.found);
		LookForGames();
		if (status.state == State::Done)
		{
			Toast(m_updates.finished > 1 ? TrP(m_updates.finished, "{0} installed", "{0} installed") : std::string(Tr("Installed")));
			m_feedback.Play(ui::Cue::Select);
		}
		else
			m_feedback.Play(ui::Cue::Denied);
	}

	void Shell::UpdatesUpdate(const ui::Press& press)
	{
		const Button b = press.button;
		const int count = (int)m_updates.rows.size();
		if (m_updates.installing)
		{
			if (b == Button::Circle && !m_updates.stopping)
			{
				// the one installing stops and what it replaced comes back; the rest are not started
				m_updates.stopping = true;
				m_updates.queue.resize(std::min<size_t>(1, m_updates.queue.size()));
				ps5emu::CancelInstall();
				m_feedback.Play(ui::Cue::Back);
			}
			return;
		}
		switch (b)
		{
		case Button::Up:
		case Button::Down:
		case Button::L2:
		case Button::R2:
		{
			const int step = b == Button::Up ? -1 : b == Button::Down ? 1 : b == Button::L2 ? -6 : 6;
			const int next = std::clamp(m_updates.selected + step, 0, std::max(0, count - 1));
			if (next == m_updates.selected)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_updates.selected = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			return;
		}
		case Button::Cross:
		{
			if (!count || m_updates.found[m_updates.rows[m_updates.selected]].packed)
			{
				m_feedback.Play(ui::Cue::Denied);
				return;
			}
			const ps5emu::Installable& item = m_updates.found[m_updates.rows[m_updates.selected]];
			m_updates.queue = {item.folder};
			m_updates.queued = 1;
			m_updates.finished = 0;
			m_updates.runBytes = item.bytes;
			m_updates.runDone = 0;
			m_updates.speed = 0;
			m_updates.message.clear();
			UpdatesInstallNext();
			m_feedback.Play(m_updates.installing ? ui::Cue::Select : ui::Cue::Denied);
			return;
		}
		case Button::Square:
		{
			// every new one, in the list's order
			m_updates.queue.clear();
			m_updates.runBytes = 0;
			for (const int row : m_updates.rows)
				if (IsNew(m_updates.found[row]))
				{
					m_updates.queue.push_back(m_updates.found[row].folder);
					m_updates.runBytes += m_updates.found[row].bytes;
				}
			if (m_updates.queue.empty())
			{
				m_feedback.Play(ui::Cue::Denied);
				Toast(Tr("Everything found is installed already"));
				return;
			}
			m_updates.queued = (int)m_updates.queue.size();
			m_updates.finished = 0;
			m_updates.runDone = 0;
			m_updates.speed = 0;
			m_updates.message.clear();
			UpdatesInstallNext();
			m_feedback.Play(m_updates.installing ? ui::Cue::Select : ui::Cue::Denied);
			return;
		}
		case Button::Triangle:
			// anything elsewhere: the folder browser, as before
			ps5emu::StopFindingInstallables();
			m_updates.looking = false;
			m_filesSide = System::WiiU;
			m_pageFrom = ScreenId::Updates;
			FilesOpen(1);
			return;
		case Button::Circle:
			ps5emu::StopFindingInstallables();
			m_updates.looking = false;
			ClosePage(m_updates.from);
			if (m_updates.from == ScreenId::Settings)
				SettingsOpen("files", true);
			return;
		default: return;
		}
	}

	std::vector<Hint> Shell::UpdatesHints() const
	{
		if (m_updates.installing)
			return {{Icon::Circle, m_updates.stopping ? Tr("Stopping…") : Tr("Stop")}};
		int fresh = 0;
		for (const int row : m_updates.rows)
			fresh += IsNew(m_updates.found[row]);
		std::vector<Hint> hints;
		if (!m_updates.rows.empty())
			hints.push_back({Icon::Cross, Tr("Install")});
		if (fresh)
			// tr: Square on Updates and DLC: install every one not installed yet, or newer ({0}: how many)
			hints.push_back({Icon::Square, TrF("Install every new one ({0})", fresh)});
		hints.push_back({Icon::Triangle, Tr("Choose a folder")});
		hints.push_back({Icon::Circle, Tr("Back")});
		return hints;
	}

	void Shell::UpdatesDraw(Canvas& canvas)
	{
		const int count = (int)m_updates.rows.size();
		m_updates.selected = std::clamp(m_updates.selected, 0, std::max(0, count - 1));
		// the places found on, and how many are new
		std::vector<std::string> places;
		int fresh = 0;
		for (const int row : m_updates.rows)
		{
			const ps5emu::Installable& item = m_updates.found[row];
			const std::string place = DriveWords(item.drive);
			if (std::find(places.begin(), places.end(), place) == places.end())
				places.push_back(place);
			fresh += IsNew(item);
		}
		std::string summary;
		if (m_updates.looking)
			summary = TrF("Looking in {0}…", ShortPath(m_updates.where, 60));
		else if (!count)
			summary = Tr("Nothing to install was found on the drives plugged in.");
		else
		{
			// tr: where updates and DLC were found ({0}: "USB drive 1, The PS5"), then how many are new
			summary = TrF("Found on {0}.", ps5lang::List(places)) + " " +
				(fresh ? TrP(fresh, "{0} new to install.", "{0} new to install.") : std::string(Tr("Everything found is installed.")));
		}
		const int only = m_updates.only ? FindGame(m_updates.only) : -1;
		DrawPageHeader(canvas, only >= 0 ? std::string("Wii U · ") + m_games[only].entry.game.name : std::string("Wii U · ") + Tr("Game files"),
			Tr("Updates and DLC"), summary);

		// the whole run, at the top right, while it runs
		if (m_updates.installing)
		{
			const auto status = ps5emu::GetInstallStatus();
			const uint64_t whole = std::max(m_updates.runBytes, m_updates.runDone + status.total);
			const uint64_t done = m_updates.runDone + status.copied;
			const Box run{1204, 92, 620, 136};
			canvas.Rect(run, kRadiusCard, Surface());
			canvas.Ring(run, kRadiusCard, 1.5f, kGlassEdge);
			canvas.Draw(Icon::Download, {run.x + 26, run.y + 22, 30, 30}, Accent());
			// tr: Updates and DLC, installing: which of how many ({0} of {1})
			canvas.Text(Style({24, ui::Weight::SemiBold, 1.0f}), run.x + 68, run.y + 24, TrF("Installing {0} of {1}", m_updates.finished + 1, m_updates.queued),
				kText, run.w - 96, 1);
			std::string facts = TrF("{0} of {1}", Gigabytes(done), Gigabytes(whole));
			if (m_updates.speed > 1e6f)
			{
				// tr: how fast an install copies, in megabytes a second
				facts += " · " + TrF("{0} MB/s", (int)(m_updates.speed / 1e6f));
				const int seconds = (int)((whole - std::min(whole, done)) / m_updates.speed);
				// tr: an install's time left, in seconds or minutes
				facts += " · " + (seconds < 90 ? TrP(seconds, "about {0} second left", "about {0} seconds left") :
												  TrP(seconds / 60, "about {0} minute left", "about {0} minutes left"));
			}
			ui::TextStyle small = Style({20, ui::Weight::Regular, 1.0f});
			small.tabular = true;
			canvas.Text(small, run.x + 28, run.y + 64, facts, Secondary(), run.w - 56, 1);
			const Box bar{run.x + 28, run.y + 104, run.w - 56, 10};
			canvas.Rect(bar, 5, Wash(0.14f));
			canvas.Rect({bar.x, bar.y, bar.w * (whole ? std::clamp((float)done / whole, 0.0f, 1.0f) : 0.0f), bar.h}, 5, Accent());
		}

		canvas.PushAlpha(Enter(1));
		if (!count && !m_updates.looking)
		{
			// none found: where it looked, and what it looks for
			const Box card{kListX, kTop, kListW, 300};
			Panel(canvas, card, kRadiusSheet);
			canvas.Text(Style(kHeadingStyle), card.x + 40, card.y + 36, Tr("Nothing found to install"), kText, card.w - 80, 1);
			canvas.Text(Style({22, ui::Weight::Regular, 1.45f}), card.x + 40, card.y + 96,
				Tr("Every USB drive and extended storage plugged in, the games folder and /data were searched, four folders deep, for "
				   "updates, DLC and games as folders with code, content and meta (the format Cemu installs). Put them on a USB drive, or "
				   "press Triangle to choose a folder anywhere."),
				Secondary(), card.w - 80, 6);
		}
		// the list: a heading for each game, its rows under it
		struct Line
		{
			int row; // -1: a game's heading
			int item;
			float y, h;
		};
		std::vector<Line> lines;
		float y = 0;
		uint64_t group = 0;
		for (int i = 0; i < count; i++)
		{
			const int item = m_updates.rows[i];
			if (i == 0 || m_updates.found[item].baseTitleId != group)
			{
				group = m_updates.found[item].baseTitleId;
				lines.push_back({-1, item, y, kHeadH});
				y += kHeadH;
			}
			lines.push_back({i, item, y, kRowH});
			y += kRowH;
		}
		float focusY = 0;
		for (const Line& line : lines)
			if (line.row == m_updates.selected)
				focusY = line.y;
		const float view = kBottom - kTop;
		const float target = std::clamp(focusY - view * 0.4f, 0.0f, std::max(0.0f, y - view));
		m_updates.scroll += (target - m_updates.scroll) * std::min(1.0f, m_dt * (m_settings.ui.reduceMotion ? 60.0f : 14.0f));
		canvas.PushClip({kListX - 16, kTop - 12, kListW + 32, view + 24});
		const auto status = ps5emu::GetInstallStatus();
		for (const Line& line : lines)
		{
			const float top = kTop + line.y - m_updates.scroll;
			if (top > kBottom + 10 || top + line.h < kTop - 10)
				continue;
			const ps5emu::Installable& item = m_updates.found[line.item];
			if (line.row < 0)
			{
				// the game: its cover, its name, what is installed of it
				const int game = FindGame(item.baseTitleId);
				const Box cover{kListX, top + 8, 40, 54};
				if (game >= 0)
					DrawCover(canvas, m_games[game], cover, false, 0);
				else
					canvas.Rect(cover, 6, Wash(0.1f));
				const std::string name = game >= 0 ? m_games[game].entry.game.name : !item.name.empty() ? item.name : Hex(item.baseTitleId);
				canvas.Text(Style({26, ui::Weight::SemiBold, 1.0f}), kListX + 60, top + 22, name, kText, kListW - 340, 1);
				if (game >= 0 && m_games[game].entry.game.hasUpdate)
					canvas.Text(Style({19, ui::Weight::Regular, 1.0f}), kListX + kListW - 260, top + 26, TrF("v{0} installed", m_games[game].entry.game.version),
						Tertiary(), 260, 1, ui::Align::Right);
				continue;
			}
			const bool focused = line.row == m_updates.selected;
			const bool running = m_updates.installing && !m_updates.queue.empty() && m_updates.queue.front() == item.folder;
			const int waiting = (int)(std::find(m_updates.queue.begin(), m_updates.queue.end(), item.folder) - m_updates.queue.begin());
			const Box box{kListX + 64, top, kListW - 64, kRowH - 10};
			canvas.PushAlpha(IsNew(item) || running || focused ? 1.0f : 0.55f);
			canvas.Rect(box, kRadiusCard, focused ? Surface2() : Surface());
			if (focused)
				Focus(box, kRadiusCard);
			canvas.Text(Style({24, ui::Weight::SemiBold, 1.0f}), box.x + 26, box.CentreY() - 15, WhatItIs(item), kText, 200, 1);
			ui::TextStyle facts = Style({20, ui::Weight::Regular, 1.0f});
			facts.tabular = true;
			canvas.Text(facts, box.x + 236, box.CentreY() - 12, item.bytes ? Gigabytes(item.bytes) : std::string("…"), Secondary(), 110, 1);
			canvas.Draw(item.drive.starts_with("/mnt/usb") ? Icon::Usb : Icon::Folder, {box.x + 356, box.CentreY() - 13, 26, 26}, Secondary());
			canvas.Text(facts, box.x + 392, box.CentreY() - 12, DriveWords(item.drive), Secondary(), 230, 1);
			std::string state = Status(item);
			if (running)
				// tr: a row of Updates and DLC while it installs ({0}: a percentage)
				state = TrF("Installing · {0}", ps5lang::Percent(status.total ? (int)(status.copied * 100 / status.total) : 0));
			else if (waiting > 0 && waiting < (int)m_updates.queue.size())
				state = Tr("Waiting") + std::string(" · ") + state;
			canvas.Text(Style({20, ui::Weight::Medium, 1.0f}), box.Right() - 26 - (box.w - 660), box.CentreY() - 12, state,
				IsNew(item) || running ? Accent() : Secondary(), box.w - 660, 1, ui::Align::Right);
			if (running)
			{
				const Box bar{box.x + 26, box.Bottom() - 14, box.w - 52, 5};
				canvas.Rect(bar, 2.5f, Wash(0.12f));
				canvas.Rect({bar.x, bar.y, bar.w * (status.total ? std::clamp((float)status.copied / status.total, 0.0f, 1.0f) : 0.0f), bar.h}, 2.5f, Accent());
			}
			canvas.PopAlpha();
		}
		canvas.PopClip();
		canvas.PopAlpha();

		// the row in focus, in full, on the right
		if (!count)
			return;
		const ps5emu::Installable& item = m_updates.found[m_updates.rows[m_updates.selected]];
		canvas.PushAlpha(Enter(2));
		const Box aside{kAsideX, kTop, kAsideW, 560};
		Panel(canvas, aside, kRadiusSheet);
		float ay = aside.y + 36;
		canvas.Text(Style(kOverlineStyle), aside.x + 36, ay, item.packed ? Tr("Runs from where it is") : Tr("To install"), Tertiary(), aside.w - 72, 1);
		ay += 34;
		const int game = FindGame(item.baseTitleId);
		const std::string title = (game >= 0 ? m_games[game].entry.game.name : !item.name.empty() ? item.name : Hex(item.baseTitleId)) + "\n" + WhatItIs(item);
		const ui::TextBlock heading = canvas.Text(Style(kHeadingStyle), aside.x + 36, ay, title, kText, aside.w - 72, 3);
		ay += heading.height + 22;
		const std::pair<std::string, std::string> facts[] = {
			{Tr("Version"), item.installedVersion < 0 ? TrF("v{0}, not installed yet", item.version) : TrF("v{0}, v{1} installed now", item.version, item.installedVersion)},
			{Tr("Title ID"), Hex(item.titleId)},
			{Tr("Where it is"), DriveWords(item.drive) + "  ·  " + ShortPath(item.folder, 40)},
		};
		for (const auto& [label, value] : facts)
		{
			canvas.Draw(Icon::Check, {aside.x + 36, ay + 2, 24, 24}, kGood);
			canvas.Text(Style(kCaptionStyle), aside.x + 74, ay, label, Tertiary(), aside.w - 110, 1);
			canvas.Text(Style({22, ui::Weight::Medium, 1.2f}), aside.x + 74, ay + 26, value, kText, aside.w - 110, 1);
			ay += 76;
		}
		canvas.Rect({aside.x + 36, ay, aside.w - 72, 1.5f}, 0, kLine);
		ay += 20;
		const std::string note = !m_updates.message.empty() ? m_updates.message :
			item.packed ? std::string(Tr("Cemu runs it straight from the game files folder (title.tmd and .app files): put it there and the library lists it with its game.")) :
						  std::string(Tr("It is installed into the Wii U's storage (mlc01). What is installed now is set aside until it finishes, and put back if you stop."));
		canvas.Text(Style({20, ui::Weight::Regular, 1.5f}), aside.x + 36, ay, note, Secondary(), aside.w - 72, 6);
		canvas.PopAlpha();
	}
}
