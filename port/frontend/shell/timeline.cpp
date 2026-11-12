// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the Timeline, a museum of the library (6.0). It is a stop of its own in the console lists, after
// the last console (the Classic carousel's, Orbit's dial), whose card opens it. Every console's games on one
// shelf, oldest console to newest, each game drawn as what it came on, in that thing's true shape: GameTDB's
// picture of its disc or card where it has one (the GameCube's, Wii's and Wii U's discs, the DS's, 3DS's and
// Switch's cards), the LaunchBox Games Database's of its cartridge from its own region (a Japanese NES game's
// Famicom cartridge), else its cartridge, Disk Card or disc drawn with its box art on the label. Each kind keeps its
// own proportions, and the kinds their sizes against each other, brought a little closer together (a Switch
// card stays big enough to see beside an NES cartridge). Over the shelves, a ruler of the years in their eras,
// every console's mark, and the player's own games by the year each came out; under them, the game in focus on
// its label, and its console's placard on the slot it goes in. L1 and R1 step consoles, Up and Down eras, Cross
// puts the game in the slot and starts it, Circle goes back. Its own light ground and ink on every theme. A console
// with no games is a slim bay with its thing drawn faint; the era in focus has its line over the shelves.

#include "shell_internal.h"
#include "../../eden/library.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <map>

namespace ps5shell
{
	using namespace ui::tokens;

	namespace
	{
		constexpr uint32_t Rgb(uint32_t rgb)
		{
			return 0xff000000u | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff);
		}
		constexpr uint32_t kInk = Rgb(0x15171c), kInk2 = Rgb(0x3b404a), kInk3 = Rgb(0x4a4f5a), kPaper = Rgb(0xe7e8eb);
		constexpr float kLeft = 96, kView = 1728, kShelfTop = 300, kShelfBottom = 618, kShelfWidth = 300;
		constexpr float kEmptyWidth = 168; // a shelf with no games: a slim bay, its console's thing drawn faint
		constexpr int kFirstYear = 1980, kYears = 46;

		// what a game came on: its width and height in millimetres, and its name on the placards
		enum MediaKind
		{
			kWatch, kNesCart, kVsBoard, kDiskCard, kGbPak, kGbcPak, kSnesCart, kMemoryPak, kVbPak, kN64Cart, kGbaPak, kMiniDisc,
			kMiniPak, kIqueCard, kDsCard, kWiiDisc, k3dsCard, kWiiUDisc, kSwitchCard, kSwitch2Card
		};
		struct Media
		{
			float wide, tall;
			const char* what;
		};
		// tr: what a game came on, on the Timeline's placards
		constexpr Media kMedia[] = {
			{110, 110, TrMark("Built in")}, // a slot square enough for a Multi Screen unit open, the drawn one 110 x 67
			{120, 133, TrMark("Cartridge")},
			{150, 110, TrMark("Arcade board")},
			{76, 90, TrMark("Disk Card")},
			{57, 65, TrMark("Game Pak")},
			{57, 65, TrMark("Game Pak")},
			{120, 88, TrMark("Cartridge")},
			{54, 40, TrMark("Memory Pak")},
			{60, 72, TrMark("Game Pak")},
			{116, 75, TrMark("Cartridge")},
			{57, 35, TrMark("Game Pak")},
			{80, 80, TrMark("8 cm disc")},
			{25, 27, TrMark("Cartridge")},
			{45, 64, TrMark("iQue Card")},
			{33, 35, TrMark("Game Card")},
			{120, 120, TrMark("12 cm disc")},
			{35, 35, TrMark("Game Card")},
			{120, 120, TrMark("12 cm disc")},
			{21, 31, TrMark("Game Card")},
			{21, 31, TrMark("Game Card")},
		};
		bool Round(int kind) { return kind == kMiniDisc || kind == kWiiDisc || kind == kWiiUDisc; }

		// Where the drawn thing stands in its slot: all of it, but a Game & Watch's, 110 x 67 at its foot
		Box BodyBox(int kind, const Box& slot)
		{
			if (kind != kWatch)
				return slot;
			const float h = slot.w * 67 / 110;
			return {slot.x, slot.Bottom() - h, slot.w, h};
		}

		// its size on the shelf, in the layout's pixels: as long as it is, to the power 0.6 (the NES's 133 mm
		// 160 high, a Switch card's 31 mm 67)
		void ShelfSize(int kind, float& w, float& h)
		{
			const Media& m = kMedia[kind];
			const float longest = std::max(m.wide, m.tall);
			const float perMm = 8.5f * std::pow(longest, 0.6f) / longest;
			w = m.wide * perMm;
			h = m.tall * perMm;
		}

		// the consoles, by the year each came out, and a line for each one's placard
		struct Era
		{
			System side;
			int year;
			const char* brief;
			int media;
			const char* placard;
		};
		// tr: a console's placard on the Timeline, one short line on what it was
		constexpr Era kEras[] = {
			{System::GameWatch, 1980, "G&W", kWatch, TrMark("One game in each, and a clock. The cross pad began here.")},
			{System::Nes, 1983, "NES", kNesCart, TrMark("The Famicom, then the NES: it brought games back to living rooms.")},
			{System::VsSystem, 1984, "VS.", kVsBoard, TrMark("Arcade cabinets that ran NES games, some with two screens.")},
			{System::Fds, 1986, "FDS", kDiskCard, TrMark("Games on disks, written again at shop kiosks in Japan.")},
			{System::Gb, 1989, "GB", kGbPak, TrMark("Four shades of green on AA batteries, and Tetris for two by cable.")},
			{System::Snes, 1990, "SNES", kSnesCart, TrMark("Mode 7, and chips in the cartridges that drew in 3D.")},
			{System::Sgb, 1994, "SGB", kGbPak, TrMark("Game Boy games on the TV, with colours and borders.")},
			{System::Satellaview, 1995, "BS-X", kMemoryPak, TrMark("Games sent by satellite to Super Famicoms in Japan.")},
			{System::VirtualBoy, 1995, "VB", kVbPak, TrMark("3D in red and black, in a visor on a stand.")},
			{System::N64, 1996, "N64", kN64Cart, TrMark("The analog stick, the Rumble Pak and four controller ports.")},
			{System::Gbc, 1998, "GBC", kGbcPak, TrMark("The Game Boy in colour, and it still plays the old games.")},
			{System::Gba, 2001, "GBA", kGbaPak, TrMark("A 32-bit Game Boy, with shoulder buttons.")},
			{System::GameCube, 2001, "GC", kMiniDisc, TrMark("8 cm discs, and a handle on the back.")},
			{System::PokemonMini, 2001, "mini", kMiniPak, TrMark("The smallest cartridges, with rumble and a shake sensor.")},
			{System::Ique, 2003, "iQue", kIqueCard, TrMark("The N64 inside a controller, for China, its games on a card.")},
			{System::Nds, 2004, "DS", kDsCard, TrMark("Two screens, the lower one touched with a stylus.")},
			{System::Wii, 2006, "Wii", kWiiDisc, TrMark("A remote to point and swing, in every hand.")},
			{System::N3ds, 2011, "3DS", k3dsCard, TrMark("3D without glasses, and a camera that sees in 3D too.")},
			{System::WiiU, 2012, "Wii U", kWiiUDisc, TrMark("A second screen in the controller.")},
			{System::Switch, 2017, "Switch", kSwitchCard, TrMark("On the TV or in your hands: the same console.")},
			{System::Switch2, 2025, "Switch 2", kSwitch2Card, TrMark("A bigger Switch, with Joy-Con that work as a mouse.")},
		};

