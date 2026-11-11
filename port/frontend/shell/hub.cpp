// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: a game's hub: everything about one game. Its cover large,
// its system, IDs and title, chips for what is known for sure (status, version, DLC, format, play
// time), Play, its graphic packs and the Game menu; GameTDB's description and facts; how it runs.
// L1 and R1 go to the games either side of it, as the Library orders them.

#include "shell_internal.h"
#include "../actions.h"
#include "../../app/boxart.h"
#include "../../app/emulator.h"

#include <algorithm>
#include <cmath>

namespace ps5shell
{
	using namespace ui::tokens;
	using namespace ps5actions;

	namespace
	{
		constexpr float kAboutTop = 590, kAboutHeight = 250, kAboutWidth = 640;
	}

	void Shell::HubOpen(int game, ScreenId from)
	{
		if (game < 0 || game >= (int)m_games.size())
			return;
		m_hubGame = game;
		if (from != ScreenId::Hub)
			m_hubFrom = ScreenId::Library;
		m_hubAction = 0;
		m_hubAbout = false;
		m_caseInspect = false;
		m_caseOpen = false;
		m_caseDisc = false;
		m_caseAngle = 0;
		m_caseZoom = 1;
		m_hubScroll.Snap(0);
		m_onBar = false;
		Show(ScreenId::Hub);
		m_screenAt = m_now;
	}

