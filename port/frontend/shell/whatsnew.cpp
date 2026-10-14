// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: What's new, once a version, before anything else: on the app's first start and after an
// update, what the version brings, a card each, as its release notes' highlights say it. Cross or
// Circle goes on to what was next: a first start's Make it yours and Setup check, the Setup check, an
// update's new options, or the side.

#include "shell_internal.h"
#include "../../app/updates.h"

#include <algorithm>

namespace ps5shell
{
	using namespace ui::tokens;

	namespace
	{
		struct News
		{
			Icon icon;
			const char* title;
			const char* text;
		};

		// the release notes' highlights: changed with each release that has something to say
		const News kNews[] = {
			// tr: What's new: the 31 languages the app's menus are in
			{Icon::Globe, TrMark("31 languages"), TrMark("Every language the PS5 has, in every menu, and your games' names in yours.")},
			{Icon::Screens, TrMark("Nintendo DS"), TrMark("DS games on melonDS, a third system beside the Wii U and the 3DS.")},
			{Icon::Home, TrMark("Games on your home screen"), TrMark("Add to home screen gives a game a tile of its own that starts it at once.")},
			{Icon::Usb, TrMark("Wii U mods"), TrMark("Mods from a USB drive, each on its game's Graphic packs page.")},
			{Icon::Sparkle, TrMark("Eight themes"), TrMark("Classic brings back the first launcher; Eden, Phantom, Terminal and more.")},
			{Icon::Drive, TrMark("Network shares"), TrMark("Play games from your PC or NAS, over SMB or NFS.")},
			{Icon::Star, TrMark("RetroAchievements"), TrMark("Achievements in DS games: sign in from Settings > Online.")},
			{Icon::Pulse, TrMark("Faster Wii U games"), TrMark("Breath of the Wild near 60 fps at 1440p, and steadier at 4K.")},
			{Icon::Check, TrMark("Pokémon drawn right"), TrMark("X, Y, Omega Ruby, Alpha Sapphire, Sun, Moon and the Ultra games.")},
		};

		constexpr int kColumns = 3;
		constexpr float kGridTop = 290, kGap = 24, kCardHeight = 196, kPad = 26, kIcon = 52;
	}

	void Shell::WhatsNewOpen(NewsThen then)
	{
		m_newsThen = then;
		m_onBar = false;
		m_message.clear();
		Show(ScreenId::WhatsNew);
		m_screenAt = m_now;
	}

	void Shell::WhatsNewDone()
	{
		m_feedback.Play(ui::Cue::Select);
		switch (m_newsThen)
		{
		case NewsThen::Welcome: WelcomeOpen(true); break; // then the Setup check, then the side
		case NewsThen::Setup: SetupOpen(true); break;	  // then the side (FinishStart)
		case NewsThen::NewOptions: WelcomeOpen(false); break;
		case NewsThen::Side: OpenSide(m_side, !m_prepared); break;
		}
	}

	void Shell::WhatsNewUpdate(const ui::Press& press)
	{
		if (press.button == Button::Cross || press.button == Button::Circle)
			WhatsNewDone();
	}

	void Shell::WhatsNewDraw(Canvas& canvas)
	{
		// what this is
		canvas.PushAlpha(Enter(0));
		canvas.Text(Style(kOverlineStyle), kSafeX, 92, TrF("PS5CEMU-HAR {0}", ps5update::Readable(PS5CEMU_VERSION)), Secondary(), 1100, 1);
		// tr: the heading of the screen a new version opens on, once: what it brings
		canvas.Text(Style({56, ui::Weight::Bold, 1.1f, -0.8f}), kSafeX, 122, Tr("What's new"), kText, 1100, 1);
		canvas.Text(Style({24, ui::Weight::Regular, 1.3f}), kSafeX, 206,
			Tr("The biggest update yet, at a glance. Its release notes on GitHub have everything."), Secondary(), 1920 - 2 * kSafeX, 2);
		canvas.PopAlpha();

		// the cards, each in one of the three systems' colours in turn
		static constexpr System kColours[] = {System::WiiU, System::N3ds, System::Nds};
		const float width = (1920 - 2 * kSafeX - (kColumns - 1) * kGap) / kColumns;
		const float textWidth = width - kPad - kIcon - 22 - kPad;
		const int count = (int)std::size(kNews);
		for (int i = 0; i < count; i++)
		{
			const int row = i / kColumns, column = i % kColumns;
			const float enter = Enter(1 + row);
			canvas.PushAlpha(enter);
			canvas.PushOffset(0, (1 - enter) * 24);
			const Box card{kSafeX + column * (width + kGap), kGridTop + row * (kCardHeight + kGap), width, kCardHeight};
			canvas.Rect(card, kRadiusCard, Surface());
			// the title and its line, as many lines as the card holds, in the card's middle
			const ui::TextBlock title = m_fonts.Layout(Style({26, ui::Weight::SemiBold, 1.15f}), Tr(kNews[i].title), textWidth, 2);
			const ui::TextStyle textStyle = Style({20, ui::Weight::Regular, 1.4f});
			const int lines = std::clamp((int)((kCardHeight - 2 * kPad + 4 - title.height - 8) / (textStyle.size * textStyle.lineHeight)), 1, 4);
			const ui::TextBlock text = m_fonts.Layout(textStyle, Tr(kNews[i].text), textWidth, lines);
			const float top = card.y + std::max(kPad, (kCardHeight - (title.height + 8 + text.height)) * 0.5f);
			const uint32_t colour = AccentOf(kColours[i % 3]);
			const Box icon{card.x + kPad, top, kIcon, kIcon};
			canvas.Rect(icon, kIcon / 2, ui::SetAlpha(colour, 0.18f));
			canvas.Draw(kNews[i].icon, icon.Inset(13), colour);
			const float tx = icon.Right() + 22;
			canvas.Text(title, tx, top, kText);
			canvas.Text(text, tx, top + title.height + 8, Secondary());
			canvas.PopOffset();
			canvas.PopAlpha();
		}
	}
}