		// the museum's rooms: the eras, from the year each began, and a line on each over the shelves
		struct Room
		{
			int from;
			const char* name;
			const char* line;
		};
		// tr: an era on the Timeline's ruler (the years its consoles came out in), and its one line over the shelves
		constexpr Room kRooms[] = {{1980, TrMark("Pocket games"), TrMark("Games on LCD screens, small enough for a pocket.")},
			{1983, TrMark("8-bit"), TrMark("Cartridges brought games home, and the screens began to scroll.")},
			{1990, TrMark("16-bit"), TrMark("More colours, richer sound, and Mode 7.")},
			{1996, TrMark("3D"), TrMark("Worlds in polygons, the analog stick and rumble.")},
			{2001, TrMark("Discs and pockets"), TrMark("Small discs at home, a 32-bit Game Boy on the go.")},
			{2006, TrMark("Motion"), TrMark("Games played by pointing, swinging and touching.")},
			{2012, TrMark("HD"), TrMark("High definition, and a screen in the controller.")},
			{2017, TrMark("Hybrid"), TrMark("One console, on the TV or in your hands.")}};
		constexpr int kRoomCount = (int)(sizeof(kRooms) / sizeof(kRooms[0]));
		int RoomOf(int year)
		{
			int room = 0;
			while (room + 1 < kRoomCount && kRooms[room + 1].from <= year)
				room++;
			return room;
		}

		const Era* EraOf(System side)
		{
			for (const Era& era : kEras)
				if (era.side == side)
					return &era;
			return nullptr;
		}

		// GameTDB's folder for a side's discs and cards; false: it has none of them
		bool MediaSystem(System side, ps5boxart::System& system)
		{
			switch (side)
			{
			case System::WiiU: system = ps5boxart::System::WiiU; return true;
			case System::N3ds: system = ps5boxart::System::N3ds; return true;
			case System::Nds: system = ps5boxart::System::Nds; return true;
			case System::Switch:
			case System::Switch2: system = ps5boxart::System::Switch; return true;
			case System::GameCube:
			case System::Wii: system = ps5boxart::System::GameCubeWii; return true;
			default: return false;
			}
		}

		// the LaunchBox Games Database's name for a libretro side's cartridges (ps5gameinfo::FindCart); empty: none
		const char* CartSystem(System side)
		{
			switch (side)
			{
			case System::Nes: return "nes";
			case System::Snes: return "snes";
			case System::Gb:
			case System::Sgb: return "gb";
			case System::Gbc: return "gbc";
			case System::Gba: return "gba";
			case System::N64: return "n64";
			case System::VirtualBoy: return "vb";
			case System::PokemonMini: return "mini";
			case System::GameWatch: return "gw"; // each game's own unit
			default: return "";
			}
		}

		// The drawn thing, under its label: its body in its own colours, in millimetres scaled to the box. What
		// the label is on, for the box art (round on a disc)
		struct Label
		{
			Box box;
			float radius;
		};
		Label DrawBody(Canvas& canvas, int kind, const Box& slot)
		{
			const Box b = BodyBox(kind, slot);
			const float u = b.w / kMedia[kind].wide; // the drawing's pixels a millimetre
			auto at = [&](float x, float y, float w, float h) { return Box{b.x + x * u, b.y + y * u, w * u, h * u}; };
			auto rect = [&](float x, float y, float w, float h, float r, uint32_t colour) { canvas.Rect(at(x, y, w, h), r * u, colour); };
			auto label = [&](float x, float y, float w, float h, float r) { return Label{at(x, y, w, h), r * u}; };
			auto darker = [](uint32_t colour, float by) { return ui::Mix(colour, 0xff000000, by); };
			switch (kind)
			{
			case kWatch:
			{
				// the faceplate, its screen between the two buttons, Game A, Game B and Time at the right
				const uint32_t plate = Rgb(0xbfa871);
				canvas.LinearGradient(b, 4 * u, ui::Mix(plate, 0xffffffff, 0.3f), darker(plate, 0.15f), b.x, b.y, b.x, b.Bottom());
				rect(26, 8, 58, 46, 2, Rgb(0x2b2b2e));
				for (float x : {6.0f, 92.0f})
					rect(x, 40, 12, 12, 6, Rgb(0xb3262a));
				for (float y : {9.0f, 15.0f, 21.0f})
					rect(92, y, 12, 3, 1.5f, Rgb(0x2b2b2e));
				return label(30, 11, 50, 40, 1);
			}
			case kNesCart:
			{
				// grey, its grip's grooves along the top, the label in its recess
				const uint32_t body = Rgb(0x8d8d92);
				rect(0, 0, 120, 133, 3, body);
				for (float y = 5; y < 28; y += 4)
					rect(6, y, 108, 1.6f, 0.8f, darker(body, 0.2f));
				rect(9, 33, 102, 90, 2, darker(body, 0.1f));
				rect(0, 127, 120, 6, 3, darker(body, 0.12f));
				return label(12, 36, 96, 84, 1.5f);
			}
			case kVsBoard:
			{
				// a green board: its chips, its gold fingers at the foot, the game's sticker
				const uint32_t board = Rgb(0x2e6a3c);
				rect(0, 0, 150, 110, 3, board);
				for (float y : {20.0f, 40.0f, 60.0f})
					rect(4, y, 6, 1, 0.5f, ui::Mix(board, 0xffffffff, 0.25f));
				for (float x : {14.0f, 44.0f, 74.0f, 104.0f})
					rect(x, 80, x > 100 ? 32.0f : 24.0f, 11, 1, Rgb(0x1a1a1c));
				for (float x = 8; x < 142; x += 5)
					rect(x, 99, 3, 11, 0.5f, Rgb(0xd4a64a));
				return label(14, 9, 122, 64, 2);
			}
			case kDiskCard:
			{
				// yellow, its label at the top, the shutter the disk shows through below
				const uint32_t body = Rgb(0xf2c230);
				rect(0, 0, 76, 90, 2.5f, body);
				rect(0, 52, 76, 1.4f, 0, darker(body, 0.12f));
				rect(25, 57, 26, 29, 2, darker(body, 0.45f));
				rect(29, 61, 18, 21, 1, Rgb(0x3a3324));
				return label(6, 5, 64, 44, 1.5f);
			}
			case kGbPak:
			case kGbcPak:
			{
				// grey (the Game Boy Color's own, smoky black): grooves along the top, the label in its recess, the
				// arrow under it
				const uint32_t body = kind == kGbPak ? Rgb(0xa7a6a3) : Rgb(0x2f3036);
				rect(0, 0, 57, 65, 2.5f, body);
				for (float y = 2.5f; y < 10; y += 2.2f)
					rect(4, y, 49, 1, 0.5f, darker(body, 0.22f));
				rect(4, 11, 49, 44, 1.5f, darker(body, 0.08f));
				const Box arrow = at(25.5f, 57.5f, 6, 4);
				canvas.Triangle(arrow.x, arrow.y, arrow.Right(), arrow.y, arrow.CentreX(), arrow.Bottom(), darker(body, 0.3f));
				return label(6, 13, 45, 40, 1);
			}
			case kSnesCart:
			{
				// wide and rounded, its label in the middle, a ridged grip each side
				const uint32_t body = Rgb(0xb9b8bd);
				rect(0, 0, 120, 88, 8, body);
				for (float x : {5.0f, 102.0f})
				{
					rect(x, 40, 13, 40, 3, darker(body, 0.08f));
					for (float y = 43; y < 78; y += 3.5f)
						rect(x + 2, y, 9, 1.2f, 0.6f, darker(body, 0.22f));
				}
				rect(0, 82, 120, 6, 3, darker(body, 0.12f));
				return label(22, 8, 76, 58, 2);
			}
			case kMemoryPak:
			{
				const uint32_t body = Rgb(0x6d6e75);
				rect(0, 0, 54, 40, 3, body);
				rect(0, 34, 54, 6, 3, darker(body, 0.2f));
				return label(6, 5, 42, 26, 1.5f);
			}
			case kVbPak:
			{
				// black, the label in a red frame, grooves under it
				const uint32_t body = Rgb(0x2b2b2e);
				rect(0, 0, 60, 72, 3, body);
				rect(5, 5, 50, 44, 2, Rgb(0xb3121c));
				for (float y = 54; y < 68; y += 3)
					rect(10, y, 40, 1.2f, 0.6f, ui::Mix(body, 0xffffffff, 0.1f));
				return label(7, 7, 46, 40, 1);
			}
			case kN64Cart:
			{
				// dark grey, its shoulders, the label over its ribs
				const uint32_t body = Rgb(0x3f4048);
				rect(0, 0, 116, 75, 7, body);
				rect(0, 0, 116, 14, 7, ui::Mix(body, 0xffffffff, 0.06f));
				for (float x = 22; x < 96; x += 4)
					rect(x, 57, 1.6f, 14, 0.8f, darker(body, 0.25f));
				return label(18, 6, 80, 46, 3);
			}
			case kGbaPak:
			{
				const uint32_t body = Rgb(0x9fa0a4);
				rect(0, 0, 57, 35, 3.5f, body);
				rect(18, 30, 21, 3, 1.5f, darker(body, 0.18f));
				return label(5, 4, 47, 24, 1.5f);
			}
			case kMiniPak:
			{
				const uint32_t body = Rgb(0x4a4a52);
				rect(0, 0, 25, 27, 2, body);
				rect(4, 21, 17, 1, 0.5f, ui::Mix(body, 0xffffffff, 0.15f));
				return label(3, 3, 19, 16, 1);
			}
			case kIqueCard:
			{
				const uint32_t body = Rgb(0xe9e9ee);
				rect(0, 0, 45, 64, 3, body);
				for (float x = 8; x < 37; x += 4)
					rect(x, 53, 2.4f, 8, 0.5f, Rgb(0xc9a24a));
				return label(4, 4, 37, 42, 1.5f);
			}
			case kDsCard:
			case k3dsCard:
			{
				// the DS's dark card, the 3DS's light one with the tab on its side
				const bool n3ds = kind == k3dsCard;
				const uint32_t body = n3ds ? Rgb(0xe3e5ea) : Rgb(0x3c3d42);
				rect(0, 0, 33, 35, 1.5f, body);
				if (n3ds)
					rect(31.5f, 6, 3.5f, 10, 1, body);
				rect(0, 0, 33, 3, 1.5f, n3ds ? darker(body, 0.06f) : ui::Mix(body, 0xffffffff, 0.08f));
				return label(2.5f, 4, 28, 26, 1);
			}
			case kSwitchCard:
			case kSwitch2Card:
			{
				// small: the Switch's dark, the Switch 2's red
				const uint32_t body = kind == kSwitchCard ? Rgb(0x2b2d33) : Rgb(0xc8102e);
				rect(0, 0, 21, 31, 2, body);
				rect(6, 27, 9, 1.4f, 0.7f, ui::Mix(body, 0xffffffff, 0.2f));
				return label(2, 2, 17, 23, 1);
			}
			default:
			{
				// a disc: silver, its label to the edge
				const float d = kMedia[kind].wide;
				canvas.LinearGradient(b, b.w / 2, Rgb(0xe6e8ec), Rgb(0xaeb2bb), b.x, b.y, b.Right(), b.Bottom());
				return {at(1, 1, d - 2, d - 2), (d - 2) / 2 * u};
			}
			}
		}

