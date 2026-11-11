// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: New in this version. After an update, once, the new options to set up, each a row as in
// Settings (the same rows: Settings changes them later); What's new (whatsnew.cpp) has said what the
// version brings before it. Circle goes on: nothing here has to be answered. A first start has its own
// setup instead (firststart.cpp).

#include "shell_internal.h"
#include "../../app/reports.h"
#include "../../app/updates.h"
#include "../../azahar/controls.h"
#include "../../ps5/pad.h"

#include <algorithm>

namespace ps5shell
{
	using namespace ui::tokens;

	namespace
	{
		constexpr float kListTop = 290, kListWidth = 1000, kRowStep = 104, kRowHeight = 92;
		constexpr float kAsideX = 1196, kAsideWidth = 628, kAsidePad = 36;

		// tr: what a Wii U player's touchpad click does (as Settings > Controls > Player names them)
		constexpr const char* kTouchpadClicks[] = {TrMark("Automatic"), TrMark("Touches the screen"), TrMark("Presses a button")};
	}

	void Shell::WelcomeOpen()
	{
		m_welcomeRow = 0;
		m_welcomeScroll = 0;
		m_onBar = false;
		m_message.clear();
		Show(ScreenId::Welcome);
		m_screenAt = m_now;
		// its first steps, once each, for a console that never had them: which layout, then whether to send
		// reports (6.0)
		auto back = [this] {
			Show(ScreenId::Welcome);
			m_screenAt = m_now;
		};
		auto reports = [this, back] {
			if (ps5reports::Available() && !m_settings.ui.reportsAsked)
				ReportsOpen(back);
			else
				back();
		};
		if (!m_settings.ui.layoutChosen)
			LayoutOpen(reports);
		else if (ps5reports::Available() && !m_settings.ui.reportsAsked)
			ReportsOpen(back);
	}

	std::vector<Row> Shell::WelcomeRows()
	{
		std::vector<Row> rows;
		// the Settings pages' own rows, by id, in the order asked for
		auto pick = [&](const std::string& page, std::initializer_list<const char*> ids) {
			const std::vector<Row> found = SideRows(page, m_settings);
			for (const char* id : ids)
				for (const Row& row : found)
					if (row.id == id)
						rows.push_back(row);
		};
		// A and B on every side at once
		auto faceButtons = [&] {
			Row row;
			row.kind = Row::Kind::Segmented;
			row.id = "abeverywhere";
			row.label = Tr("A and B");
			row.options = {Tr("A on Circle"), Tr("A on Cross")};
			const bool aOnCircle = ps5azahar::MappedInput(m_settings.n3ds, ps5azahar::Button::A) == ps5emu::PadInput::Circle;
			row.index = aOnCircle ? 0 : 1;
			row.value = row.options[row.index];
			row.description = Tr("For every system: A on Circle as on Nintendo consoles, or on Cross.");
			row.help = Tr("A on Circle and B on Cross as on Nintendo consoles, or A on Cross and B on Circle as on PlayStation. X and Y swap to match. You can change it later in each system's Controls.");
			rows.push_back(row);
		};
		pick("launcher", {"theme", "side-wiiu", "side-switch", "side-ds", "side-3ds"});
		faceButtons();
		if (SideShown(System::WiiU))
		{
			Row touchpad;
			touchpad.kind = Row::Kind::Choice;
			touchpad.id = "touchpad1";
			touchpad.label = Tr("Touchpad click");
			touchpad.options = {Tr(kTouchpadClicks[0]), Tr(kTouchpadClicks[1]), Tr(kTouchpadClicks[2])};
			touchpad.index = std::clamp(m_settings.touchpadClick[0], 0, 2);
			touchpad.value = touchpad.options[touchpad.index];
			touchpad.description = Tr("For Wii U player 1: touch the GamePad screen, or press a button.");
			touchpad.help = Tr("What a touchpad click does for Wii U player 1: touch the GamePad screen, or press the button set for Touchpad click (- (Select) if none). Automatic: a touch with the GamePad, a button with other controllers. Other players: Settings > Controls for the Wii U.");
			rows.push_back(touchpad);
			// on the Wii U side, its pages' new rows
			if (IsWiiU())
				pick("graphics", {"upscaling", "shadernotices"});
		}
		Row next;
		next.kind = Row::Kind::Link;
		next.id = "continue";
		next.label = Tr("Continue to the Library");
		next.description = Tr("Everything here is in Settings too.");
		next.help = Tr("Continue. You don't have to choose now: everything here is also in Settings.");
		rows.push_back(next);
		return rows;
	}

	void Shell::WelcomeDone()
	{
		m_feedback.Play(ui::Cue::Select);
		OpenSide(m_side, !m_prepared);
	}