	void Shell::HubUpdate(const ui::Press& press)
	{
		if (m_hubGame < 0 || m_hubGame >= (int)m_games.size())
		{
			ShowTab(m_hubFrom);
			return;
		}
		const Button b = press.button;
		if (m_caseInspect)
		{
			switch (b)
			{
			case Button::Circle:
				if (m_caseDisc) { m_caseDisc = false; m_feedback.Play(ui::Cue::Back); break; }
				[[fallthrough]];
			case Button::Triangle: m_caseInspect = false; m_feedback.Play(ui::Cue::Back); break;
			case Button::Left: m_caseAngle -= 0.22f; m_feedback.Play(ui::Cue::Focus); break;
			case Button::Right: m_caseAngle += 0.22f; m_feedback.Play(ui::Cue::Focus); break;
			case Button::Up: m_caseZoom = std::min(1.5f, m_caseZoom + 0.12f); m_feedback.Play(ui::Cue::Focus); break;
			case Button::Down: m_caseZoom = std::max(0.65f, m_caseZoom - 0.12f); m_feedback.Play(ui::Cue::Focus); break;
			case Button::Square:
				m_caseDisc = !m_caseDisc;
				m_caseOpen = true;
				m_caseAngle = 0;
				m_caseZoom = 1;
				m_feedback.Play(ui::Cue::Select);
				break;
			case Button::Cross: m_caseOpen = !m_caseOpen; m_feedback.Play(ui::Cue::Select); break;
			default: break;
			}
			return;
		}
		// Play, Graphic packs (the Wii U's), Game settings, the Game menu
		const bool packs = IsWiiU() && m_status.coreReady;
		const int actions = packs ? 4 : 3;
		switch (b)
		{
		case Button::Triangle:
			if (!IsRetro() || IsPorpoise())
			{
				const auto& game = m_games[m_hubGame].entry.game;
				const auto system = BoxOf(game);
				ps5boxart::Fetch(system, {game.gameId}, ps5boxart::Art::Back);
				if (IsWiiU() || IsPorpoise()) ps5boxart::Fetch(system, {game.gameId}, ps5boxart::Art::Disc);
			}
			m_caseInspect = true;
			m_caseOpen = false;
			m_caseDisc = false;
			m_caseAngle = 0;
			m_caseZoom = 1;
			m_feedback.Play(ui::Cue::Select);
			break;
		case Button::Circle:
			if (m_hubAbout)
			{
				m_hubAbout = false;
				m_feedback.Play(ui::Cue::Back);
				break;
			}
			m_feedback.Play(ui::Cue::Back);
			if (m_hubFrom == ScreenId::Library)
			{
				m_libraryFocusTitle = m_games[m_hubGame].entry.game.titleId;
				LibraryRefresh();
			}
			ShowTab(m_hubFrom);
			break;
		case Button::Left:
		case Button::Right:
		{
			if (m_hubAbout)
				break;
			const int next = m_hubAction + (b == Button::Right ? 1 : -1);
			if (next < 0 || next >= actions)
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_hubAction = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Down:
			if (!m_hubAbout)
			{
				if (m_hubAboutHeight > kAboutHeight)
				{
					m_hubAbout = true;
					m_feedback.Play(ui::Cue::Focus);
				}
				else
					m_feedback.Play(ui::Cue::Edge, press.repeat);
				break;
			}
			m_hubScroll.target = std::min(m_hubScroll.target + 108, std::max(0.0f, m_hubAboutHeight - kAboutHeight));
			break;
		case Button::Up:
			if (m_hubAbout)
			{
				if (m_hubScroll.target <= 0)
				{
					m_hubAbout = false;
					m_feedback.Play(ui::Cue::Focus);
				}
				else
					m_hubScroll.target = std::max(0.0f, m_hubScroll.target - 108);
			}
			else
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			break;
		case Button::L1:
		case Button::R1:
		{
			// the game before or after, as the Library has them
			std::vector<int> order = m_shelf;
			if (order.empty())
				for (int i = 0; i < (int)m_games.size(); i++)
					order.push_back(i);
			auto it = std::find(order.begin(), order.end(), m_hubGame);
			int at = it == order.end() ? 0 : (int)(it - order.begin());
			at = (at + (b == Button::R1 ? 1 : (int)order.size() - 1)) % (int)order.size();
			HubOpen(order[at], m_hubFrom);
			m_feedback.Play(ui::Cue::Focus);
			break;
		}
		case Button::Cross:
			if (m_hubAbout)
			{
				m_hubAbout = false;
				m_feedback.Play(ui::Cue::Focus);
			}
			else if (m_hubAction == 0)
				Launch(m_hubGame);
			else if (packs && m_hubAction == 1)
			{
				m_feedback.Play(ui::Cue::Select);
				PacksOpen(m_hubGame, ScreenId::Hub);
			}
			else if (m_hubAction == actions - 2)
			{
				m_feedback.Play(ui::Cue::Select);
				GameSettingsOpen(m_hubGame);
			}
			else
				OpenGameMenu(m_hubGame);
			break;
		case Button::Options: OpenGameMenu(m_hubGame); break;
		default: break;
		}
	}

	void Shell::CaseDraw(Canvas& canvas, Game& game)
	{
		// A small projected case inside the native launcher. The cover is a textured face; the
		// tray, spine and disc are drawn by the same Vulkan UI kit as the rest of the shell.
		canvas.Rect({0, 0, 1920, 1080}, 0, 0xe80d0f18);
		canvas.Text(Style({28, ui::Weight::SemiBold, 1.0f}), kSafeX, 74, game.entry.game.name, 0xffffffff, 1700, 1);
		const auto system = BoxOf(game.entry.game);
		const std::string& id = game.entry.game.gameId;
		const bool discMedia = IsWiiU() || IsPorpoise();
		const bool remoteArt = !IsRetro() || IsPorpoise();
		const ui::Picture& backArt = m_images->Get(!remoteArt ? std::string() : ps5boxart::Path(system, id, ps5boxart::Art::Back));
		const ui::Picture& discArt = m_images->Get(!remoteArt ? std::string() : ps5boxart::Path(system, id, ps5boxart::Art::Disc));
		const float w = IsWiiU() ? 420.0f : 490.0f, h = IsWiiU() ? 610.0f : 500.0f;
		const float angle = m_caseAngle, cs = std::cos(angle), sn = std::sin(angle);
		auto project = [&](float x, float y, float z) -> ui::Point {
			const float rx = x * cs + z * sn, rz = z * cs - x * sn;
			const float scale = 850.0f / (850.0f - rz) * m_caseZoom;
			return {960 + rx * scale, 530 + y * scale};
		};
		auto plane = [&](float z) {
			return std::array<ui::Point, 4>{project(-w / 2, -h / 2, z), project(w / 2, -h / 2, z),
				project(w / 2, h / 2, z), project(-w / 2, h / 2, z)};
		};
		if (m_caseDisc && !discMedia)
		{
			// Every other side can inspect its media too, using the game's existing cover.
			const auto media = plane(0);
			const ui::Picture& art = Cover(game);
			if (cs >= 0) canvas.ImageQuad(art.texture, media, art.texture ? 0xffffffff : Accent());
			else canvas.ImageQuad(0, media, 0xff343946);
			if (!art.texture && cs > 0.6f)
				canvas.Text(Style({24, ui::Weight::SemiBold, 1.0f}), media[0].x + 24, media[0].y + 60,
					game.entry.game.name, 0xffffffff, std::max(1.0f, media[1].x - media[0].x - 48), 3);
			return;
		}
		if (m_caseDisc)
		{
			// Lift the disc out of its tray for an unobstructed view of either face.
			const float radius = 250 * m_caseZoom;
			const float width = std::max(3.0f, radius * std::fabs(cs));
			const Box disc{960 - width, 530 - radius, width * 2, radius * 2};
			canvas.EllipseGradient(disc, cs >= 0 ? Accent() : 0xffe8ebef, cs >= 0 ? 0xff344052 : 0xff8c99ac);
			const Box hub{960 - width * 0.14f, 530 - radius * 0.14f, width * 0.28f, radius * 0.28f};
			canvas.EllipseGradient(hub, 0xff0d0f18, 0xff0d0f18);
			if (cs > 0 && discArt.texture)
				canvas.Image(discArt.texture, disc, 0);
			if (!discArt.texture && cs > 0.6f)
				canvas.Text(Style({22, ui::Weight::SemiBold, 1.0f}), 960 - width * 0.7f,
					530 - radius * 0.5f, game.entry.game.name, 0xffffffff, width * 1.4f, 2);
			return;
		}
		const auto back = plane(-24), rim = plane(12);
		canvas.Shadow({700, 850, 520, 80}, 36, 12, 45, 0x99000000);
		if (cs < 0 && backArt.texture)
		{
			const std::array<ui::Point, 4> reverse{back[1], back[0], back[3], back[2]};
			canvas.ImageQuad(backArt.texture, reverse);
			return;
		}
		canvas.ImageQuad(0, back, 0xff30333d);
		// The tray edge has depth when the case turns.
		const int edge = sn >= 0 ? 1 : 0;
		const int other = edge == 1 ? 2 : 3;
		canvas.Triangle(back[edge].x, back[edge].y, rim[edge].x, rim[edge].y, rim[other].x, rim[other].y, 0xff242a37);
		canvas.Triangle(back[edge].x, back[edge].y, rim[other].x, rim[other].y, back[other].x, back[other].y, 0xff242a37);
		if (m_caseOpen && !discMedia)
		{
			const ui::Picture& art = Cover(game);
			const ui::Point centre = project(0, 0, 18);
			const float mediaWidth = w * 0.38f * m_caseZoom * std::max(0.03f, std::fabs(cs));
			const float mediaHeight = h * 0.36f * m_caseZoom;
			const Box media{centre.x - mediaWidth / 2, centre.y - mediaHeight / 2, mediaWidth, mediaHeight};
			canvas.Rect(media, 8, 0xff343946);
			if (art.texture && cs > 0) canvas.ImageCover(art.texture, art.width, art.height, media.Inset(6, 6), 4);
		}
		if (m_caseOpen && discMedia)
		{
			// A disc in the tray. Its apparent width follows the case's turn.
			const ui::Point centre = project(0, 0, 18);
			const float radius = std::min(w, h) * 0.36f * m_caseZoom;
			const float width = std::max(10.0f, radius * std::fabs(cs));
			const Box disc{centre.x - width, centre.y - radius, width * 2, radius * 2};
			canvas.EllipseGradient(disc, 0xffe8ebef, 0xfff7f7f4);
			canvas.EllipseGradient(disc.Inset(width * 0.04f, radius * 0.04f), 0xffeceef2, 0xff8c99ac);
			if (cs > 0 && discArt.texture) canvas.Image(discArt.texture, disc, 0);
			const Box hub{centre.x - width * 0.14f, centre.y - radius * 0.14f, width * 0.28f, radius * 0.28f};
			canvas.EllipseGradient(hub, 0xff252b39, 0xff252b39);
		}
		// The lid pivots on the left edge; its cover faces out when closed.
		const float opened = m_caseOpen ? -2.0f : 0.0f;
		auto lid = [&](float x, float y) {
			const float fromHinge = x + w / 2;
			return project(-w / 2 + fromHinge * std::cos(opened), y, 29 + fromHinge * std::sin(opened));
		};
		const std::array<ui::Point, 4> front{lid(-w / 2, -h / 2), lid(w / 2, -h / 2), lid(w / 2, h / 2), lid(-w / 2, h / 2)};
		const ui::Picture& art = Cover(game);
		const bool pictureFacesUs = front[1].x > front[0].x; // the hinge is vertical, so this is the projected winding
		canvas.ImageQuad(pictureFacesUs ? art.texture : 0, front,
			pictureFacesUs && art.texture ? 0xffffffff : ui::Mix(Accent(), 0xff1a1c27, 0.75f));
		canvas.Line(front[0].x, front[0].y, front[3].x, front[3].y, 5, 0xffd8dbe2);
		if (!art.texture && pictureFacesUs && !m_caseOpen && std::fabs(angle) < 0.6f)
		{
			canvas.Rect({front[0].x + 20, front[0].y + 20, front[1].x - front[0].x - 40, 48}, 2, Accent());
			canvas.Text(Style({24, ui::Weight::SemiBold, 1.0f}), front[0].x + 32, front[0].y + 92,
				game.entry.game.name, 0xffffffff, front[1].x - front[0].x - 64, 3);
		}
	}

	void Shell::HubDraw(Canvas& canvas)
	{
		if (m_hubGame < 0 || m_hubGame >= (int)m_games.size())
			return;
		Game& game = m_games[m_hubGame];
		const auto& g = game.entry.game;
		WantBackdrop(&game);
		if (m_caseInspect)
		{
			CaseDraw(canvas, game);
			return;
		}

		// the games either side, named at the top
		std::vector<int> order = m_shelf;
		if (order.empty())
			for (int i = 0; i < (int)m_games.size(); i++)
				order.push_back(i);
		if (order.size() > 1)
		{
			auto it = std::find(order.begin(), order.end(), m_hubGame);
			const int at = it == order.end() ? 0 : (int)(it - order.begin());
			const Game& before = m_games[order[(at + (int)order.size() - 1) % order.size()]];
			const Game& after = m_games[order[(at + 1) % order.size()]];
			canvas.PushAlpha(Enter(0));
			const ui::TextStyle side = Style({24, ui::Weight::Medium, 1.0f});
			canvas.Draw(Icon::L1, {kSafeX, 52, 34, 30}, Tertiary());
			canvas.Text(side, kSafeX + 46, 54, before.entry.game.name, Tertiary(), 560, 1);
			canvas.Draw(Icon::R1, {1920 - kSafeX - 34, 52, 34, 30}, Tertiary());
			const ui::TextBlock afterName = m_fonts.Layout(side, after.entry.game.name, 560, 1);
			canvas.Text(afterName, 1920 - kSafeX - 46 - afterName.width, 54, Tertiary());
			canvas.PopAlpha();
		}

		// the cover
		const bool wiiu = IsWiiU();
		const Box cover{kSafeX, 150, 420, wiiu ? 588.0f : 382.0f};
		canvas.PushAlpha(Enter(1));
		canvas.PushOffset((1 - Enter(1)) * -30, 0);
		DrawCover(canvas, game, cover, false, 0);
		canvas.PopOffset();
		canvas.PopAlpha();

		// its kicker, title and chips
		const float x = 596;
		canvas.PushAlpha(Enter(2));
		canvas.PushOffset(0, (1 - Enter(2)) * 24);
		float kx = x + Badge(canvas, x, 150, m_side) + 18;
		// a DS game has no title ID of its own (the one it is given stays out of sight)
		const std::string kicker = Join({g.gameId, game.known ? game.info.region : "", IsDs() ? "" : ShownId(g)}, " · ");
		canvas.Text(Style({18, ui::Weight::SemiBold, 1.9f, 3.5f, true}), kx, 150, kicker, Secondary(), 1920 - kSafeX - kx, 1);
		ui::TextStyle display = Style(kDisplayStyle);
		display.size = m_fonts.Fit(display, g.name, 1228, 2, display.size - 16, 8);
		const ui::TextBlock title = canvas.Text(display, x, 196, g.name, kText, 1228, 2);
		float cy = 196 + std::max(title.height, display.size * 1.04f) + 18;
		float cx = x;
		if (game.report)
			cx += Chip(canvas, cx, cy, TrC("status", game.report->status), ps5compat::Kind(game.report->status)) + 10;
		if (wiiu)
		{
			// tr: a chip: the game's update, by its version ("Update v208")
			cx += Chip(canvas, cx, cy, g.hasUpdate ? TrF("Update v{0}", g.version) : fmt::format("v{}", g.version)) + 10;
			if (g.dlcCount)
				cx += Chip(canvas, cx, cy, Tr("DLC")) + 10;
		}
		else if (IsSwitch() && (g.hasUpdate || g.dlcCount))
		{
			// a Switch game's update, by the version it shows ("Update v1.2.0"), and its DLC: those Eden
			// will take with it (addons.h)
			if (g.hasUpdate && !g.versionName.empty())
				cx += Chip(canvas, cx, cy, TrF("Update v{0}", g.versionName)) + 10;
			if (g.dlcCount)
				cx += Chip(canvas, cx, cy, TrP(g.dlcCount, "{0} DLC", "{0} DLC")) + 10;
		}
		else if (!g.publisher.empty() && !game.known)
			cx += Chip(canvas, cx, cy, g.publisher) + 10;
		if (!g.format.empty())
			cx += Chip(canvas, cx, cy, g.format) + 10;
		if (g.format == "Switch 2")
			cx += Chip(canvas, cx, cy, Tr("Experimental"), "warn") + 10;
		if (!Played(game).empty())
			cx += Chip(canvas, cx, cy, Played(game)) + 10;
		if (game.entry.favourite)
			Chip(canvas, cx, cy, Tr("Favourite"));

		// the actions
		const bool packs = wiiu && m_status.coreReady;
		const int actions = packs ? 4 : 3;
		m_hubAction = std::clamp(m_hubAction, 0, actions - 1);
		const float ay = cy + 72;
		Box b = PillButton(canvas, x, ay, Tr("Play"), true, !m_hubAbout && m_hubAction == 0, Icon::Play, true, 72, 420);
		float bx = b.Right() + 20;
		if (packs)
		{
			if (game.packsOn < 0 && m_status.coreReady)
				game.packsOn = ps5emu::EnabledGraphicPackCount(g.titleId);
			b = PillButton(canvas, bx, ay, game.packsOn > 0 ? TrP(game.packsOn, "Graphic packs · {0} on", "Graphic packs · {0} on") : std::string(Tr("Graphic packs")),
				false, !m_hubAbout && m_hubAction == 1, Icon::Play, false, 72, 620);
			bx = b.Right() + 20;
		}
		const bool own = m_settings.games.count(ps5settings::GameKey(ps5launcher::SideName(m_side), g.titleId)) > 0;
		b = PillButton(canvas, bx, ay, own ? Tr("Game settings · custom") : Tr("Game settings"), false, !m_hubAbout && m_hubAction == actions - 2);
		bx = b.Right() + 20;
		IconButton(canvas, bx, ay, Icon::More, !m_hubAbout && m_hubAction == actions - 1);
		canvas.PopOffset();
		canvas.PopAlpha();

		// About: GameTDB's description, which Up and Down scroll
		canvas.PushAlpha(Enter(3));
		canvas.PushOffset(0, (1 - Enter(3)) * 24);
		const float aboutTop = std::max(kAboutTop, ay + 72 + 52);
		// tr: the heading over a game's description
		canvas.Text(Style(kOverlineStyle), x, aboutTop - 34, TrC("game", "About"), Secondary(), kAboutWidth, 1);
		std::string synopsis = game.known ? game.info.synopsis : std::string();
		// (a Switch game has no ID on its box in its files: GameTDB finds it by its name, or does not)
		if (synopsis.empty())
			synopsis = g.gameId.empty() && !IsSwitch() ? Tr("No description: this dump has no game ID.") :
														 Tr("GameTDB has no description of this game yet.");
		const ui::TextStyle body = Style({26, ui::Weight::Regular, 1.46f});
		const ui::TextBlock about = m_fonts.Layout(body, synopsis, kAboutWidth);
		m_hubAboutHeight = about.height;
		const float aboutHeight = std::min(kAboutHeight, 1080 - 120 - aboutTop);
		m_hubScroll.omega = kScrollOmega;
		m_hubScroll.Update(m_dt);
		const Box aboutBox{x - 16, aboutTop - 8, kAboutWidth + 32, aboutHeight + 16};
		canvas.PushClip(aboutBox);
		canvas.Text(about, x, aboutTop - m_hubScroll.value, Secondary());
		canvas.PopClip();
		if (m_hubAbout)
			Focus(aboutBox, 16);

		// the facts, and how it runs
		const float fx = 1284;
		std::vector<std::pair<const char*, std::string>> facts;
		if (game.known)
		{
			// tr: a game's facts, from GameTDB
			facts = {{Tr("Developer"), game.info.developer}, {Tr("Released"), ps5gameinfo::ReleaseDate(game.info.released)},
				{Tr("Genre"), ps5gameinfo::Genres(game.info.genre, 2)}, {Tr("Rating"), game.info.rating}, {Tr("Publisher"), game.info.publisher},
				{Tr("Players"), game.info.players > 0 ? std::to_string(game.info.players) : ""}};
			std::erase_if(facts, [](const auto& fact) { return fact.second.empty(); });
		}
		if (facts.empty() && !IsDs() && !ShownId(g).empty())
			facts.push_back({Tr("Title ID"), ShownId(g)});
		if (IsSwitch())
			facts.push_back({Tr("Format"), g.format});
		for (int i = 0; i < (int)facts.size() && i < 4; i++)
		{
			const float fy = aboutTop - 32 + (i / 2) * 82;
			const float fxi = fx + (i % 2) * 284;
			canvas.Text(Style({20, ui::Weight::Medium, 1.2f}), fxi, fy, facts[i].first, Tertiary(), 270, 1);
			canvas.Text(Style({26, ui::Weight::Medium, 1.2f}), fxi, fy + 28, facts[i].second, kText, 270, 1);
		}
		if (game.report)
		{
			const std::string kind = ps5compat::Kind(game.report->status);
			const uint32_t colour = kind == "good" ? kGood : kind == "warn" ? kWarn : kind == "bad" ? kBad : kText;
			const Box card{fx, aboutTop + 154, 540, 172};
			canvas.Rect(card, kRadiusCard, ui::SetAlpha(colour, 0.07f));
			canvas.Ring(card, kRadiusCard, 1.5f, ui::SetAlpha(colour, 0.25f));
			canvas.Draw(kind == "good" ? Icon::Check : Icon::Warning, {card.x + 24, card.y + 22, 28, 28}, colour);
			const std::string heading = kind == "good" ? std::string(Tr("Runs well on PS5")) : TrC("status", game.report->status);
			canvas.Text(Style({26, ui::Weight::SemiBold, 1.2f}), card.x + 62, card.y + 22, heading, ui::Mix(colour, 0xffffffff, 0.3f), card.w - 86, 1);
			// tr: {0} is a status from the compatibility list, such as Playable
			const std::string notes = game.report->notes.empty() ? TrF("The compatibility list has it as “{0}”.", TrC("status", game.report->status)) :
																   TrC("compatibility note", game.report->notes);
			canvas.Text(Style({21, ui::Weight::Regular, 1.4f}), card.x + 24, card.y + 66, notes, Secondary(), card.w - 48, 3);
		}
		canvas.PopOffset();
		canvas.PopAlpha();
	}
}