		// what goes over the label: a disc's clear middle and its hole, the NES label's black strip, the
		// Switches' band
		void DrawOver(Canvas& canvas, int kind, const Box& b)
		{
			const float u = b.w / kMedia[kind].wide;
			auto at = [&](float x, float y, float w, float h) { return Box{b.x + x * u, b.y + y * u, w * u, h * u}; };
			if (Round(kind))
			{
				const float d = kMedia[kind].wide, hub = 17, hole = 7.5f;
				const Box clear = at(d / 2 - hub, d / 2 - hub, hub * 2, hub * 2);
				canvas.Rect(clear, hub * u, 0x66ffffff);
				canvas.Ring(clear, hub * u, 0.6f * u, 0x33000000);
				const Box middle = at(d / 2 - hole, d / 2 - hole, hole * 2, hole * 2);
				canvas.Rect(middle, hole * u, kPaper);
				canvas.Ring(middle, hole * u, 0.8f * u, Rgb(0x9da1aa));
				canvas.Ring(b, b.w / 2, (kind == kWiiUDisc ? 1.6f : 0.8f) * u, Rgb(0x9da1aa)); // the Wii U's rounded rim
			}
			else if (kind == kNesCart)
				canvas.Rect(at(12, 111, 96, 9), 0, Rgb(0x141414));
			else if (kind == kSwitchCard || kind == kSwitch2Card)
				canvas.Rect(at(2, 2, 17, 4.5f), 1 * u, kind == kSwitchCard ? Rgb(0xe60012) : Rgb(0x26262b));
		}

