// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: Make it yours' second step, once, on a first start or the first start of 6.0: whether to
// send reports to the project (app/reports.h). Off until the player answers; Yes has the focus (the
// owner's choice, 6.0), so Cross answers yes, and Circle is Not now. Settings > Online changes it later.
// Asked only by a build with an intake to send to (ps5reports::Available).

#include "shell_internal.h"
#include "../../app/reports.h"
#include "../../ps5/log.h"

#include <algorithm>

namespace ps5shell
{
	using namespace ui::tokens;

	void Shell::ReportsOpen(std::function<void()> then)
	{
		m_reportsThen = std::move(then);
		m_reportsFocus = 1; // Yes
		m_onBar = false;
		Show(ScreenId::Reports);
		m_screenAt = m_now;
	}

	void Shell::ReportsAnswer(bool yes)
	{
		m_settings.ui.reports = yes;
		m_settings.ui.reportsAsked = true;
		SaveSettings();
		ps5reports::SetEnabled(ps5reports::Available() && yes);
		ps5log::Line("[reports] {}", yes ? "the player said yes" : "the player said not now");
		m_feedback.Play(ui::Cue::Select);
		if (m_reportsThen)
		{
			const std::function<void()> then = std::move(m_reportsThen);
			m_reportsThen = nullptr;
			then();
		}
	}

