// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM's UI kit: what the UI answers with besides the picture (feedback.h).

#include "feedback.h"
#include "../frontend/sound.h"
#include "../ps5/pad.h"

namespace ui
{
	void Feedback::Play(Cue cue, bool repeat)
	{
		using ps5sound::Effect;
		switch (cue)
		{
		// 6.0: a step of the focus is felt as well as heard, a tick so short it is more click than rumble (not
		// while a direction is held: the repeats would buzz), so moving about has weight
		case Cue::Focus:
			ps5sound::Play(Effect::Move);
			if (!repeat)
				Rumble(0, 38, 0.022);
			break;
		case Cue::Toggle:
		case Cue::Select:
		case Cue::Sheet:
			ps5sound::Play(Effect::Select);
			Rumble(0, 70, 0.035);
			break;
		case Cue::Notify: ps5sound::Play(Effect::Select); break;
		case Cue::Back: ps5sound::Play(Effect::Back); break;
		case Cue::Denied:
			ps5sound::Play(Effect::Denied);
			Rumble(0, 90, 0.05);
			break;
		case Cue::Edge:
			// the end of a list says nothing: a sound each time it is reached was heard as nagging (6.0); a soft
			// bump in the hand instead, once, not while held
			if (!repeat)
				Rumble(40, 0, 0.03);
			break;
		case Cue::Launch:
			ps5sound::Play(Effect::Launch);
			Rumble(150, 150, 0.12);
			break;
		case Cue::Hold:
			ps5sound::Play(Effect::Select);
			Rumble(150, 150, 0.12);
			break;
		}
	}

	void Feedback::Rumble(uint8_t large, uint8_t small, double seconds)
	{
		for (int player = 0; player < ps5pad::kMaxPlayers; player++)
			if (ps5pad::IsConnected(player))
				ps5pad::SetVibration(player, large, small);
		m_rumbling = true;
		m_rumbleUntil = m_now + seconds;
	}

	void Feedback::SetLight(uint32_t colour)
	{
		for (int player = 0; player < ps5pad::kMaxPlayers; player++)
			if (ps5pad::IsConnected(player))
				ps5pad::SetLightBar(player, colour & 255, colour >> 8 & 255, colour >> 16 & 255);
	}

	void Feedback::Update(double now)
	{
		m_now = now;
		if (m_rumbling && now >= m_rumbleUntil)
			Stop();
	}

	void Feedback::Stop()
	{
		if (!m_rumbling)
			return;
		m_rumbling = false;
		for (int player = 0; player < ps5pad::kMaxPlayers; player++)
			if (ps5pad::IsConnected(player))
				ps5pad::SetVibration(player, 0, 0);
	}
}