		std::string Joined(std::initializer_list<std::string> parts)
		{
			std::string out;
			for (const std::string& part : parts)
				if (!part.empty())
					out += (out.empty() ? "" : " · ") + part;
			return out;
		}
	}

	// -- the Timeline's stop in the console lists ------------------------------------------------------

	void Shell::TimelineStopColours(uint32_t dots[3]) const
	{
		// the oldest console shown, one from the middle, the newest: the colours of the consoles it holds
		std::vector<System> sides;
		for (const Era& era : kEras)
			if (SideShown(era.side))
				sides.push_back(era.side);
		if (sides.empty())
			sides.push_back(m_side);
		const size_t last = sides.size() - 1;
		const System picks[3] = {sides[0], sides[last / 2], sides[last]};
		for (int i = 0; i < 3; i++)
			dots[i] = AccentOf(picks[i]) | 0xff000000;
	}

	void Shell::TimelineStop(bool on)
	{
		m_timelineStop = on;
		if (!on)
			return;
		m_orbitHome = false;
		m_feedback.Play(ui::Cue::Focus);
		TimelineBuild(); // its card shows what the Timeline holds
	}

	void Shell::TimelineStopUpdate(const ui::Press& press)
	{
		switch (press.button)
		{
		case Button::Cross: TimelineOpen(); break;
		case Button::Up: FocusBar(); break;
		case Button::R3:
			if (Orbit())
				OrbitHomeOpen(true);
			break;
		case Button::Circle:
			// back to the console it was turned from
			m_timelineStop = false;
			m_feedback.Play(ui::Cue::Back);
			break;
		case Button::Left:
		case Button::Right:
		case Button::Down: m_feedback.Play(ui::Cue::Edge, press.repeat); break;
		default: break;
		}
	}

	void Shell::TimelineStopDraw(Canvas& canvas)
	{
		WantBackdrop(nullptr);
		const bool orbit = Orbit();
		if (orbit)
			DrawDial(canvas);
		DrawBar(canvas, true);
		int games = 0, consoles = 0;
		for (const TimelineShelf& shelf : m_shelves)
		{
			games += (int)shelf.games.size();
			consoles++;
		}
		// beside Orbit's dial, as its consoles' names are; under Classic's bar, as the Library's cards are
		const float x = orbit ? 680.0f : kSafeX, width = 1920 - kSafeX - x;
		const float top = orbit ? 150.0f : 236.0f;
		canvas.PushAlpha(Enter(1));
		if (orbit)
			DrawWordmark(canvas, x, 72, 38, kText);
		// tr: the overline of the Timeline's card in the console lists: it is the library's museum
		canvas.Text(Style(kOverlineStyle), x, top, Tr("Museum"), Secondary(), width, 1);
		const ui::TextBlock title = canvas.Text(Style({92, ui::Weight::Bold, 1.0f, -1.0f}), x, top + 32, Tr("Timeline"), kText, width, 1);
		float y = top + 32 + title.height + 16;
		uint32_t dots[3];
		TimelineStopColours(dots);
		canvas.Rect({x, y + 7, 12, 12}, 6, dots[1]);
		const std::string line = TrP(games, "{0} game", "{0} games") + " · " + TrP(consoles, "{0} console", "{0} consoles");
		canvas.Text(Style({24, ui::Weight::Regular, 1.0f}), x + 26, y, line, Secondary(), width - 26, 1);
		y += 60;
		const ui::TextBlock body = canvas.Text(Style(kBodyStyle), x, y,
			Tr("Every console's games on one shelf, oldest to newest, each as the disc, card or cartridge it came on."), Secondary(), std::min(width, 1000.0f), 3);
		y += body.height + 30;
		PillButton(canvas, x, y, Tr("Open"), true, !m_onBar, Icon::Cross, true, 64);
		canvas.PopAlpha();

		// the ruler in little, on glass: the eras, every console's mark, the player's games by their years
		canvas.PushAlpha(Enter(2));
		const Box panel{x, orbit ? 680.0f : 700.0f, width, 250};
		Glass(canvas, panel, 28, Wash(0.06f));
		TimelineRuler(canvas, {panel.x + 48, panel.y + 34, panel.w - 96, 176}, kText, Secondary(), 0, -1, -1);
		canvas.PopAlpha();
		if (orbit)
			OrbitHintsDraw(canvas, false);
	}

	// -- the ruler: its eras, its years, a mark for each console, the player's games by the year each came out --

	void Shell::TimelineRuler(Canvas& canvas, const Box& area, uint32_t ink, uint32_t quiet, uint32_t ground, int focusShelf, float pointerYear)
	{
		auto yearX = [&](float year) { return area.x + (year - kFirstYear) / kYears * area.w; };
		const float ruler = area.y + 114;
		// the rooms: a band each, every other one a shade darker, its name under the years
		const ui::TextStyle roomStyle = Style({12, ui::Weight::Bold, 1.0f, 1.2f, true});
		for (int i = 0; i < kRoomCount; i++)
		{
			const float from = yearX((float)kRooms[i].from), to = i + 1 < kRoomCount ? yearX((float)kRooms[i + 1].from) : area.Right();
			const int here = focusShelf >= 0 && focusShelf < (int)m_shelves.size() ? RoomOf(m_shelves[focusShelf].year) : -1;
			canvas.Rect({from, area.y, to - from, area.h}, 0, ui::SetAlpha(ink, i == here ? 0.075f : i % 2 ? 0.04f : 0.015f));
			canvas.Text(roomStyle, from + 8, area.Bottom() - 22, Tr(kRooms[i].name), i == here ? ink : quiet, to - from - 12, 1);
		}
		// the player's games, a bar at each year as tall as how many came out in it (from GameTDB's dates)
		std::map<int, int> byYear;
		int most = 1;
		for (const TimelineShelf& shelf : m_shelves)
			for (const Game& game : shelf.games)
				if (game.year >= kFirstYear && game.year < kFirstYear + kYears)
					most = std::max(most, ++byYear[game.year]);
		const float yearWidth = area.w / kYears;
		for (const auto& [year, count] : byYear)
		{
			const float h = 4 + 34 * count / (float)most;
			const bool here = pointerYear >= 0 && (int)std::lround(pointerYear) == year;
			canvas.Rect({yearX(year + 0.5f) - yearWidth * 0.3f, ruler - h, yearWidth * 0.6f, h}, 2, ui::SetAlpha(ink, here ? 0.45f : 0.16f));
		}
		// the line, a tick a year, the years every five
		canvas.Rect({area.x, ruler, area.w, 2}, 0, ink);
		const ui::TextStyle yearStyle = Style({14, ui::Weight::Medium, 1.0f, 0.3f, false, true});
		for (int year = kFirstYear; year < kFirstYear + kYears; year++)
		{
			const float x = yearX((float)year);
			const bool five = year % 5 == 0;
			canvas.Rect({x - 1, ruler + 2, 2, five ? 12.0f : 6.0f}, 0, ink);
			if (five)
			{
				const ui::TextBlock label = m_fonts.Layout(yearStyle, std::to_string(year));
				canvas.Text(label, x - label.width * 0.5f, ruler + 16, quiet);
			}
		}
		// each console's mark at its year, in lanes where they crowd (the mid-1990s, 2001)
		float lanes[4] = {-999, -999, -999, -999};
		const ui::TextStyle markStyle = Style({14, ui::Weight::Bold, 1.0f});
		for (int i = 0; i < (int)m_shelves.size(); i++)
		{
			const TimelineShelf& one = m_shelves[i];
			const float x = yearX((float)one.year);
			int lane = 0;
			while (lane < 3 && x - lanes[lane] < 84)
				lane++;
			lanes[lane] = x;
			const float y = ruler - 52 - lane * 26;
			const uint32_t colour = AccentOf(one.side) | 0xff000000;
			canvas.Rect({x - 1, y + 16, 2, ruler - (y + 16)}, 0, ui::SetAlpha(colour, 0.7f));
			const float size = i == focusShelf ? 24.0f : 16.0f;
			const Box dot{x - size / 2, y + 8 - size / 2, size, size};
			if (ground)
				canvas.Rect(dot.Scaled(1.25f), size * 0.625f, ground);
			canvas.Rect(dot, size / 2, colour);
			if (i == focusShelf)
				canvas.Ring(dot.Scaled(1.3f), size * 0.65f, 2, ink);
			// its name right of its mark, or left of it at the ruler's end
			const ui::TextBlock name = m_fonts.Layout(markStyle, one.brief);
			const bool left = x + size / 2 + 6 + name.width > area.Right() + 24;
			canvas.Text(name, left ? x - size / 2 - 6 - name.width : x + size / 2 + 6, y, ink);
		}
		// the game in focus: a playhead on the line at its year, gliding there
		if (pointerYear >= 0)
		{
			const float x = yearX(pointerYear + 0.5f);
			if (ground)
				canvas.Rect({x - 6, ruler - 11, 12, 24}, 6, ground);
			canvas.Rect({x - 4, ruler - 9, 8, 20}, 4, ink);
		}
	}

	// -- a game's manual, as Nintendo keeps it online ---------------------------------------------------

	std::string Shell::ManualOf(System side, const Game& game)
	{
		const char* system = side == System::Nes ? "nes" : side == System::Snes ? "snes" : side == System::WiiU ? "wiiu" : "";
		if (!*system)
			return {};
		std::vector<std::string> names = {game.entry.game.path.stem().string(), game.entry.game.name};
		if (game.known && !game.info.title.empty())
			names.push_back(game.info.title);
		return ps5gameinfo::FindManual(system, names);
	}

	void Shell::DrawManualQr(Canvas& canvas, const std::string& address, const Box& box)
	{
		auto code = m_qrCodes.find(address);
		if (code == m_qrCodes.end())
			code = m_qrCodes.emplace(address, ui::QrCode(address)).first;
		if (code->second.Size())
			ui::DrawQr(canvas, code->second, box, 12);
	}

	// -- the Timeline -----------------------------------------------------------------------------------

	void Shell::TimelineBuild()
	{
		// What the shelves are made from: the sides shown, their games as the catalogue has them, the covers come
		// since, the language. Made again only when that changes: a game made for every game of every side is
		// too much for each turn past the stop
		uint64_t key = 1469598103934665603ull;
		auto mix = [&](uint64_t value) { key = (key ^ value) * 1099511628211ull; };
		mix(m_settings.ui.sides);
		mix(m_settings.boxArt);
		mix(ps5boxart::Arrivals());
		mix(ps5eden::CoverArrivals());
		mix(std::hash<std::string>{}(ps5lang::Current()));
		for (const Era& era : kEras)
			if (SideShown(era.side))
				AsSide(era.side, [&] {
					mix((uint64_t)era.side + 1);
					for (const ps5catalog::Entry& entry : ps5catalog::Games(CatalogSide()))
					{
						mix(entry.game.titleId);
						mix(std::hash<std::string>{}(entry.game.path.string()));
						mix((uint64_t)entry.hidden | (uint64_t)entry.favourite << 1);
						mix(entry.minutesPlayed);
						mix((uint64_t)entry.lastPlayed);
					}
				});
		if (key == m_timelineKey && !m_shelves.empty())
			return;
		m_timelineKey = key;
		m_shelves.clear();
		std::map<ps5boxart::System, std::vector<std::string>> media; // the discs and cards to ask GameTDB for
		std::vector<std::string> cartPictures;						 // and the cartridges to ask LaunchBox for
		for (const Era& era : kEras)
		{
			if (!SideShown(era.side))
				continue;
			TimelineShelf shelf;
			shelf.side = era.side;
			shelf.year = era.year;
			shelf.brief = era.brief;
			shelf.media = era.media;
			AsSide(era.side, [&] {
				for (const ps5catalog::Entry& entry : ps5catalog::Games(CatalogSide()))
				{
					if (entry.hidden)
						continue;
					Game game;
					MakeGame(game, entry);
					shelf.games.push_back(std::move(game));
				}
			});
			// a libretro side's games: their years, publishers and cartridges from the LaunchBox Games Database
			const char* cartSystem = CartSystem(era.side);
			std::map<std::string, std::string> carts; // a game's file -> its cartridge's picture
			if (*cartSystem)
				for (Game& game : shelf.games)
				{
					ps5gameinfo::Cart cart;
					if (ps5gameinfo::FindCart(cartSystem, game.entry.game.path.stem().string(), cart))
					{
						if (!game.year)
							game.year = cart.year;
						if (game.entry.game.publisher.empty())
							game.entry.game.publisher = cart.publisher;
					}
					carts[game.entry.game.path.string()] = cart.picture;
				}
			// each by its own year, where GameTDB or LaunchBox gives it, else by its name
			std::stable_sort(shelf.games.begin(), shelf.games.end(), [](const Game& a, const Game& b) {
				if (a.year != b.year)
					return a.year < b.year;
				return a.sortName < b.sortName;
			});
			shelf.mediaArt.assign(shelf.games.size(), std::string());
			shelf.carts.assign(shelf.games.size(), std::string());
			for (size_t i = 0; i < shelf.games.size() && !carts.empty(); i++)
				shelf.carts[i] = carts[shelf.games[i].entry.game.path.string()];
			ShelfSize(shelf.media, shelf.mediaW, shelf.mediaH);
			const float gap = std::max(14.0f, shelf.mediaW * 0.14f);
			const int count = (int)shelf.games.size();
			shelf.w = count ? std::max(kShelfWidth, 20 + count * shelf.mediaW + std::max(0, count - 1) * gap + 40) : std::max(kEmptyWidth, std::min(shelf.mediaW, 150.0f) + 40);
			ps5boxart::System system;
			if (MediaSystem(era.side, system))
				for (const Game& game : shelf.games)
					if (!game.entry.game.gameId.empty())
						media[system].push_back(game.entry.game.gameId);
			for (const std::string& picture : shelf.carts)
				if (!picture.empty())
					cartPictures.push_back(picture);
			m_shelves.push_back(std::move(shelf));
		}
		float along = 0;
		for (TimelineShelf& shelf : m_shelves)
		{
			shelf.x = along;
			along += shelf.w;
		}
		if (m_settings.boxArt)
		{
			for (const auto& [system, ids] : media)
				ps5boxart::FetchMedia(system, ids);
			ps5boxart::FetchCarts(cartPictures);
		}
		m_mediaArrivals = ps5boxart::MediaArrivals();
		TimelineMedia();
	}

	void Shell::TimelineMedia()
	{
		for (TimelineShelf& shelf : m_shelves)
		{
			ps5boxart::System system;
			const bool scans = MediaSystem(shelf.side, system);
			for (size_t i = 0; i < shelf.games.size(); i++)
				if (shelf.mediaArt[i].empty())
					shelf.mediaArt[i] = scans ? ps5boxart::MediaPath(system, shelf.games[i].entry.game.gameId) : ps5boxart::CartPath(shelf.carts[i]);
		}
	}

	void Shell::TimelineOpen()
	{
		m_feedback.Play(ui::Cue::Select);
		TimelineBuild();
		// the shelf of the side open, on the game in focus
		m_shelfAt = 0;
		m_shelfGame = 0;
		const uint64_t title = !m_orbitHome && LibraryGame() >= 0 ? m_games[LibraryGame()].entry.game.titleId : 0;
		for (int i = 0; i < (int)m_shelves.size(); i++)
		{
			if (m_shelves[i].side != m_side)
				continue;
			m_shelfAt = i;
			for (int j = 0; j < (int)m_shelves[i].games.size(); j++)
				if (m_shelves[i].games[j].entry.game.titleId == title)
					m_shelfGame = j;
		}
		m_insertAt = 0;
		m_shelfScroll.value = m_shelfScroll.target = -1; // there at once
		m_rulerAt.value = m_rulerAt.target = -1;
		m_onBar = false;
		Show(ScreenId::Timeline);
		m_screenAt = m_now;
	}

	void Shell::TimelineUpdate(const ui::Press& press)
	{
		if (m_insertAt > 0 || m_shelves.empty())
		{
			if (press.button == Button::Circle && m_insertAt <= 0)
				Show(ScreenId::Library);
			return;
		}
		TimelineShelf& shelf = m_shelves[m_shelfAt];
		const int last = (int)m_shelves.size() - 1;
		auto toShelf = [&](int to, bool lastGame = false) {
			to = std::clamp(to, 0, last);
			if (to == m_shelfAt)
			{
				m_feedback.Play(ui::Cue::Edge, press.repeat);
				return;
			}
			m_shelfAt = to;
			m_shelfGame = lastGame ? std::max(0, (int)m_shelves[to].games.size() - 1) : 0;
			m_feedback.Play(ui::Cue::Focus);
		};
		switch (press.button)
		{
		case Button::Left:
			if (m_shelfGame > 0)
			{
				m_shelfGame--;
				m_feedback.Play(ui::Cue::Focus);
			}
			else
				toShelf(m_shelfAt - 1, true);
			break;
		case Button::Right:
			if (m_shelfGame + 1 < (int)shelf.games.size())
			{
				m_shelfGame++;
				m_feedback.Play(ui::Cue::Focus);
			}
			else
				toShelf(m_shelfAt + 1);
			break;
		case Button::L1: toShelf(m_shelfAt - 1); break;
		case Button::R1:
		case Button::Touchpad: toShelf(m_shelfAt + 1); break;
		case Button::Up:
		case Button::Down:
		{
			// the era before or after: its first console shown
			const int room = RoomOf(shelf.year);
			int to = m_shelfAt;
			if (press.button == Button::Down)
			{
				while (to < last && RoomOf(m_shelves[to].year) == room)
					to++;
			}
			else
			{
				while (to > 0 && RoomOf(m_shelves[to].year) == room)
					to--;
				while (to > 0 && RoomOf(m_shelves[to - 1].year) == RoomOf(m_shelves[to].year))
					to--;
			}
			toShelf(to);
			break;
		}
		case Button::L2: // the Library (its Timeline stop) and Settings, as the triggers are everywhere
			m_feedback.Play(ui::Cue::Back);
			Show(ScreenId::Library);
			break;
		case Button::R2:
			m_feedback.Play(ui::Cue::Focus);
			ShowTab(ScreenId::Settings);
			break;
		case Button::Cross:
			if (!shelf.games.empty())
			{
				m_insertAt = m_now;
				m_feedback.Play(ui::Cue::Select);
			}
			else
			{
				// an empty shelf: its side, with its own words on where its games go
				const System side = shelf.side;
				Show(ScreenId::Library);
				SwitchSide(side);
			}
			break;
		case Button::Circle:
			m_feedback.Play(ui::Cue::Back);
			Show(ScreenId::Library);
			break;
		default: break;
		}
	}

	void Shell::TimelineDraw(Canvas& canvas)
	{
		if (m_shelves.empty())
			return;
		if (ps5boxart::MediaArrivals() != m_mediaArrivals)
		{
			m_mediaArrivals = ps5boxart::MediaArrivals();
			TimelineMedia();
		}
		m_shelfAt = std::clamp(m_shelfAt, 0, (int)m_shelves.size() - 1);
		TimelineShelf& shelf = m_shelves[m_shelfAt];
		m_shelfGame = std::clamp(m_shelfGame, 0, std::max(0, (int)shelf.games.size() - 1));
		Game* focus = shelf.games.empty() ? nullptr : &shelf.games[m_shelfGame];
		const Era* focusEra = EraOf(shelf.side);

		// Cross has put it in the slot: it starts as the cartridge has gone in
		constexpr double kInsertSeconds = 0.7;
		const float inserted = m_insertAt > 0 ? ui::ease::CubicOut(std::min(1.0f, (float)((m_now - m_insertAt) / (m_settings.ui.reduceMotion ? 0.01 : 0.56)))) : 0.0f;
		if (m_insertAt > 0 && m_now - m_insertAt > kInsertSeconds && focus)
		{
			const System side = shelf.side;
			const Game game = *focus;
			m_insertAt = 0;
			StartFromAnySide(side, game);
			return;
		}

		// the top: its name, the era in focus (its line, how many of the player's games are from it), how many,
		// the clock
		const int room = RoomOf(shelf.year);
		int games = 0, eraGames = 0;
		for (const TimelineShelf& one : m_shelves)
		{
			games += (int)one.games.size();
			if (RoomOf(one.year) == room)
				eraGames += (int)one.games.size();
		}
		char clock[16] = "";
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		if (localtime_r(&now, &local))
			std::strftime(clock, sizeof(clock), "%H:%M", &local);
		canvas.PushAlpha(Enter(0));
		const float logo = DrawWordmark(canvas, kLeft, 71, 34, kInk);
		canvas.Rect({kLeft + logo + 18, 56, 2, 30}, 0, ui::SetAlpha(kInk, 0.25f));
		// tr: the view of every console's games on one shelf, a stop of its own in the console lists
		const ui::TextBlock heading = canvas.Text(Style({32, ui::Weight::Bold, 1.0f, 0.3f, true}), kLeft + logo + 38, 54, Tr("Timeline"), kInk, 300, 1);
		const ui::TextStyle right = Style({20, ui::Weight::Medium, 1.0f, 0.4f, true, true});
		float rightEdge = 1920 - kSafeX;
		const ui::TextBlock time = m_fonts.Layout(right, clock);
		rightEdge -= time.width;
		canvas.Text(time, rightEdge, 58, kInk);
		const ui::TextBlock count = m_fonts.Layout(right, TrP(games, "{0} game", "{0} games"));
		rightEdge -= count.width + 28;
		canvas.Text(count, rightEdge, 58, kInk);
		float eraX = kLeft + logo + 58 + heading.width;
		// tr: over the Timeline's shelves, after the era's name: how many of the player's games are from it
		const ui::TextBlock era = canvas.Text(Style({16, ui::Weight::Medium, 1.0f, 0.6f, true}), eraX, 64,
			Joined({Tr(kRooms[room].name), TrP(eraGames, "{0} of your games", "{0} of your games")}), kInk, rightEdge - 40 - eraX, 1);
		eraX += era.width + 14;
		if (rightEdge - 40 - eraX > 120)
			canvas.Text(Style({19, ui::Weight::Regular, 1.0f}), eraX, 61, Tr(kRooms[room].line), kInk3, rightEdge - 40 - eraX, 1);
		canvas.PopAlpha();

		// the ruler, its pointer gliding to the year of the game in focus (its console's, when GameTDB has none)
		m_rulerAt.target = (float)(focus && focus->year ? focus->year : shelf.year);
		if (m_rulerAt.value < 0)
			m_rulerAt.value = m_rulerAt.target;
		m_rulerAt.omega = m_settings.ui.reduceMotion ? 60.0f : kScrollOmega;
		m_rulerAt.Update(m_dt);
		canvas.PushAlpha(Enter(0));
		TimelineRuler(canvas, {kLeft, 110, kView, 176}, kInk, kInk3, kPaper, m_shelfAt, m_rulerAt.value);
		canvas.PopAlpha();

		// the shelf: its boards, slid to the one in focus
		float total = 0;
		for (const TimelineShelf& one : m_shelves)
			total += one.w;
		const float gapOf = std::max(14.0f, shelf.mediaW * 0.14f);
		const float wanted = std::min(shelf.x, std::max(0.0f, total - kView));
		// a long shelf slides on with its game in focus
		float slideTo = wanted;
		if (focus)
		{
			const float itemRight = shelf.x + 20 + (m_shelfGame + 1) * shelf.mediaW + m_shelfGame * gapOf + 40;
			slideTo = std::max(wanted, std::min(itemRight - kView, std::max(0.0f, total - kView)));
		}
		m_shelfScroll.target = slideTo;
		if (m_shelfScroll.value < 0)
			m_shelfScroll.value = slideTo;
		m_shelfScroll.omega = m_settings.ui.reduceMotion ? 60.0f : kScrollOmega;
		m_shelfScroll.Update(m_dt);
		const float scroll = m_shelfScroll.value;

		// on opening, the pieces are set down on the shelf one after another, left to right: how far above it one
		// still is (0: on it)
		auto setDown = [&](float x) {
			if (m_settings.ui.reduceMotion || m_behind)
				return 0.0f;
			const float t = (float)((m_now - m_screenAt - kStaggerSeconds - std::clamp((x - kLeft) / kView, 0.0f, 1.0f) * 0.32f) / 0.46f);
			return 1 - ui::ease::CubicOut(std::clamp(t, 0.0f, 1.0f));
		};

		// one piece: GameTDB's picture of it, else its body with the box art on its label (the box art whole on
		// a disc's round label, filling a cartridge's); the box it took and its corner, for its ring
		// a favourite's star, on the piece's corner (a disc's, by its rim)
		auto favourite = [&](const Game& game, const Box& piece, bool round) {
			if (!game.entry.favourite)
				return;
			const float size = std::clamp(piece.w * 0.2f, 22.0f, 30.0f);
			const float inset = round ? piece.w * 0.146f : 0.0f; // a circle's corner at 45 degrees
			const Box badge{piece.Right() - inset - size * 0.7f, piece.y + inset - size * 0.3f, size, size};
			canvas.Rect(badge.Scaled(1.15f), size * 0.575f, kPaper);
			canvas.Rect(badge, size / 2, kInk);
			canvas.Draw(Icon::Star, badge.Inset(size * 0.22f), kPaper);
		};

		// what a piece takes of its box: its picture (as it is, its own shape clear around it, as large as the box
		// lets it, standing on its foot), else the drawn thing; and its picture, when it has one
		auto pieceBox = [&](TimelineShelf& one, int index, const Box& box, const ui::Picture** picture) -> Box {
			const std::string& scanPath = one.mediaArt[index];
			const ui::Picture* scan = scanPath.empty() ? nullptr : &m_images->Get(scanPath);
			*picture = scan && scan->texture && scan->width > 0 && scan->height > 0 ? scan : nullptr;
			if (!*picture)
				return BodyBox(one.media, box);
			const float aspect = (float)scan->width / scan->height;
			if (box.w > box.h * aspect)
				return {box.CentreX() - box.h * aspect * 0.5f, box.y, box.h * aspect, box.h};
			return {box.x, box.Bottom() - box.w / aspect, box.w, box.w / aspect};
		};

		auto drawPiece = [&](TimelineShelf& one, int index, const Box& box, bool words, float& radius) -> Box {
			Game& game = one.games[index];
			const bool round = Round(one.media);
			const ui::Picture* scan = nullptr;
			const Box fitted = pieceBox(one, index, box, &scan);
			if (scan)
			{
				radius = round ? fitted.w / 2 : fitted.w * 0.06f;
				canvas.Shadow(fitted.Inset(fitted.w * 0.03f), radius, 6, 14, 0x29000000);
				canvas.Image(scan->texture, fitted, 0);
				if (words)
					favourite(game, fitted, round);
				return fitted;
			}
			const Box body = BodyBox(one.media, box);
			radius = round ? body.w / 2 : body.w * 0.05f;
			canvas.Shadow(body, radius, 6, 14, 0x29000000);
			const Label label = DrawBody(canvas, one.media, box);
			bool art = false;
			AsSide(one.side, [&] {
				const ui::Picture& picture = Cover(game);
				if (picture.texture)
				{
					canvas.ImageCover(picture.texture, picture.width, picture.height, label.box, label.radius, 0xffffffff, 0.3f);
					art = true;
				}
			});
			if (!art)
			{
				const uint32_t accent = AccentOf(one.side) | 0xff000000;
				canvas.LinearGradient(label.box, label.radius, ui::Mix(accent, 0xff000000, 0.55f), accent, label.box.x, label.box.y, label.box.Right(),
					label.box.Bottom());
				if (words && label.box.w >= 70)
				{
					const Box& l = label.box;
					const float size = std::clamp(l.w / 9, 10.0f, 16.0f);
					canvas.Text(Style({size, ui::Weight::Bold, 1.1f}), l.x + 6, round ? l.CentreY() - l.h * 0.36f : l.y + 6, game.entry.game.name, 0xffffffff,
						round ? l.w * 0.5f : l.w - 12, round ? 3 : 2);
				}
			}
			DrawOver(canvas, one.media, box);
			if (words)
				favourite(game, body, round);
			return body;
		};

		const Box view{kLeft, kShelfTop, kView, 360};
		canvas.PushAlpha(Enter(1));
		canvas.PushClip(view);
		canvas.Rect({kLeft, kShelfBottom, kView, 8}, 2, kInk);
		canvas.LinearGradient({kLeft, kShelfBottom + 8, kView, 26}, 0, ui::SetAlpha(kInk, 0.18f), ui::SetAlpha(kInk, 0), 0, kShelfBottom + 8, 0, kShelfBottom + 34);
		struct Lift
		{
			float order, mix;
			TimelineShelf* shelf;
			int index;
			Box box;
		};
		std::vector<Lift> lifts;
		for (int i = 0; i < (int)m_shelves.size(); i++)
		{
			TimelineShelf& one = m_shelves[i];
			const float x = kLeft + one.x - scroll;
			if (x > 1920 || x + one.w < 0)
				continue;
			canvas.Rect({x, kShelfTop, 1, kShelfBottom - kShelfTop}, 0, ui::SetAlpha(kInk, 0.14f));
			// its name stays in sight while a long shelf slides under it, until the next shelf's pushes it on
			const float hx = std::max(x, std::min(kLeft, x + one.w - kShelfWidth));
			const float hw = x + one.w - hx;
			const uint32_t colour = AccentOf(one.side) | 0xff000000;
			const bool empty = one.games.empty();
			canvas.Rect({hx + 20, kShelfTop + 12, 12, 12}, 6, colour);
			// an empty shelf's bay is slim: its console's short name, its year
			const std::string name = empty ? std::string(one.brief) : SystemName(one.side);
			ui::TextStyle shelfName = Style({28, ui::Weight::Bold, 1.0f, 0.2f, true});
			shelfName.size = m_fonts.Fit(shelfName, name, hw - 56, 1, 16, 1);
			canvas.Text(shelfName, hx + 42, kShelfTop + 6, name, empty ? kInk3 : kInk, hw - 56, 1);
			const std::string sub = empty ? std::to_string(one.year)
										  : Joined({std::to_string(one.year), Tr(kMedia[one.media].what), TrP((int)one.games.size(), "{0} game", "{0} games")});
			canvas.Text(Style({14, ui::Weight::Medium, 1.0f, 0.4f, true, true}), hx + 20, kShelfTop + 42, sub, kInk3, hw - 40, 1);
			if (empty)
			{
				// where its games would stand: the thing they came on, faint, ringed when the bay is in focus
				float w = one.mediaW, h = one.mediaH;
				const float fit = std::min(1.0f, (one.w - 40) / w);
				w *= fit;
				h *= fit;
				const Box ghost = BodyBox(one.media, {x + (one.w - w) / 2, kShelfBottom - h, w, h});
				const float radius = Round(one.media) ? w / 2 : w * 0.05f;
				canvas.PushAlpha(0.14f);
				DrawBody(canvas, one.media, ghost);
				DrawOver(canvas, one.media, ghost);
				canvas.PopAlpha();
				const bool on = i == m_shelfAt;
				canvas.Ring(ghost, radius, on ? 3.0f : 1.5f, ui::SetAlpha(kInk, on ? 1.0f : 0.25f));
				canvas.Text(Style({14, ui::Weight::SemiBold, 1.15f}), x + 20, ghost.y - 46, Tr("No games yet"), on ? kInk : kInk3, one.w - 40, 2);
				continue;
			}
			const float gap = std::max(14.0f, one.mediaW * 0.14f);
			float itemX = x + 20;
			for (int j = 0; j < (int)one.games.size(); j++)
			{
				Box box{itemX, kShelfBottom - one.mediaH, one.mediaW, one.mediaH};
				itemX += one.mediaW + gap;
				if (box.Right() < 0)
					continue;
				if (box.x > 1920)
					break;
				const float above = setDown(box.x);
				box.y -= 90 * above;
				// lifted off the shelf, over its neighbours, gliding up as the focus comes and down as it goes
				const bool on = i == m_shelfAt && j == m_shelfGame;
				const float mix = FocusMix(FocusKey(one.games[j].entry.game.path.string(), (uint64_t)one.side + 201), on);
				if (mix > 0.002f || on)
					lifts.push_back({mix + (on ? 1 : 0), mix, &one, j, box});
				else
				{
					float radius = 0;
					canvas.PushAlpha(1 - above);
					drawPiece(one, j, box, true, radius);
					canvas.PopAlpha();
				}
			}
		}
		std::sort(lifts.begin(), lifts.end(), [](const Lift& a, const Lift& b) { return a.order < b.order; });
		for (const Lift& lift : lifts)
		{
			const float scale = 1 + ((m_settings.ui.reduceMotion ? 1.0f : 1.12f) - 1) * lift.mix;
			const Box& b = lift.box;
			const Box lifted{b.x - b.w * (scale - 1) * 0.5f, b.Bottom() - b.h * scale - 36 * lift.mix, b.w * scale, b.h * scale};
			const ui::Picture* picture = nullptr;
			const Box shape = pieceBox(*lift.shelf, lift.index, lifted, &picture);
			canvas.Shadow(shape, Round(lift.shelf->media) ? shape.w / 2 : 6, 30 * lift.mix, 40, ui::WithAlpha(0x52000000, lift.mix));
			float radius = 0;
			const Box drawn = drawPiece(*lift.shelf, lift.index, lifted, true, radius);
			canvas.Ring(drawn.Scaled(1.04f), radius * 1.04f, 3, ui::WithAlpha(kInk, lift.mix));
		}
		canvas.PopClip();
		canvas.PopAlpha();

		// the game in focus, on its museum label, and its console's placard on the slot it goes in
		canvas.PushAlpha(Enter(2));
		// its manual, where Nintendo keeps one online: a QR code at the label's end, to read it on a phone
		const std::string manual = focus ? ManualOf(shelf.side, *focus) : std::string();
		const float x = kLeft, width = manual.empty() ? 1180.0f : 1000.0f;
		if (!manual.empty())
		{
			const Box code{kLeft + 1180 - 150, 694, 150, 150};
			DrawManualQr(canvas, manual, code);
			// tr: under a QR code on the Timeline's museum label: it opens the game's manual on a phone
			const ui::TextBlock caption = m_fonts.Layout(Style({14, ui::Weight::Medium, 1.0f, 0.4f, true, true}), Tr("Manual"));
			canvas.Text(caption, code.CentreX() - caption.width * 0.5f, code.Bottom() + 10, kInk3);
		}
		if (focus)
		{
			const int year = focus->year ? focus->year : shelf.year;
			const std::string publisher = focus->known && !focus->info.publisher.empty() ? focus->info.publisher : focus->entry.game.publisher;
			canvas.Text(Style({17, ui::Weight::Medium, 1.0f, 0.6f, true, true}), x, 690, Joined({SystemName(shelf.side), std::to_string(year), publisher}), kInk3, width, 1);
			const std::string& gameName = focus->entry.game.name;
			const ui::TextBlock title = canvas.Text(Style({gameName.size() > 22 ? 56.0f : 68.0f, ui::Weight::Bold, 1.04f}), x, 718, gameName, kInk, width, 2);
			float factsY = 718 + title.height + 12;
			// tr: on a game's museum label in the Timeline: {0} is the day it came out
			const std::string released = focus->known && focus->info.released.size() > 4 ? TrF("Released {0}", ps5gameinfo::ReleaseDate(focus->info.released)) : "";
			const std::string kind = focus->known ? ps5gameinfo::Genres(focus->info.genre, 2) : "";
			const std::string about = Joined({released, kind});
			if (!about.empty())
			{
				canvas.Text(Style({22, ui::Weight::Regular, 1.2f}), x, factsY, about, kInk2, width, 1);
				factsY += 34;
			}
			const std::string last = LastPlayedWords(focus->entry.lastPlayed);
			const std::string played = Joined({Played(*focus), last.empty() ? "" : TrF("Last played {0}", last)});
			const std::string playedText = played.empty() ? std::string(Tr("Not played yet")) : played;
			const ui::TextBlock factsText = canvas.Text(Style({22, ui::Weight::Regular, 1.2f}), x, factsY, playedText, kInk2, width, 1);
			if (focus->report)
			{
				// how it runs, after its facts: the chips' dot in its colour, and its word
				const std::string kind = ps5compat::Kind(focus->report->status);
				const float dx = x + factsText.width + 22;
				canvas.Rect({dx, factsY + 9, 10, 10}, 5, kind == "good" ? kGood : kind == "warn" ? kWarn : kBad);
				canvas.Text(Style({22, ui::Weight::SemiBold, 1.2f}), dx + 18, factsY, TrC("status", focus->report->status), kInk2, width, 1);
			}
			float buttonY = factsY + 46;
			// what the game is, as GameTDB tells it, in as many lines as the label has room for above Play (none
			// for a game GameTDB does not know): its paragraphs run on, past a first one that is only a tagline
			// (short, or in capitals)
			if (focus->known && !focus->info.synopsis.empty())
			{
				std::string about = focus->info.synopsis;
				const size_t first = about.find('\n');
				if (first != std::string::npos)
				{
					const std::string head = about.substr(0, first);
					const bool capitals = std::none_of(head.begin(), head.end(), [](char c) { return c >= 'a' && c <= 'z'; });
					const bool sentence = !head.empty() && std::strchr(".!?", head.back());
					if ((head.size() < 60 && !sentence) || capitals)
						about = about.substr(first + 1);
				}
				about.erase(0, about.find_first_not_of('\n'));
				std::replace(about.begin(), about.end(), '\n', ' ');
				const ui::TextStyle aboutStyle = Style({19, ui::Weight::Regular, 1.35f});
				const int lines = std::clamp((int)((1004 - 60 - 16 - (factsY + 40)) / (19 * 1.35f)), 0, 3);
				if (lines > 0)
				{
					const ui::TextBlock text = canvas.Text(aboutStyle, x, factsY + 40, about, kInk3, std::min(width, 1080.0f), lines);
					buttonY = factsY + 40 + text.height + 16;
				}
			}
			const ui::TextStyle playStyle = Style({22, ui::Weight::SemiBold, 1.0f});
			const std::string play = Tr("Play");
			const Box button{x, buttonY, m_fonts.Width(playStyle, play) + 96, 60};
			canvas.Rect(button, 30, kInk);
			canvas.Draw(Icon::Cross, {button.x + 28, button.CentreY() - 12, 24, 24}, 0xffffffff);
			const ui::TextBlock label = m_fonts.Layout(playStyle, play);
			canvas.Text(label, button.x + 62, button.CentreY() - label.height * 0.5f, 0xffffffff);
		}
		else
		{
			canvas.Text(Style({17, ui::Weight::Medium, 1.0f, 0.6f, true, true}), x, 690, std::string(SystemName(shelf.side)) + " · " + std::to_string(shelf.year), kInk3, width, 1);
			const ui::TextBlock title = canvas.Text(Style({68, ui::Weight::Bold, 1.04f}), x, 718, Tr("An empty shelf"), kInk, width, 1);
			canvas.Text(Style({22, ui::Weight::Regular, 1.3f}), x, 730 + title.height, TrF("Put its games in {0}, on the PS5 or a USB drive.", GamesFolder(shelf.side)), kInk2, width, 2);
		}

		// the console: its placard is the machine the game goes in, its slot on top
		const Box machine{1364, 836, 460, 172};
		const float slotY = machine.y + 26;
		if (focus)
		{
			const float fit = std::min({1.5f, 150 / shelf.mediaH, 400 / shelf.mediaW});
			const float w = shelf.mediaW * fit, h = shelf.mediaH * fit;
			const Box piece{machine.CentreX() - w / 2, slotY + 4 - h + inserted * (h + 8), w, h};
			canvas.PushClip({machine.x, 640, machine.w, slotY + 7 - 640});
			float radius = 0;
			drawPiece(shelf, m_shelfGame, piece, false, radius);
			canvas.PopClip();
		}
		canvas.Rect(machine, 22, kInk);
		canvas.Rect({machine.CentreX() - 150, slotY, 300, 14}, 7, 0xff000000);
		canvas.Rect({machine.Right() - 34, machine.y + 22, 10, 10}, 5, m_insertAt > 0 ? Rgb(0x2bd67b) : Rgb(0x5b6070));
		const uint32_t accent = AccentOf(shelf.side) | 0xff000000;
		DrawConsole(canvas, shelf.side, machine.x + 62, machine.y + 104, 72, accent, kInk, accent, ConsoleIcon(shelf.side, 72));
		const float textX = machine.x + 118, textW = machine.w - 118 - 24;
		canvas.Text(Style({22, ui::Weight::Bold, 1.0f}), textX, machine.y + 56, SystemName(shelf.side), 0xffffffff, textW, 1);
		// and the player's time on it, every game's together
		uint32_t minutes = 0;
		for (const Game& game : shelf.games)
			minutes += game.entry.minutesPlayed;
		const std::string hours = minutes == 0 ? "" : minutes < 60 ? TrP(minutes, "{0} min played", "{0} min played") : TrP(minutes / 60, "{0} h played", "{0} h played");
		canvas.Text(Style({14, ui::Weight::Medium, 1.0f, 0.4f, true, true}), textX, machine.y + 86, Joined({std::to_string(shelf.year), Tr(kMedia[shelf.media].what), hours}),
			Rgb(0xaeb3bd), textW, 1);
		if (focusEra)
			canvas.Text(Style({15, ui::Weight::Regular, 1.3f}), textX, machine.y + 110, Tr(focusEra->placard), Rgb(0xd7d9de), textW, 2);
		canvas.PopAlpha();

		// its hints, in its ink
		const ui::TextStyle hint = Style({18, ui::Weight::Regular, 1.0f});
		struct Item
		{
			Icon first, second;
			bool two;
			std::string words;
		};
		// tr: the Timeline's hint for Up and Down, which go to the era before or after
		const Item items[4] = {{Icon::L1, Icon::R1, true, Tr("Previous or next console")}, {Icon::UpDown, Icon::UpDown, false, Tr("Previous or next era")},
			{Icon::L2, Icon::R2, true, Tr("Library or Settings")}, {Icon::Circle, Icon::Circle, false, Tr("Back")}};
		float at = 1920 - kSafeX;
		const float hintY = 1080 - 26 - 28;
		for (int i = 3; i >= 0; i--)
		{
			const ui::TextBlock words = m_fonts.Layout(hint, items[i].words);
			at -= words.width;
			canvas.Text(words, at, hintY + 14 - words.height * 0.5f, kInk2);
			at -= 32;
			canvas.Draw(items[i].second, {at, hintY, 26, 28}, kInk2);
			if (items[i].two)
			{
				at -= 30;
				canvas.Draw(items[i].first, {at, hintY, 26, 28}, kInk2);
			}
			at -= 30;
		}
	}
}
