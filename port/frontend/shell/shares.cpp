// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Network shares, Settings > Game files' page for the SMB and NFS
// shares app/netfs.cpp reads games from. The page lists them; Cross opens a share's sheet (or a new one's):
// the computers on the PS5's network that share files, looked for as it opens, the type, the computer, the
// shared folder (those the computer lists, Left and Right), a folder inside, the sign-in, then Test, Save
// and Remove (held). A test, the search for a computer's shared folders and the look around the network
// each run on a thread of their own; the sheet looks for their answers each frame. Saved shares are
// /data/ps5cemu/network-shares.txt, and each is a tab of the folder browser (pages.cpp).

#include "shell_internal.h"
#include "../actions.h"
#include "../../app/paths.h"
#include "../../ps5/log.h"

#include <algorithm>
#include <mutex>
#include <thread>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;
	using Problem = ps5netfs::Problem;

	// A test, a search for shared folders or a look around the network, on its own thread: what it found,
	// once done. A sheet closed meanwhile lets it finish into a job nobody reads.
	struct ShareJob
	{
		std::mutex m;
		bool done = false;
		Problem problem = Problem::None;
		std::string detail;
		std::vector<std::string> names;
		std::vector<ps5netfs::Found> computers;
	};

	namespace
	{
		constexpr float kListX = 96, kListW = 760, kTop = 330, kRowH = 104;
		constexpr float kSheetX = 900, kSheetW = 924, kSheetY = 96, kSheetH = 888;
		constexpr float kFieldH = 58, kFieldGap = 6;

		enum Field
		{
			kNetwork,
			kType,
			kComputer,
			kFolder,
			kInside,
			kUser,
			kPassword,
			kTest,
			kSave,
			kRemove,
		};

		std::string SharesFile()
		{
			return ps5paths::Root() + "/network-shares.txt";
		}

		bool Nfs(const ps5netfs::Share& share)
		{
			return share.protocol == "nfs";
		}

		// What went wrong reaching a share, in the player's words
		std::string ProblemWords(Problem problem, bool nfs, const std::string& detail)
		{
			switch (problem)
			{
			case Problem::None: return {};
			case Problem::NoComputer: return Tr("Enter the computer's address or name first.");
			case Problem::NoShare:
				return nfs ? Tr("Enter the export's path, like /volume1/games.") : Tr("Enter the shared folder's name, or choose one the computer lists.");
			case Problem::UnknownName: return Tr("No computer answers to that name. Try its address, like 192.168.1.20.");
			case Problem::Unreachable: return Tr("Nothing answered. Is the computer on, with file sharing turned on?");
			case Problem::SignIn: return Tr("The computer did not accept the user name or password.");
			case Problem::NoSuchShare: return Tr("The computer shares no folder by that name.");
			case Problem::Denied:
				return nfs ? Tr("The computer did not let the PS5 in: allow the PS5's address in the export's settings.") :
							 Tr("The computer did not let the PS5 in: check the folder's sharing permissions, or sign in.");
			case Problem::NoFolder: return Tr("The folder inside is not there.");
			case Problem::NoExports: return Tr("The computer did not list its exports. Type the export's path, like /volume1/games.");
			case Problem::Other: return detail.empty() ? std::string(Tr("It did not work.")) : detail;
			}
			return {};
		}

		// "192.168.1.20 / Games", or the NFS export
		std::string Where(const ps5netfs::Share& share)
		{
			std::string where = share.host;
			if (!share.share.empty())
				where += (Nfs(share) ? ":" : " / ") + share.share;
			if (!share.folder.empty())
				where += " / " + share.folder;
			return where;
		}

		std::shared_ptr<ShareJob> StartJob(std::function<void(ShareJob&)> work)
		{
			auto job = std::make_shared<ShareJob>();
			std::thread([job, work = std::move(work)] {
				ShareJob result;
				work(result);
				std::lock_guard lock(job->m);
				job->problem = result.problem;
				job->detail = std::move(result.detail);
				job->names = std::move(result.names);
				job->computers = std::move(result.computers);
				job->done = true;
			}).detach();
			return job;
		}

		bool Done(const std::shared_ptr<ShareJob>& job)
		{
			if (!job)
				return false;
			std::lock_guard lock(job->m);
			return job->done;
		}
	}

	void Shell::SharesOpen(ScreenId from)
	{
		m_shares.from = from;
		m_shares.list = ps5netfs::shares();
		m_shares.status.assign(m_shares.list.size(), std::string());
		m_shares.selected = 0;
		m_shares.editing = false;
		Show(ScreenId::Shares);
		m_feedback.Play(ui::Cue::Select);
	}

	void Shell::SharesEdit(int index)
	{
		m_shares.editing = true;
		m_shares.index = index;
		m_shares.share = index >= 0 && index < (int)m_shares.list.size() ? m_shares.list[index] : ps5netfs::Share{};
		m_shares.row = index >= 0 ? kTest : kNetwork;
		m_shares.message.clear();
		m_shares.messageGood = false;
		m_shares.saving = false;
		m_shares.names.clear();
		m_shares.nameIndex = 0;
		m_shares.test = m_shares.find = nullptr;
		m_holdProgress = 0;
		SharesScan();
		if (!m_shares.share.host.empty())
			SharesFind();
	}

	void Shell::SharesScan()
	{
		m_shares.found.clear();
		m_shares.foundIndex = 0;
		m_shares.scan = StartJob([](ShareJob& job) { job.computers = ps5netfs::discover(); });
	}

	void Shell::SharesFind()
	{
		// the shared folders the computer lists (SMB), or its exports (NFS)
		m_shares.names.clear();
		m_shares.nameIndex = 0;
		if (m_shares.share.host.empty())
			return;
		const ps5netfs::Share server = m_shares.share;
		m_shares.find = StartJob([server](ShareJob& job) { job.problem = ps5netfs::enumerate(server, job.names, &job.detail); });
	}

	void Shell::SharesPoll()
	{
		if (Done(m_shares.scan))
		{
			std::lock_guard lock(m_shares.scan->m);
			m_shares.found = m_shares.scan->computers;
			m_shares.scan = nullptr;
		}
		if (Done(m_shares.find))
		{
			std::lock_guard lock(m_shares.find->m);
			m_shares.names = m_shares.find->names;
			// the folder already chosen, in its place among them
			const auto it = std::find(m_shares.names.begin(), m_shares.names.end(), m_shares.share.share);
			m_shares.nameIndex = it != m_shares.names.end() ? (int)(it - m_shares.names.begin()) : 0;
			if (m_shares.share.share.empty() && !m_shares.names.empty())
				m_shares.share.share = m_shares.names.front();
			m_shares.find = nullptr;
		}
		if (Done(m_shares.test))
		{
			Problem problem;
			std::string detail;
			{
				std::lock_guard lock(m_shares.test->m);
				problem = m_shares.test->problem;
				detail = m_shares.test->detail;
			}
			m_shares.test = nullptr;
			const bool good = problem == Problem::None;
			m_shares.messageGood = good;
			m_shares.message = good ? std::string(Tr("Connected: the PS5 can read this folder.")) : ProblemWords(problem, Nfs(m_shares.share), detail);
			ps5log::Line("[netfs] test of {}: {}", Where(m_shares.share), good ? "connected" : m_shares.message);
			if (m_shares.saving)
			{
				m_shares.saving = false;
				if (good)
					SharesSave();
				else
					m_feedback.Play(ui::Cue::Denied);
			}
			else
				m_feedback.Play(good ? ui::Cue::Select : ui::Cue::Denied);
		}
	}

	void Shell::SharesSave()
	{
		ps5netfs::Share share = m_shares.share;
		std::vector<ps5netfs::Share> others = m_shares.list;
		if (m_shares.index >= 0 && m_shares.index < (int)others.size())
			others.erase(others.begin() + m_shares.index);
		if (share.name.empty())
			share.name = ps5netfs::unique_name(share, others);
		std::vector<ps5netfs::Share> list = m_shares.list;
		if (m_shares.index >= 0 && m_shares.index < (int)list.size())
			list[m_shares.index] = share;
		else
		{
			list.push_back(share);
			m_shares.index = (int)list.size() - 1;
		}
		if (!ps5netfs::save(SharesFile(), list))
		{
			m_shares.message = TrF("It could not be saved in {0}.", SharesFile());
			m_shares.messageGood = false;
			m_feedback.Play(ui::Cue::Denied);
			return;
		}
		ps5netfs::set_shares(list);
		m_shares.list = list;
		m_shares.status.resize(list.size());
		m_shares.status[m_shares.index] = Tr("Connected");
		m_shares.selected = m_shares.index;
		m_shares.editing = false;
		ps5log::Line("[netfs] saved {} ({}), {} shares", share.name, Where(share), list.size());
		// tr: a network share saved ({0}: its name); it is now a tab of the folder browser
		Toast(TrF("{0} is saved: choose a games folder on it in Game files", share.name));
		m_feedback.Play(ui::Cue::Select);
	}

	void Shell::SharesRemove()
	{
		if (m_shares.index < 0 || m_shares.index >= (int)m_shares.list.size())
			return;
		std::vector<ps5netfs::Share> list = m_shares.list;
		const std::string name = list[m_shares.index].name;
		list.erase(list.begin() + m_shares.index);
		ps5netfs::save(SharesFile(), list);
		ps5netfs::set_shares(list);
		m_shares.list = list;
		m_shares.status.assign(list.size(), std::string());
		m_shares.selected = std::min(m_shares.selected, (int)list.size());
		m_shares.editing = false;
		ps5log::Line("[netfs] removed {}", name);
		Toast(TrF("{0} is removed", name));
	}

	std::vector<Row> Shell::ShareRows()
	{
		using Kind = Row::Kind;
		const ps5netfs::Share& share = m_shares.share;
		const bool nfs = Nfs(share);
		std::vector<Row> rows(kRemove + 1);
		auto set = [&](int field, Kind kind, const char* id, const std::string& label, const std::string& value, const std::string& description) {
			Row& row = rows[field];
			row.kind = kind;
			row.id = id;
			row.label = label;
			row.value = value;
			row.description = description;
			return &row;
		};
		// the computers found on the network
		std::vector<std::string> computers;
		for (const ps5netfs::Found& found : m_shares.found)
			computers.push_back((found.name.empty() ? found.address : found.name + " · " + found.address) +
				(found.smb && found.nfs ? " · SMB, NFS" : found.nfs ? " · NFS" : " · SMB"));
		Row* network = set(kNetwork, computers.empty() ? Kind::Action : Kind::Choice, "network", Tr("On your network"),
			m_shares.scan ? std::string(Tr("Looking…")) : computers.empty() ? std::string(Tr("None found: Triangle looks again")) : std::string(),
			computers.empty() ? Tr("Computers on the PS5's network that share files.") : Tr("Left and Right choose one; Cross takes it."));
		if (!computers.empty())
		{
			network->options = computers;
			network->index = std::clamp(m_shares.foundIndex, 0, (int)computers.size() - 1);
			network->value = computers[network->index];
		}
		Row* type = set(kType, Kind::Segmented, "type", Tr("Type"), {}, Tr("SMB: Windows, macOS, Linux or most NAS. NFS: a NAS or Linux."));
		type->options = {"SMB", "NFS"};
		type->index = nfs ? 1 : 0;
		set(kComputer, Kind::Link, "computer", Tr("Computer"), share.host, Tr("Its address (192.168.1.20) or its name."));
		Row* folder = set(kFolder, m_shares.names.empty() ? Kind::Link : Kind::Choice, "folder", nfs ? Tr("Export") : Tr("Shared folder"), share.share,
			m_shares.find ? std::string(Tr("Looking for its shared folders…")) :
			m_shares.names.empty() ? std::string(nfs ? Tr("Its path on the computer, like /volume1/games.") : Tr("The shared folder's name, like Games.")) :
									 TrP((int)m_shares.names.size(), "The computer shares {0} folder. Left and Right choose; Cross types another.",
										 "The computer shares {0} folders. Left and Right choose; Cross types another."));
		if (!m_shares.names.empty())
		{
			folder->options = m_shares.names;
			folder->index = std::clamp(m_shares.nameIndex, 0, (int)m_shares.names.size() - 1);
		}
		set(kInside, Kind::Link, "inside", Tr("Folder inside"), share.folder, Tr("Optional: a folder in the share to start from."));
		Row* user = set(kUser, Kind::Link, "user", Tr("User name"), share.user.empty() ? std::string(Tr("Guest")) : share.user,
			Tr("Empty signs in as a guest."));
		Row* password = set(kPassword, Kind::Link, "password", Tr("Password"), std::string(std::min<size_t>(share.password.size(), 12), '*'),
			Tr("Kept on the PS5 with the share."));
		if (nfs)
		{
			user->dimmed = password->dimmed = true;
			user->description = password->description = Tr("NFS has no sign-in: the export decides by address.");
		}
		set(kTest, Kind::Action, "test", Tr("Test"), m_shares.test ? std::string(Tr("Connecting…")) : std::string(),
			Tr("Connects and reads the folder, nothing more."));
		set(kSave, Kind::Action, "save", Tr("Save"), m_shares.saving ? std::string(Tr("Connecting…")) : std::string(),
			Tr("Tested first; then a tab of the folder browser."));
		Row* remove = set(kRemove, Kind::Hold, "remove", Tr("Remove"), Tr("Hold Cross"), Tr("Forgets the share. Nothing on it changes."));
		if (m_shares.index < 0)
			remove->dimmed = true;
		return rows;
	}

	void Shell::SharesUpdate(const ui::Press& press, const ui::Actions& actions)
	{
		SharesPoll();
		const Button b = press.button;
		if (!m_shares.editing)
		{
			const int count = (int)m_shares.list.size() + 1; // the shares, then Add a share
			switch (b)
			{
			case Button::Up:
			case Button::Down:
			{
				const int next = std::clamp(m_shares.selected + (b == Button::Down ? 1 : -1), 0, count - 1);
				m_feedback.Play(next == m_shares.selected ? ui::Cue::Edge : ui::Cue::Focus, press.repeat);
				m_shares.selected = next;
				break;
			}
			case Button::Cross:
				m_feedback.Play(ui::Cue::Sheet);
				SharesEdit(m_shares.selected < (int)m_shares.list.size() ? m_shares.selected : -1);
				break;
			case Button::Circle:
				ClosePage(m_shares.from);
				if (m_shares.from == ScreenId::Settings)
					SettingsOpen("files", true);
				break;
			default: break;
			}
			return;
		}

		// the sheet
		const std::vector<Row> rows = ShareRows();
		ps5netfs::Share& share = m_shares.share;
		const bool nfs = Nfs(share);
		const bool busy = m_shares.test != nullptr;
		// Remove, held to confirm
		if (m_shares.row == kRemove && m_shares.index >= 0)
		{
			const float seconds = m_settings.ui.holdMs / 1000.0f;
			m_holdProgress = actions.Held(Button::Cross) ? actions.HeldFor(Button::Cross) / seconds : 0.0f;
			if (m_holdProgress >= 1 && !m_holdFired)
			{
				m_holdFired = true;
				m_feedback.Play(ui::Cue::Hold);
				SharesRemove();
				return;
			}
			if (!actions.Held(Button::Cross))
				m_holdFired = false;
		}
		else
			m_holdProgress = 0;
		auto skip = [&](int row) { return nfs && (row == kUser || row == kPassword); };
		switch (b)
		{
		case Button::Up:
		case Button::Down:
		{
			int next = m_shares.row;
			do
				next += b == Button::Down ? 1 : -1;
			while (next >= 0 && next <= kRemove && skip(next));
			if (next < 0 || next > kRemove)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_shares.row = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Left:
		case Button::Right:
		{
			const int step = b == Button::Right ? 1 : -1;
			if (m_shares.row == kNetwork && !m_shares.found.empty())
			{
				m_shares.foundIndex = (m_shares.foundIndex + step + (int)m_shares.found.size()) % (int)m_shares.found.size();
				m_feedback.Play(ui::Cue::Focus);
			}
			else if (m_shares.row == kType)
			{
				share.protocol = nfs ? "" : "nfs";
				share.share.clear();
				SharesFind();
				m_feedback.Play(ui::Cue::Toggle);
			}
			else if (m_shares.row == kFolder && !m_shares.names.empty())
			{
				m_shares.nameIndex = (m_shares.nameIndex + step + (int)m_shares.names.size()) % (int)m_shares.names.size();
				share.share = m_shares.names[m_shares.nameIndex];
				m_feedback.Play(ui::Cue::Focus);
			}
			else
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			break;
		}
		case Button::Triangle:
			if (!m_shares.scan)
			{
				SharesScan(); // look around the network again
				m_feedback.Play(ui::Cue::Select);
			}
			break;
		case Button::Cross:
		{
			if (busy || rows[m_shares.row].dimmed)
			{
				m_feedback.Play(ui::Cue::Denied);
				break;
			}
			m_feedback.Play(ui::Cue::Select);
			switch (m_shares.row)
			{
			case kNetwork:
				if (!m_shares.found.empty())
				{
					// that computer: its address, the type it answers to, then its shared folders
					const ps5netfs::Found& found = m_shares.found[std::clamp(m_shares.foundIndex, 0, (int)m_shares.found.size() - 1)];
					share.host = found.address;
					share.protocol = found.smb ? "" : "nfs";
					share.share.clear();
					m_shares.row = kFolder;
					SharesFind();
				}
				else if (!m_shares.scan)
					SharesScan();
				break;
			case kType:
				share.protocol = nfs ? "" : "nfs";
				share.share.clear();
				SharesFind();
				break;
			case kComputer:
				OpenTextKeyboard(Tr("The computer's address or name"), share.host, 64, false, [this](const std::string& text) {
					m_shares.share.host = text;
					m_shares.share.share.clear();
					SharesFind();
				});
				break;
			case kFolder:
				OpenTextKeyboard(nfs ? Tr("The export's path") : Tr("The shared folder's name"), share.share, 128, false,
					[this](const std::string& text) { m_shares.share.share = text; });
				break;
			case kInside:
				OpenTextKeyboard(Tr("A folder inside the share"), share.folder, 128, false, [this](const std::string& text) { m_shares.share.folder = text; });
				break;
			case kUser:
				OpenTextKeyboard(Tr("User name"), share.user, 64, false, [this](const std::string& text) {
					m_shares.share.user = text;
					SharesFind(); // signed in, it may list more
				});
				break;
			case kPassword:
				OpenTextKeyboard(Tr("Password"), "", 128, true, [this](const std::string& text) {
					m_shares.share.password = text;
					SharesFind();
				});
				break;
			case kTest:
			case kSave:
			{
				m_shares.saving = m_shares.row == kSave;
				m_shares.message = Tr("Connecting…");
				m_shares.messageGood = false;
				const ps5netfs::Share tested = share;
				m_shares.test = StartJob([tested](ShareJob& job) { job.problem = ps5netfs::test(tested, &job.detail); });
				break;
			}
			default: break;
			}
			break;
		}
		case Button::Circle:
			// the sheet goes, what it changed with it
			m_shares.editing = false;
			m_feedback.Play(ui::Cue::Back);
			break;
		default: break;
		}
	}

	std::vector<Hint> Shell::SharesHints() const
	{
		if (!m_shares.editing)
			return {{Icon::Cross, m_shares.selected < (int)m_shares.list.size() ? Tr("Change") : Tr("Add")}, {Icon::Circle, Tr("Back")}};
		std::vector<Hint> hints;
		if (m_shares.row == kNetwork || m_shares.row == kType || m_shares.row == kFolder)
			hints.push_back({Icon::LeftRight, Tr("Change")});
		hints.push_back({Icon::Cross, m_shares.row == kRemove ? Tr("Hold to remove") : Tr("Choose")});
		hints.push_back({Icon::Triangle, Tr("Look again")});
		hints.push_back({Icon::Circle, Tr("Back")});
		return hints;
	}

	void Shell::SharesDraw(Canvas& canvas)
	{
		SharesPoll();
		// the page's head, in the list's column: the sheet takes the right
		canvas.PushAlpha(Enter(0));
		canvas.Text(Style(kOverlineStyle), kSafeX, 64, Tr("Game files"), Secondary(), kListW, 1);
		canvas.Text(Style(kTitleStyle), kSafeX, 94, Tr("Network shares"), kText, kListW, 1);
		canvas.Text(Style({24, ui::Weight::Regular, 1.4f}), kSafeX, 184,
			Tr("Games on a computer or a NAS, played where they are. Each share is a tab of the folder browser. Read only."), Secondary(), kListW, 3);
		canvas.PopAlpha();
		// the shares, then Add a share
		canvas.PushAlpha(Enter(1) * (m_shares.editing ? 0.45f : 1.0f));
		const int count = (int)m_shares.list.size() + 1;
		for (int i = 0; i < count; i++)
		{
			const Box box{kListX, kTop + i * kRowH, kListW, kRowH - 14};
			const bool focused = !m_shares.editing && i == m_shares.selected;
			canvas.Rect(box, kRadiusCard, focused ? Surface2() : Surface());
			canvas.Ring(box, kRadiusCard, 1.5f, kGlassEdge);
			if (focused)
				Focus(box, kRadiusCard);
			const bool add = i == (int)m_shares.list.size();
			canvas.Draw(add ? Icon::Sparkle : Icon::Folder, {box.x + 26, box.CentreY() - 16, 32, 32}, Accent());
			if (add)
			{
				canvas.Text(Style(kBodyStyle), box.x + 80, box.CentreY() - 19, Tr("Add a share"), Secondary(), box.w - 110, 1);
				continue;
			}
			const ps5netfs::Share& share = m_shares.list[i];
			canvas.Text(Style({26, ui::Weight::SemiBold, 1.0f}), box.x + 80, box.y + 18, share.name, kText, box.w - 300, 1);
			canvas.Text(Style(kCaptionStyle), box.x + 80, box.y + 54, (Nfs(share) ? "NFS · " : "SMB · ") + Where(share), Secondary(), box.w - 300, 1);
			if (i < (int)m_shares.status.size() && !m_shares.status[i].empty())
				canvas.Text(Style({20, ui::Weight::Medium, 1.0f}), box.Right() - 226, box.CentreY() - 12, m_shares.status[i], kGood, 200, 1,
					ui::Align::Right);
		}
		canvas.PopAlpha();
		if (!m_shares.editing)
		{
			if (m_shares.list.empty())
				canvas.Text(Style({22, ui::Weight::Regular, 1.45f}), kListX, kTop + kRowH + 20,
					Tr("Turn on file sharing for a folder on your computer or NAS, then add it here. The PS5 reads games from it; it never "
					   "writes to it. A cable does better than Wi-Fi for Wii U games."),
					Secondary(), kListW, 4);
			return;
		}

		// the sheet: the share being added or changed
		const Box sheet{kSheetX, kSheetY, kSheetW, kSheetH};
		DrawSheet(canvas, sheet, Enter(1));
		canvas.Text(Style(kHeadingStyle), sheet.x + 48, sheet.y + 30, m_shares.index >= 0 ? m_shares.share.name : std::string(Tr("Add a share")), kText,
			sheet.w - 96, 1);
		canvas.Text(Style(kCaptionStyle), sheet.x + 48, sheet.y + 74, Tr("Nothing is ever written to it."), Tertiary(), sheet.w - 96, 1);
		const std::vector<Row> rows = ShareRows();
		float y = sheet.y + 116;
		for (int i = 0; i <= kRemove; i++)
		{
			if (Nfs(m_shares.share) && (i == kUser || i == kPassword))
				continue;
			const bool focused = i == m_shares.row;
			const float h = focused && !rows[i].description.empty() ? kFieldH + 32 : kFieldH;
			const Box box{sheet.x + 40, y, sheet.w - 80, h};
			DrawRow(canvas, rows[i], box, focused);
			if (focused)
			{
				Focus(box, kRadiusCard);
				if (i == kRemove && m_holdProgress > 0)
					HoldRing(canvas, box, m_holdProgress);
			}
			y += h + kFieldGap;
		}
		if (!m_shares.message.empty())
		{
			canvas.Draw(m_shares.messageGood ? Icon::Check : Icon::Warning, {sheet.x + 48, sheet.Bottom() - 60, 28, 28}, m_shares.messageGood ? kGood : kWarn);
			canvas.Text(Style({21, ui::Weight::Medium, 1.3f}), sheet.x + 88, sheet.Bottom() - 62, m_shares.message, m_shares.messageGood ? kGood : kText,
				sheet.w - 136, 2);
		}
	}
}
