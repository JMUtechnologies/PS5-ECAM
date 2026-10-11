// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the Setup check (docs/UI-REDESIGN.md, 6.7). It answers "why doesn't it work?" in one place: storage, the recompilers, each side's games and keys,
// the 3DS's system files, the DS's BIOS and box art, each with its status, what it means and what to
// do, and a QR code to the guide's page about it. It shows on the first start and whenever storage or
// the recompilers fail at start, then the app opens on a side's Library; Settings > Diagnostics opens
// it at any time. Nothing asks which side (5.3): the app opens on the side last used.

#include "shell_internal.h"
#include "../actions.h"
#include "../../app/paths.h"
#include "../../app/updates.h"
#include "../../azahar/azahar.h"
#include "../../melonds/melonds.h"
#include "../../ps5/privilege.h"

#include <algorithm>
#include <cmath>
#include <sys/statvfs.h>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		constexpr float kListTop = 290, kListWidth = 1000, kRowHeight = 82, kRowGap = 8;
		constexpr float kAsideX = 1196, kAsideWidth = 628, kAsidePad = 36;
		constexpr const char* kSite = "github.com/premohq/PS5CEMU-HAR";
		constexpr const char* kRepository = "https://github.com/premohq/PS5CEMU-HAR";

		// "Five", for the progress line (Appendix B: it reads as a sentence)
		std::string Words(int count, bool capital)
		{
			static constexpr const char* kWords[] = {"none", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
			std::string word = count >= 0 && count <= 10 ? kWords[count] : std::to_string(count);
			if (capital && !word.empty() && word[0] >= 'a' && word[0] <= 'z')
				word[0] = (char)(word[0] - 'a' + 'A');
			return word;
		}

		std::string FreeSpace(const char* path)
		{
			struct statvfs info{};
			if (statvfs(path, &info) != 0 || info.f_frsize == 0)
				return {};
			const double gigabytes = (double)info.f_bavail * (double)info.f_frsize / 1e9;
			return gigabytes >= 10 ? fmt::format("{:.0f} GB free", gigabytes) : fmt::format("{:.1f} GB free", gigabytes);
		}

		// where a folder is, as a person names it: "USB drive 1 / 3DS"
		std::string Place(const std::string& folder)
		{
			for (const std::string& drive : ConnectedDrives())
				if (folder == drive || folder.starts_with(drive + "/"))
				{
					std::string place = DriveName(drive);
					for (char c : folder.substr(drive.size()))
						place += c == '/' ? std::string(" / ") : std::string(1, c);
					return place;
				}
			return folder;
		}

		// the covers fetched so far, both sides'
		int Covers()
		{
			int covers = 0;
			for (const char* side : {"wiiu", "3ds", "ds"})
			{
				bool ok = false;
				for (const std::string& name : ListEntries(std::string(ps5paths::kCovers) + "/boxart/" + side, false, ok))
					covers += Lower(name).ends_with(".tga");
			}
			return covers;
		}
	}

	// -- the Setup check ------------------------------------------------------------------------------

	bool Shell::SetupNeeded() const
	{
		const ps5privilege::Result& privileges = ps5privilege::Current();
		return !privileges.filesystem || (!privileges.jit && !privileges.executable);
	}

	void Shell::SetupOpen(bool first)
	{
		m_setupFirst = first;
		if (!first)
			m_setupFrom = m_screen == ScreenId::Settings ? ScreenId::Settings : ScreenId::Library;
		else if (!m_settings.ui.setupDone)
		{
			// shown once (and again whenever storage or the recompilers fail at start)
			m_settings.ui.setupDone = true;
			SaveSettings();
		}
		RunChecks();
		// the focus on the first that needs a look
		m_setupRow = 0;
		for (int i = 0; i < (int)m_checks.size(); i++)
			if (m_checks[i].state == 1)
			{
				m_setupRow = i;
				break;
			}
		m_onBar = false;
		Show(ScreenId::Setup);
	}

	void Shell::FinishStart()
	{
		m_setupFirst = false;
		OpenSide(m_side, true);
	}

	void Shell::RunChecks()
	{
		m_checks.clear();
		const ps5privilege::Result& privileges = ps5privilege::Current();
		const std::string henGuide = std::string(kRepository) + "/blob/HEAD/docs/HEN-SETUP.md";

		// where everything is kept
		Check storage{"storage", "Storage"};
		storage.guide = henGuide;
		storage.guideTitle = "the HEN setup guide";
		if (privileges.filesystem)
		{
			const std::string free = FreeSpace(ps5paths::kRoot);
			storage.state = 0;
			storage.detail = Join({"/data is reachable", free}, " · ");
			storage.aboutTitle = "Where everything is kept";
			storage.about = fmt::format("Settings, saves, caches and logs live in {}, which your HEN opens to PS5CEMU-HAR: its app jailbreak, "
										"or elfldr with the helper that comes with the app.",
				ps5paths::kRoot);
		}
		else
		{
			storage.state = 1;
			storage.detail = "/data is out of reach: nothing can be kept";
			storage.aboutTitle = "Let PS5CEMU-HAR out of its sandbox";
			storage.about = "Add PPSA99360 to your HEN's app jailbreak list (etaHEN: its app jailbreak list; OnionHEN: the end of "
							"exact_title_ids in config.ini, with no comma after it), or have elfldr listening on port 9021. Then start "
							"PS5CEMU-HAR again.";
		}
		m_checks.push_back(storage);

		// what runs the games' code
		Check recompilers{"recompilers", "Recompilers"};
		recompilers.guide = henGuide;
		recompilers.guideTitle = "the HEN setup guide";
		recompilers.aboutTitle = "What runs the games' code";
		if (privileges.jit || privileges.executable)
		{
			recompilers.state = 0;
			recompilers.detail = privileges.jit ? "JIT memory from your HEN: games run at full speed" :
												  "Executable memory: games run at full speed, no HEN grant needed";
			recompilers.about = "Cemu's and Azahar's recompilers turn the games' code into the PS5's as they run. They need memory they "
								"can write code into and run it from: your HEN's JIT memory, or executable memory PS5CEMU-HAR makes itself.";
		}
		else
		{
			recompilers.state = 1;
			recompilers.detail = "No JIT or executable memory: Wii U games run on the much slower interpreter";
			recompilers.about = "Neither your HEN's JIT memory nor executable memory could be made, so Wii U games fall back to Cemu's "
								"interpreter. Add PPSA99360 to your HEN's app jailbreak list, then start PS5CEMU-HAR again; Settings > "
								"Diagnostics says what it got.";
		}
		m_checks.push_back(recompilers);

		// each side's games: as its last scan found them, else as its folder holds them
		for (System side : {System::WiiU, System::N3ds, System::Nds})
		{
			const bool n3ds = side == System::N3ds, nds = side == System::Nds;
			const std::string& folder = GamesFolder(side);
			const int catalogued = (int)ps5catalog::Games((ps5catalog::System)SideIndex(side)).size();
			int count = std::max(catalogued, side == System::WiiU ? m_settings.gameCount : Handheld(side).gameCount);
			bool readable = true;
			if (count <= 0)
			{
				count = n3ds ? Count3dsGames(folder) : nds ? CountDsGames(folder) : CountGames(folder);
				readable = count >= 0;
			}
			Check games{std::string(ps5launcher::SideName(side)) + "-games", fmt::format("{} games", SideTitle(side))};
			games.action = "Choose a folder";
			games.state = count > 0 ? 0 : 1;
			const std::string where = ShortPath(Place(folder), 44);
			games.detail = count > 0 ? fmt::format("{} in {}", Plural(count, "game", "games"), where) :
				readable		   ? fmt::format("None in {} yet", where) :
									 fmt::format("{} cannot be read", where);
			games.aboutTitle = fmt::format("Where your {} games go", SideTitle(side));
			games.about = n3ds ? fmt::format("Put them in {}, or choose any folder the PS5 can read, such as one on a USB drive. Folders "
											 "inside it are searched too. CIA files are installed, from Settings > Game files on the 3DS "
											 "side.",
									 folder) :
				nds			   ? fmt::format("Put them in {}, or choose any folder the PS5 can read, such as one on a USB drive. Folders "
											 "inside it are searched too.",
									 folder) :
								 fmt::format("Put them in {}, or choose any folder the PS5 can read, such as one on a USB drive. Updates and "
											 "DLC go in from Settings > Game files, or come with a .wua.",
									 folder);
			games.chips = n3ds ? std::vector<std::string>{".3ds", ".cci", ".cxi", ".3dsx", ".z3ds", ".cia"} :
				nds			   ? std::vector<std::string>{".nds", ".srl"} :
								 std::vector<std::string>{".wua", ".wud", ".wux", "code · content · meta", ".rpx"};
			games.guide = std::string(kRepository) + (n3ds ? "#nintendo-3ds" : nds ? "#nintendo-ds-melonds" : "#wii-u");
			games.guideTitle = "the game files page";
			m_checks.push_back(games);
		}

		// the keys encrypted dumps need
		const bool discKeys = IsFile(std::string(ps5paths::kRoot) + "/keys.txt");
		Check wiiuKeys{"wiiu-keys", "Wii U disc keys"};
		wiiuKeys.state = discKeys ? 0 : 1;
		wiiuKeys.detail = discKeys ? "keys.txt found" : "keys.txt is missing: only encrypted .wud and .wux need it";
		wiiuKeys.action = discKeys ? "" : "How to";
		wiiuKeys.aboutTitle = "Keys for encrypted discs";
		wiiuKeys.about = fmt::format("Encrypted .wud and .wux dumps need their disc keys in {}/keys.txt, one a line. Decrypted dumps, .wua "
									 "files and folders with code, content and meta need none.",
			ps5paths::kRoot);
		wiiuKeys.guide = std::string(kRepository) + "#wii-u";
		wiiuKeys.guideTitle = "the game files page";
		m_checks.push_back(wiiuKeys);

		const bool aesKeys = IsFile(std::string(ps5azahar::kRoot) + "/sysdata/aes_keys.txt");
		Check n3dsKeys{"3ds-keys", "3DS keys"};
		n3dsKeys.state = aesKeys ? 0 : 1;
		n3dsKeys.detail = aesKeys ? "aes_keys.txt found" : "aes_keys.txt is missing: only encrypted dumps need it";
		n3dsKeys.action = aesKeys ? "" : "How to";
		n3dsKeys.aboutTitle = "Keys for encrypted 3DS dumps";
		n3dsKeys.about = fmt::format("Encrypted dumps need aes_keys.txt from your own console in {}/sysdata. Decrypted dumps, homebrew and "
									 "installed CIAs need none. No keys come with PS5CEMU-HAR.",
			ps5azahar::kRoot);
		n3dsKeys.guide = std::string(kRepository) + "#nintendo-3ds";
		n3dsKeys.guideTitle = "the game files page";
		m_checks.push_back(n3dsKeys);

		// the 3DS's own files, which only the Home Menu and a few games need. Azahar looks for them in its
		// own files, which only the 3DS side opens (asked from elsewhere, its logging would start and run
		// on beside a Wii U game): elsewhere, the check says what they are for
		ps5emu::Game homeMenu;
		const bool systemFiles =
			ps5azahar::Available() && m_prepared == System::N3ds && ps5azahar::HomeMenu(m_settings.n3ds.region, homeMenu);
		Check system{"3ds-system", "3DS system files"};
		system.state = systemFiles ? 0 : 2;
		system.detail = systemFiles ? "Artic Setup has run: the Home Menu can start" : "Artic Setup copies them from your 3DS, for the Home Menu";
		system.action = systemFiles || !ps5azahar::Available() ? "" : "Artic Base";
		system.aboutTitle = "Your 3DS's own files";
		system.about = "The 3DS Home Menu, and the few games that need the console's system files, run once Artic Setup has copied them "
					   "from your own 3DS over the network: start the Artic Setup Tool on the 3DS, then Artic Base here.";
		system.guide = std::string(kRepository) + "#nintendo-3ds-azahar";
		system.guideTitle = "the 3DS page";
		m_checks.push_back(system);

		// a DS's own BIOS, which DS games run without (melonDS's FreeBIOS) but a few run better with
		const bool dsBios = ps5melonds::OwnBiosFound();
		Check nds{"ds-bios", "DS BIOS"};
		nds.state = dsBios ? 0 : 2;
		nds.detail = dsBios ? "Your DS's BIOS found: DS games run on it" : "melonDS's own run DS games; your DS's are optional";
		nds.action = dsBios ? "" : "How to";
		nds.aboutTitle = "Your DS's own BIOS and firmware";
		nds.about = fmt::format("melonDS runs DS games on its own BIOS (FreeBIOS) and firmware, which most games are happy with. The few that "
								"are not run on your own DS's: bios7.bin, bios9.bin and firmware.bin, dumped from it, in {}/bios. No BIOS "
								"comes with PS5CEMU-HAR.",
			ps5melonds::kRoot);
		nds.guide = std::string(kRepository) + "#nintendo-ds-melonds";
		nds.guideTitle = "the DS page";
		m_checks.push_back(nds);

		// covers
		Check boxArt{"boxart", "Box art"};
		boxArt.guide = std::string(kRepository) + "#online";
		boxArt.guideTitle = "what PS5CEMU-HAR fetches online";
		boxArt.aboutTitle = "Covers from GameTDB";
		boxArt.about = "The first time a game shows up, its cover comes from GameTDB (art.gametdb.com), by the ID on its box. Without a "
					   "connection the games' own icons are shown, and the next start tries again.";
		const int covers = Covers();
		if (!m_settings.boxArt)
		{
			boxArt.state = 2;
			boxArt.detail = "Off: the games' icons are shown instead";
			boxArt.action = "Turn on";
		}
		else if (ps5boxart::Answered() == 0)
		{
			boxArt.state = 1;
			boxArt.detail = "GameTDB did not answer: the games' icons are shown instead";
		}
		else
		{
			boxArt.state = 0;
			boxArt.detail = ps5boxart::Answered() == 1 ? fmt::format("GameTDB is reachable · {}", Plural(covers, "cover", "covers")) :
				covers > 0								  ? fmt::format("On · {}", Plural(covers, "cover", "covers")) :
															"On: covers come as your games show up";
		}
		m_checks.push_back(boxArt);
	}

	void Shell::SetupUpdate(const ui::Press& press)
	{
		const int count = (int)m_checks.size();
		switch (press.button)
		{
		case Button::Up:
		case Button::Down:
		{
			const int next = m_setupRow + (press.button == Button::Down ? 1 : -1);
			if (next < 0 || next >= count)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_setupRow = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Cross:
		{
			if (m_setupRow >= count || m_checks[m_setupRow].action.empty())
			{
				m_feedback.Play(ui::Cue::Denied);
				break;
			}
			const Check& check = m_checks[m_setupRow];
			m_feedback.Play(ui::Cue::Select);
			if (check.id == "wiiu-games" || check.id == "3ds-games" || check.id == "ds-games")
			{
				m_filesSide = ps5launcher::SideNamed(check.id.substr(0, check.id.find('-')));
				FilesOpen(0); // back here when it is done
			}
			else if (check.id == "3ds-system")
				ArticOpen();
			else if (check.id == "boxart")
			{
				m_settings.boxArt = true;
				ps5boxart::SetEnabled(true);
				SaveSettings();
				RunChecks();
			}
			else
				OpenHelp(check.aboutTitle, check.about);
			break;
		}
		case Button::Triangle:
			RunChecks();
			m_feedback.Play(ui::Cue::Select);
			Toast("Checked again");
			break;
		case Button::Circle:
			m_feedback.Play(ui::Cue::Back);
			if (m_setupFirst)
				FinishStart();
			else if (m_setupFrom == ScreenId::Library)
				ShowTab(ScreenId::Library);
			else
				SettingsOpen("diagnostics", true);
			break;
		default: break;
		}
	}

	void Shell::SetupDraw(Canvas& canvas)
	{
		if (m_checks.empty())
			RunChecks();
		const int count = (int)m_checks.size();
		m_setupRow = std::clamp(m_setupRow, 0, std::max(0, count - 1));

		// what this is, and how far along
		canvas.PushAlpha(Enter(0));
		canvas.Text(Style(kOverlineStyle), kSafeX, 92, m_setupFirst ? "Welcome to PS5CEMU-HAR" : "Help", Secondary());
		canvas.Text(Style({56, ui::Weight::Bold, 1.1f, -0.8f}), kSafeX, 122, "Let’s check your setup", kText, 1000, 1);
		const int ready = (int)std::count_if(m_checks.begin(), m_checks.end(), [](const Check& check) { return check.state != 1; });
		float x = kSafeX;
		for (int i = 0; i < count; i++, x += 50)
			canvas.Rect({x, 243, 40, 6}, 3, i < ready ? kText : 0x24ffffff);
		const std::string progress = ready == count ? fmt::format("All {} are ready.", Words(count, false)) :
			ready == 0								? fmt::format("{} need a look.", count == 1 ? "It" : "All") :
													  fmt::format("{} of {} {} ready. {} {} a look.", Words(ready, true), Words(count, false),
														  ready == 1 ? "is" : "are", Words(count - ready, true), count - ready == 1 ? "needs" : "need");
		canvas.Text(Style({20, ui::Weight::Medium, 1.0f}), x + 8, 236, progress, Secondary());
		canvas.PopAlpha();

		// the checks: those past the screen's foot scroll with the focus
		canvas.PushAlpha(Enter(1));
		canvas.PushOffset(0, (1 - Enter(1)) * 24);
		constexpr float kListBottom = 1080 - 96;
		const float listHeight = count * (kRowHeight + kRowGap) - kRowGap;
		const float listScroll = std::clamp(m_setupRow * (kRowHeight + kRowGap) - (kListBottom - kListTop) * 0.5f, 0.0f,
			std::max(0.0f, listHeight - (kListBottom - kListTop)));
		canvas.PushClip({0, kListTop - 12, kSafeX + kListWidth + 24, kListBottom - kListTop + 24});
		for (int i = 0; i < count; i++)
		{
			const Check& check = m_checks[i];
			const Box row{kSafeX, kListTop + i * (kRowHeight + kRowGap) - listScroll, kListWidth, kRowHeight};
			const bool focused = i == m_setupRow;
			canvas.Rect(row, kRadiusCard, focused ? Surface2() : Surface());
			if (focused)
				canvas.Rect(row, kRadiusCard, 0x0dffffff);
			// its status
			const uint32_t colour = check.state == 0 ? kGood : check.state == 1 ? kWarn : Tertiary();
			const Box icon{row.x + 22, row.CentreY() - 22, 44, 44};
			canvas.Rect(icon, 22, check.state == 2 ? 0x14ffffff : ui::SetAlpha(colour, 0.16f));
			canvas.Draw(check.state == 0 ? Icon::Check : check.state == 1 ? Icon::Warning : Icon::Dash, icon.Inset(10), colour);
			// what Cross does, on the right
			float right = row.Right() - 28;
			if (!check.action.empty())
			{
				const ui::TextBlock action = m_fonts.Layout(Style({21, ui::Weight::SemiBold, 1.0f}), check.action);
				right -= action.width;
				canvas.Text(action, right, row.CentreY() - action.height * 0.5f, focused ? kText : Secondary());
				if (focused)
				{
					canvas.Draw(Icon::Cross, {right - 36, row.CentreY() - 13, 26, 26}, kText);
					right -= 36;
				}
				right -= 24;
			}
			// what it is, and what was found
			const float tx = icon.Right() + 22;
			const ui::TextBlock title = canvas.Text(Style({25, ui::Weight::SemiBold, 1.2f}), tx, row.y + 11, check.title, kText, 0, 1);
			if (check.state == 2)
				canvas.Text(Style({25, ui::Weight::Regular, 1.2f}), tx + title.width + 10, row.y + 11, "· optional", Tertiary());
			canvas.Text(Style({20, ui::Weight::Regular, 1.25f}), tx, row.y + 45, check.detail, Secondary(), std::max(100.0f, right - tx), 1);
			if (focused)
				Focus(row, kRadiusCard);
		}
		canvas.PopClip();
		canvas.PopOffset();
		canvas.PopAlpha();
		if (count == 0)
			return;

		// the focused check, beside the list: what it means, what to do, and the guide's page
		const Check& check = m_checks[m_setupRow];
		const float inner = kAsideWidth - 2 * kAsidePad;
		const ui::TextBlock heading = m_fonts.Layout(Style({32, ui::Weight::SemiBold, 1.2f}), check.aboutTitle, inner, 2);
		const ui::TextBlock about = m_fonts.Layout(Style({21, ui::Weight::Regular, 1.55f}), check.about, inner, 8);
		float chipsHeight = 0;
		{
			float cx = 0;
			int lines = check.chips.empty() ? 0 : 1;
			for (const std::string& chip : check.chips)
			{
				const float width = m_fonts.Width(Style({20, ui::Weight::SemiBold, 1.0f}), chip) + 32;
				if (cx > 0 && cx + width > inner)
				{
					cx = 0;
					lines++;
				}
				cx += width + 8;
			}
			chipsHeight = lines ? lines * 38 + (lines - 1) * 8 + 18 : 0;
		}
		const float qr = 150;
		const float height = kAsidePad + 18 + 10 + heading.height + 14 + about.height + chipsHeight + 28 + 2 + 26 + qr + kAsidePad;
		const Box aside{kAsideX, kListTop + 10, kAsideWidth, height};
		canvas.PushAlpha(Enter(2));
		canvas.PushOffset((1 - Enter(2)) * 30, 0);
		canvas.Rect(aside, kRadiusSheet, m_settings.ui.highContrast ? kInk2 : 0xbf1e130e);
		canvas.Ring(aside, kRadiusSheet, 1.5f, kGlassEdge);
		float y = aside.y + kAsidePad;
		const float ax = aside.x + kAsidePad;
		canvas.Text(Style(kOverlineStyle), ax, y, check.title, Secondary(), inner, 1);
		y += 18 + 10;
		canvas.Text(heading, ax, y, kText);
		y += heading.height + 14;
		canvas.Text(about, ax, y, Secondary());
		y += about.height;
		if (!check.chips.empty())
		{
			y += 18;
			float cx = ax;
			for (const std::string& chip : check.chips)
			{
				const float width = m_fonts.Width(Style({20, ui::Weight::SemiBold, 1.0f}), chip) + 32;
				if (cx > ax && cx + width > ax + inner)
				{
					cx = ax;
					y += 38 + 8;
				}
				Chip(canvas, cx, y, chip);
				cx += width + 8;
			}
			y += 38;
		}
		y += 28;
		canvas.Rect({ax, y, inner, 1.5f}, 0, kLine);
		y += 2 + 26;
		auto code = m_qrCodes.find(check.guide);
		if (code == m_qrCodes.end())
			code = m_qrCodes.emplace(check.guide, ui::QrCode(check.guide)).first;
		ui::DrawQr(canvas, code->second, {ax, y, qr, qr}, 12);
		const float tx = ax + qr + 24, tw = aside.Right() - kAsidePad - tx;
		const ui::TextBlock guideTitle = m_fonts.Layout(Style({22, ui::Weight::SemiBold, 1.3f}), "The guide, on your phone", tw, 2);
		const ui::TextBlock guideText =
			m_fonts.Layout(Style({19, ui::Weight::Regular, 1.5f}), fmt::format("Scan it for {}: {}", check.guideTitle, kSite), tw, 4);
		const float ty = y + (qr - guideTitle.height - 6 - guideText.height) * 0.5f;
		canvas.Text(guideTitle, tx, ty, kText);
		canvas.Text(guideText, tx, ty + guideTitle.height + 6, Secondary());
		canvas.PopOffset();
		canvas.PopAlpha();
	}

	// -- what the Setup check stands on: the sides, meeting at the seam -----------------

	void Shell::DrawBrandBackdrop(Canvas& canvas, float seam)
	{
		const Box screen{0, 0, 1920, 1080};
		canvas.LinearGradient(screen, 0, 0xff160c08, kInk0, 0, 0, 0, 1080);
		// the Wii U's blue far behind the left, the 3DS's gold behind the right, as the banner has them, and
		// the DS's green rising from below the middle
		canvas.RadialGradient(screen, 0, ui::SetAlpha(0xffdf9f3b, 0.30f), ui::SetAlpha(0xffdf9f3b, 0), 0.18f * 1920, 0.40f * 1080, 605, 529);
		canvas.RadialGradient(screen, 0, ui::SetAlpha(kN3ds, 0.20f), ui::SetAlpha(kN3ds, 0), 0.88f * 1920, 0.60f * 1080, 605, 529);
		canvas.RadialGradient(screen, 0, ui::SetAlpha(kNds, 0.14f), ui::SetAlpha(kNds, 0), 0.5f * 1920, 1.05f * 1080, 700, 420);
		// bubbles on the one side of the seam, waves on the other
		canvas.PushClip({0, 0, seam, 1080});
		DrawBubbles(canvas, 0.6f);
		canvas.PopClip();
		canvas.PushClip({seam, 0, 1920 - seam, 1080});
		DrawWaves(canvas, 0.8f);
		canvas.PopClip();
		canvas.LinearGradient({seam - 1, 0, 2, 540}, 0, 0x00ffffff, 0x38ffffff, seam, 0, seam, 540);
		canvas.LinearGradient({seam - 1, 540, 2, 540}, 0, 0x38ffffff, 0x00ffffff, seam, 540, seam, 1080);
		if (!m_settings.ui.highContrast)
			canvas.Grain(screen, 0x0cffffff);
		canvas.RadialGradient(screen, 0, 0x00000000, 0x8c000000, 960, 432, 1248, 1026, 0.55f);
	}
}