	void Shell::WelcomeUpdate(const ui::Press& press, const ui::Actions& actions)
	{
		(void)actions;
		const Button b = press.button;
		if (b == Button::Count)
			return;
		auto rows = WelcomeRows();
		if (rows.empty())
			return;
		m_welcomeRow = std::clamp(m_welcomeRow, 0, (int)rows.size() - 1);
		const Row& row = rows[m_welcomeRow];
		switch (b)
		{
		case Button::Up:
		case Button::Down:
		{
			const int next = m_welcomeRow + (b == Button::Down ? 1 : -1);
			if (next < 0 || next >= (int)rows.size())
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_welcomeRow = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Circle: WelcomeDone(); break;
		case Button::Triangle: OpenHelp(row.label, row.help); break;
		case Button::Left:
		case Button::Right:
		case Button::Cross:
		{
			const bool cross = b == Button::Cross;
			const int step = cross ? 0 : b == Button::Right ? 1 : -1;
			if (row.id == "continue")
			{
				if (cross)
					WelcomeDone();
				break;
			}
			if (row.id == "touchpad1")
			{
				int& click = m_settings.touchpadClick[0];
				click = cross ? (click + 1) % 3 : std::clamp(click + step, 0, 2);
				if (m_status.coreReady)
					ps5emu::ApplyTouchpadClick(0, click, true);
				SaveSettings();
				m_feedback.Play(ui::Cue::Focus);
				break;
			}
			if (row.id == "abeverywhere")
			{
				const int index = cross ? 1 - row.index : std::clamp(row.index + step, 0, 1);
				if (index == row.index)
				{
					m_feedback.Play(ui::Cue::Edge);
					break;
				}
				SetRowIndex(m_settings, row.id, index);
				SaveSettings();
				m_feedback.Play(ui::Cue::Focus);
				break;
			}
			ChangeSetting(row, step, cross);
			break;
		}
		default: break;
		}
	}

	void Shell::WelcomeDraw(Canvas& canvas)
	{
		auto rows = WelcomeRows();
		m_welcomeRow = std::clamp(m_welcomeRow, 0, std::max(0, (int)rows.size() - 1));

		// what this is
		canvas.PushAlpha(Enter(0));
		canvas.Text(Style(kOverlineStyle), kSafeX, 92, TrF("Playtendoh {0}", ps5update::Readable(PS5CEMU_VERSION)), Secondary(), 1100, 1);
		canvas.Text(Style({56, ui::Weight::Bold, 1.1f, -0.8f}), kSafeX, 122, Tr("New in this version"), kText, 1100, 1);
		canvas.Text(Style({24, ui::Weight::Regular, 1.3f}), kSafeX, 206, Tr("A few new options to set up. They're also in Settings."), Secondary(), 1100, 1);
		canvas.PopAlpha();

		// the rows: those past the screen's foot scroll with the focus
		canvas.PushAlpha(Enter(1));
		canvas.PushOffset(0, (1 - Enter(1)) * 24);
		constexpr float kListBottom = 1080 - 110;
		const float target = std::max(0.0f, (m_welcomeRow + 1) * kRowStep - (kListBottom - kListTop));
		m_welcomeScroll += (target - m_welcomeScroll) * std::min(1.0f, m_dt * (m_settings.ui.reduceMotion ? 60.0f : 14.0f));
		canvas.PushClip({0, kListTop - 12, kSafeX + kListWidth + 24, kListBottom - kListTop + 24});
		const float labelScale = LabelScale(rows, kListWidth);
		for (int i = 0; i < (int)rows.size(); i++)
		{
			const Box box{kSafeX, kListTop + i * kRowStep - m_welcomeScroll, kListWidth, kRowHeight};
			DrawRow(canvas, rows[i], box, i == m_welcomeRow, labelScale);
		}
		canvas.PopClip();
		canvas.PopOffset();
		canvas.PopAlpha();
		if (rows.empty())
			return;

		// beside the list: what the focused row is for, in as many whole lines as fit (the rest behind
		// Triangle)
		const Row& row = rows[m_welcomeRow];
		const float inner = kAsideWidth - 2 * kAsidePad;
		const ui::TextBlock heading = m_fonts.Layout(Style({30, ui::Weight::SemiBold, 1.2f}), row.label, inner, 2);
		const float top = kListTop + 10;
		const ui::TextStyle aboutStyle = Style({21, ui::Weight::Regular, 1.55f});
		const float aboutRoom = kListBottom - top - (kAsidePad + 18 + 10 + heading.height + 14 + kAsidePad);
		const int aboutLines = std::clamp((int)(aboutRoom / (aboutStyle.size * aboutStyle.lineHeight)), 1, 12);
		const ui::TextBlock about = m_fonts.Layout(aboutStyle, row.help, inner, aboutLines);
		const float height = std::min(kListBottom - top, kAsidePad + 18 + 10 + heading.height + 14 + about.height + kAsidePad);
		const Box aside{kAsideX, top, kAsideWidth, height};
		canvas.PushAlpha(Enter(2));
		canvas.PushOffset((1 - Enter(2)) * 30, 0);
		Glass(canvas, aside, kRadiusSheet, m_settings.ui.highContrast ? kInk2 : ui::SetAlpha(kInk1, 0.80f));
		canvas.PushClip(aside, kRadiusSheet);
		float y = aside.y + kAsidePad;
		const float ax = aside.x + kAsidePad;
		canvas.Text(Style(kOverlineStyle), ax, y, Tr("This row"), Secondary(), inner, 1);
		y += 18 + 10;
		canvas.Text(heading, ax, y, kText);
		y += heading.height + 14;
		canvas.Text(about, ax, y, Secondary());
		canvas.PopClip();
		canvas.PopOffset();
		canvas.PopAlpha();
	}
}