	void Shell::ReportsUpdate(const ui::Press& press)
	{
		switch (press.button)
		{
		case Button::Left:
		case Button::Right:
		{
			const int next = press.button == Button::Right ? 1 : 0;
			if (next == m_reportsFocus)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_reportsFocus = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Cross: ReportsAnswer(m_reportsFocus == 1); break;
		case Button::Circle: ReportsAnswer(false); break;
		default: break;
		}
	}

	void Shell::ReportsDraw(Canvas& canvas)
	{
		// the layout question's ground and ink (orbit.cpp, LayoutDraw)
		constexpr uint32_t kInk = 0xfffbf7f5, kInk2 = 0xc7fbf7f5, kInk3 = 0x9efbf7f5;
		const float left = 140, width = 1920 - 280;
		canvas.PushAlpha(Enter(0));
		DrawMark(canvas, 115, 70, 30);
		DrawWordmark(canvas, 160, 70, 28, kInk);
		canvas.Text(Style(kOverlineStyle), left, 136, Tr("Make it yours"), kInk3, width, 1);
		// tr: the first start's question: whether the app may send reports of how games run to the project
		canvas.Text(Style({54, ui::Weight::SemiBold, 1.1f}), left, 164, Tr("Help Playtendoh get better?"), kInk, width, 1);
		canvas.Text(Style({22, ui::Weight::Regular, 1.3f}), left, 240,
			Tr("When a game ends, Playtendoh can send the project a short report of how it ran, so what fails gets fixed and the list of games "
			   "that work grows. Nothing about you is in it, and nothing is sent while you play."),
			kInk2, 1200, 2);
		canvas.PopAlpha();

		// what is sent, what never is, and where every field is written down
		struct Column
		{
			const char* title;
			const char* lines[3];
		};
		const Column columns[2] = {
			{Tr("What a report has"),
				{Tr("The game and its console, how it ended, and for how long"), Tr("The settings it ran with, and the PS5's firmware and model"),
					Tr("The last lines of Playtendoh's log, with addresses and folder names taken out")}},
			{Tr("What it never has"),
				{Tr("Your PSN name or ID, your console's serial, its IP or MAC address"),
					Tr("Your RetroAchievements and SteamGridDB accounts, room names, nicknames and Wi-Fi names"), Tr("Your folders, your saves and your game list")}},
		};
		canvas.PushAlpha(Enter(1));
		const float top = 344, columnWidth = 560, gap = 40;
		for (int c = 0; c < 2; c++)
		{
			const Box card{left + c * (columnWidth + gap), top, columnWidth, 470};
			canvas.Rect(card, 24, 0x0fffffff);
			canvas.Ring(card, 24, 1, 0x1affffff);
			const float x = card.x + 36, textWidth = card.w - 72;
			canvas.Text(Style({28, ui::Weight::SemiBold, 1.1f}), x, card.y + 34, columns[c].title, kInk, textWidth, 1);
			float y = card.y + 96;
			for (const char* line : columns[c].lines)
			{
				canvas.Rect({x, y + 11, 8, 8}, 4, c == 0 ? 0xff94c92b : 0xff5f6bf0);
				const ui::TextBlock text = canvas.Text(Style({21, ui::Weight::Regular, 1.4f}), x + 26, y, line, kInk2, textWidth - 26, 3);
				y += text.height + 26;
			}
		}
		// every field, on a phone
		const std::string address = ps5reports::kExplained;
		auto code = m_qrCodes.find(address);
		if (code == m_qrCodes.end())
			code = m_qrCodes.emplace(address, ui::QrCode(address)).first;
		const Box aside{left + 2 * (columnWidth + gap), top, 1920 - 140 - (left + 2 * (columnWidth + gap)), 470};
		canvas.Rect(aside, 24, 0x0fffffff);
		canvas.Ring(aside, 24, 1, 0x1affffff);
		const float qr = 240;
		canvas.Rect({aside.x + 36, aside.y + 36, qr + 24, qr + 24}, 16, 0xffffffff);
		ui::DrawQr(canvas, code->second, {aside.x + 48, aside.y + 48, qr, qr}, 12);
		canvas.Text(Style({21, ui::Weight::SemiBold, 1.3f}), aside.x + 36, aside.y + 330, Tr("Every field a report has"), kInk, aside.w - 72, 1);
		canvas.Text(Style({18, ui::Weight::Regular, 1.4f}), aside.x + 36, aside.y + 364, "github.com/premohq/PlayTendoh docs/REPORTS.md", kInk3, aside.w - 72, 2);
		canvas.PopAlpha();

		// the two answers: Not now, then Yes, in focus to begin with
		canvas.PushAlpha(Enter(2));
		canvas.Text(Style({19, ui::Weight::Regular, 1.3f}), left, 912,
			Tr("You can change this any time in Settings > Online. Turning it off deletes the reports waiting to be sent."), kInk2, 900, 2);
		const std::string answers[2] = {Tr("Not now"), Tr("Yes, send reports")};
		const ui::TextStyle answerStyle = Style({22, ui::Weight::SemiBold, 1.0f});
		float right = 1920 - 140;
		for (int i = 1; i >= 0; i--)
		{
			const bool on = i == m_reportsFocus;
			const float buttonWidth = m_fonts.Width(answerStyle, answers[i]) + (on ? 72 + 36 : 72);
			const Box button{right - buttonWidth, 900, buttonWidth, 68};
			right = button.x - 20;
			canvas.Rect(button, 34, on ? kInk : 0x14ffffff);
			if (!on)
				canvas.Ring(button, 34, 1.5f, 0x40ffffff);
			const ui::TextBlock label = m_fonts.Layout(answerStyle, answers[i]);
			float x = button.x + 36;
			if (on)
			{
				canvas.Draw(Icon::Cross, {button.x + 32, button.CentreY() - 13, 26, 26}, 0xff1b0f0a);
				x = button.x + 68;
			}
			canvas.Text(label, x, button.CentreY() - label.height * 0.5f, on ? 0xff1b0f0a : kInk);
		}
		canvas.PopAlpha();

		const ui::TextStyle hint = Style({17, ui::Weight::Regular, 1.0f});
		const ui::TextBlock choose = m_fonts.Layout(hint, Tr("Choose"));
		canvas.Draw(Icon::LeftRight, {1920 - 96 - choose.width - 34, 1080 - 56, 26, 26}, kInk2);
		canvas.Text(choose, 1920 - 96 - choose.width, 1080 - 43 - choose.height * 0.5f, kInk2);
	}
}
