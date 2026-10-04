// SPDX-License-Identifier: GPL-3.0-or-later
#include "ingame3ds.h"
#include "menu_canvas.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/pad.h"

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "imgui/imgui_impl_vulkan.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <atomic>
#include <mutex>

// Cemu's overlay font, compressed into Cemu (resource/CafeDefaultFont.cpp)
uint8* extractCafeDefaultFont(sint32* size);

namespace ps5ingame3ds
{
	namespace
	{
		using ps5menu::Canvas;
		const ps5menu::Palette& kColours = ps5menu::kGold;

		// What the game's loop and the menu share
		std::mutex s_mutex;
		Settings s_settings;  // under s_mutex
		bool s_changed = false;
		std::string s_name;
		uint64_t s_titleId = 0;
		double s_fps = 0, s_speed = 0;
		std::string s_breakdown;
		std::atomic<bool> s_open{false};
		std::atomic<uint64_t> s_openedAt{0}; // sceKernelGetProcessTime
		std::atomic<bool> s_libraryRequested{false};
		std::vector<std::string> s_stateTimes; // under s_mutex, with the two below
		std::string s_stateMessage;
		int s_stateRequest = 0; // a save to slot n (n), a load from it (-n)
		std::vector<std::string> s_amiibo; // under s_mutex, with the four below
		std::vector<std::pair<std::string, bool>> s_cheats;
		std::string s_extrasMessage;
		ExtrasRequest s_extrasRequest;
		int s_amiiboIndex = 0;
		// the border, under s_mutex: the theme, its picture until the renderer takes it, the screens
		int s_borderTheme = 0;
		std::vector<uint8_t> s_borderPixels;
		int s_borderWidth = 0, s_borderHeight = 0;
		bool s_borderFresh = false;
		std::vector<ScreenRect> s_screens;
		float s_screensWidth = 0, s_screensHeight = 0;

		// The keyboard: asked for on Azahar's thread, typed on its renderer's, its result taken by
		// the game's loop
		std::atomic<bool> s_keyboardOpen{false};
		KeyboardRequest s_keyboard; // under s_mutex, with the four below
		std::string s_keyboardError;
		bool s_keyboardFresh = false; // a new request: the renderer starts its text over
		bool s_keyboardDone = false;
		std::string s_keyboardResult;
		int s_keyboardButton = 0;

		// The rest belongs to Azahar's renderer thread, which draws the menu.
		struct Gpu
		{
			bool ready = false, failed = false;
			VkDevice device = VK_NULL_HANDLE;
			VkRenderPass renderPass = VK_NULL_HANDLE;
			uint32_t width = 0, height = 0; // the screen's pass's picture
			ImGui_ImplVulkan_InitInfo info{};
			VkDescriptorPool pool = VK_NULL_HANDLE;
			ImGuiContext* context = nullptr;
			ImFontAtlas* atlas = nullptr;
			ImFont* title = nullptr;
			ImFont* head = nullptr;
			ImFont* row = nullptr;
			ImFont* small = nullptr;
			bool fontsUploaded = false;
			int uploadAge = -1;	   // frames since the font upload, until its buffer goes
			bool pending = false;  // a frame's draw data waits for the render pass
			ImTextureID border = nullptr;
			std::vector<std::pair<ImTextureID, int>> retired; // textures and their age in frames, until the GPU is done with them
			uint64_t lastFrame = 0;
		};
		Gpu g;
		uint32_t s_buttons = 0, s_pressed = 0;
		bool s_confirmLibrary = false;
		enum class Page
		{
			Main,
			Controls,
			States,
			Cheats,
		};
		Page s_page = Page::Main;
		bool s_pageChanged = false;
		int s_stateSlot = 1;
		int s_cheatOffset = 0; // the Cheats page's first row
		bool s_confirmLoad = false;

		constexpr const char* kLayouts[] = {"Top above bottom", "Top screen only", "Large top screen", "Side by side"};
		constexpr const char* kFilters[] = {"None", "Anime4K", "Bicubic", "ScaleForce", "xBRZ", "MMPX"};
		// the CPU clock's steps, percent of the 3DS's
		constexpr int kClocks[] = {25, 50, 75, 100, 125, 150, 200, 300, 400};

		int NextClock(int clock, int change)
		{
			constexpr int count = (int)std::size(kClocks);
			int at = 3; // 100%
			for (int i = 0; i < count; i++)
				if (kClocks[i] == clock)
					at = i;
			return kClocks[(at + change + count) % count];
		}

		// the speed limit's steps, percent of the 3DS's; 0 is none
		constexpr int kSpeedLimits[] = {100, 150, 200, 300, 0};

		int NextSpeedLimit(int limit, int change)
		{
			constexpr int count = (int)std::size(kSpeedLimits);
			int at = 0; // 100%
			for (int i = 0; i < count; i++)
				if (kSpeedLimits[i] == limit)
					at = i;
			return kSpeedLimits[(at + change + count) % count];
		}

		void Change(const Settings& settings)
		{
			std::lock_guard lock(s_mutex);
			s_settings = settings;
			s_changed = true;
		}

		void CloseMenu()
		{
			s_open = false;
		}

		// The theme's frame round each screen, at the design's 4K sizes (tools/render-borders.py)
		struct BorderStyle
		{
			float shadow;
			ImU32 line;
			bool ring;
		};
		constexpr BorderStyle kBorderStyles[] = {
			{0, 0, false},
			{0.55f, IM_COL32(255, 255, 255, 34), false}, // Midnight
			{0.50f, IM_COL32(255, 210, 90, 60), false},	 // Waves
			{0.55f, IM_COL32(255, 255, 255, 30), false}, // Aurora
			{0.35f, IM_COL32(255, 255, 255, 22), true},	 // Shell
		};

		// The border's picture where no screen is (the space round them cut into the cells of a grid
		// on the screens' edges), then each screen's frame, all outside the screens
		void DrawBorder(int theme)
		{
			std::vector<ScreenRect> screens;
			float width, height;
			{
				std::lock_guard lock(s_mutex);
				screens = s_screens;
				width = s_screensWidth;
				height = s_screensHeight;
			}
			const ImVec2 size = ImGui::GetIO().DisplaySize;
			if (width <= 0 || height <= 0)
				return;
			const float sx = size.x / width, sy = size.y / height;
			for (ScreenRect& r : screens)
				r = {r.left * sx, r.top * sy, r.right * sx, r.bottom * sy};
			ImDrawList* draw = ImGui::GetBackgroundDrawList();
			std::vector<float> xs{0, size.x}, ys{0, size.y};
			for (const ScreenRect& r : screens)
			{
				xs.insert(xs.end(), {r.left, r.right});
				ys.insert(ys.end(), {r.top, r.bottom});
			}
			std::sort(xs.begin(), xs.end());
			std::sort(ys.begin(), ys.end());
			for (size_t i = 0; i + 1 < xs.size(); i++)
				for (size_t j = 0; j + 1 < ys.size(); j++)
				{
					const float x0 = xs[i], x1 = xs[i + 1], y0 = ys[j], y1 = ys[j + 1];
					if (x1 - x0 < 0.5f || y1 - y0 < 0.5f)
						continue;
					const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
					bool inside = false;
					for (const ScreenRect& r : screens)
						inside |= cx > r.left && cx < r.right && cy > r.top && cy < r.bottom;
					if (!inside)
						draw->AddImage(g.border, {x0, y0}, {x1, y1}, {x0 / size.x, y0 / size.y}, {x1 / size.x, y1 / size.y});
				}
			const BorderStyle& style = kBorderStyles[std::clamp(theme, 0, kBorderCount - 1)];
			const float k = size.y / 2160.0f;
			for (const ScreenRect& r : screens)
			{
				// a soft shadow, in rings that fade outwards, deeper below
				constexpr int kRings = 12;
				for (int i = 0; i < kRings; i++)
				{
					const float d = (3 + i * 6) * k;
					const int alpha = (int)(255 * style.shadow * 0.22f * (1.0f - (float)i / kRings));
					draw->AddRect({r.left - d, r.top - d * 0.6f}, {r.right + d, r.bottom + d * 1.6f}, IM_COL32(0, 0, 0, alpha), 0, 0, 6 * k);
				}
				if (style.ring)
				{
					// Shell's recessed ring, a bezel round the screen
					draw->AddRect({r.left - 17 * k, r.top - 17 * k}, {r.right + 17 * k, r.bottom + 17 * k}, IM_COL32(20, 24, 30, 255), 30 * k, 0, 34 * k);
					draw->AddRect({r.left - 34 * k, r.top - 34 * k}, {r.right + 34 * k, r.bottom + 34 * k}, IM_COL32(255, 255, 255, 18), 30 * k, 0, 3 * k);
				}
				draw->AddRect({r.left - 1.5f * k, r.top - 1.5f * k}, {r.right + 1.5f * k, r.bottom + 1.5f * k}, style.line, 0, 0, 3 * k);
			}
		}

		void ShowPage(Page page)
		{
			s_page = page;
			s_pageChanged = true;
			s_confirmLibrary = false;
			s_confirmLoad = false;
		}

		// Cemu's Vulkan entry points from Azahar's instance and device (Cemu's renderer never ran),
		// a descriptor pool for the font, an ImGui context of its own with Cemu's font at the
		// menu's four sizes, and ImGui's Vulkan backend on Azahar's render pass.
		bool Initialize(const Target& target, float scale)
		{
			if (!InitializeGlobalVulkan() || !InitializeInstanceVulkan(target.instance) || !InitializeDeviceVulkan(target.device))
			{
				ps5log::Line("[ingame3ds] Cemu's Vulkan commands did not load from Azahar's device: no menu");
				return false;
			}
			const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
			VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			poolInfo.maxSets = 16; // the font, the border, borders on their way out
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &size;
			if (vkCreateDescriptorPool(target.device, &poolInfo, nullptr, &g.pool) != VK_SUCCESS)
			{
				ps5log::Line("[ingame3ds] no descriptor pool: no menu");
				return false;
			}

			sint32 fontSize = 0;
			uint8* font = extractCafeDefaultFont(&fontSize); // kept: the atlas reads it
			g.atlas = new ImFontAtlas();
			ImFontConfig config{};
			config.FontDataOwnedByAtlas = false;
			g.title = g.atlas->AddFontFromMemoryTTF(font, fontSize, 48.0f * scale, &config);
			g.head = g.atlas->AddFontFromMemoryTTF(font, fontSize, 32.0f * scale, &config);
			g.row = g.atlas->AddFontFromMemoryTTF(font, fontSize, 24.0f * scale, &config);
			g.small = g.atlas->AddFontFromMemoryTTF(font, fontSize, 20.0f * scale, &config);
			g.context = ImGui::CreateContext(g.atlas);
			ImGui::SetCurrentContext(g.context);
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

			ImGui_ImplVulkan_InitInfo info{};
			info.Instance = target.instance;
			info.PhysicalDevice = target.physicalDevice;
			info.Device = target.device;
			info.QueueFamily = target.queueFamily;
			info.Queue = target.queue;
			info.DescriptorPool = g.pool;
			info.MinImageCount = std::max(2u, target.imageCount);
			info.ImageCount = info.MinImageCount;
			ImGui_ImplVulkan_Init(&info, target.renderPass);
			g.info = info;
			g.device = target.device;
			g.renderPass = target.renderPass;
			g.width = target.width;
			g.height = target.height;
			ps5log::Line("[ingame3ds] the menu is ready ({} images in flight)", info.ImageCount);
			return true;
		}

		// ImGui's input from player 1's DualSense: the D-pad and the left stick move between items,
		// Cross chooses; Circle and Options are read here (ImGui gives Circle another use).
		void Input()
		{
			ImGuiIO& io = ImGui::GetIO();
			ps5pad::Data data{};
			const bool connected = ps5pad::Read(0, data) && !(data.buttons & ps5pad::kIntercepted);
			const uint32_t buttons = connected ? data.buttons : 0;
			s_pressed = buttons & ~s_buttons;
			s_buttons = buttons;
			const bool touchpadHeld = buttons & ps5pad::kTouchPad; // a shortcut's
			auto key = [&](ImGuiKey imguiKey, uint32_t mask) { io.AddKeyEvent(imguiKey, !touchpadHeld && (buttons & mask)); };
			key(ImGuiKey_GamepadDpadUp, ps5pad::kUp);
			key(ImGuiKey_GamepadDpadDown, ps5pad::kDown);
			key(ImGuiKey_GamepadDpadLeft, ps5pad::kLeft);
			key(ImGuiKey_GamepadDpadRight, ps5pad::kRight);
			key(ImGuiKey_GamepadFaceDown, ps5pad::kCross);
			auto stick = [&](ImGuiKey negative, ImGuiKey positive, uint8_t raw) {
				const float value = connected ? std::clamp((raw - 128) / 127.0f, -1.0f, 1.0f) : 0.0f;
				io.AddKeyAnalogEvent(negative, value < -0.5f, std::max(-value, 0.0f));
				io.AddKeyAnalogEvent(positive, value > 0.5f, std::max(value, 0.0f));
			};
			stick(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, data.leftX);
			stick(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown, data.leftY);
			io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
			io.MouseDown[0] = false;
		}

		void DrawMenu(float scale)
		{
			ImGuiIO& io = ImGui::GetIO();
			Settings settings;
			std::string name;
			uint64_t titleId;
			{
				std::lock_guard lock(s_mutex);
				settings = s_settings;
				name = s_name;
				titleId = s_titleId;
			}

			const ImVec2 origin{(io.DisplaySize.x - 1920.0f * scale) * 0.5f, (io.DisplaySize.y - 1080.0f * scale) * 0.5f};
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
			ImGui::SetNextWindowFocus();
			ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(0, 0, 0, 0)); // the rows show the focus
			constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
			if (ImGui::Begin("PS5CEMU-HAR##InGameMenu3ds", nullptr, kFlags))
			{
				const bool appearing = ImGui::IsWindowAppearing();
				if (appearing)
				{
					ImGui::GetCurrentContext()->NavDisableHighlight = false;
					s_confirmLibrary = false;
					s_page = Page::Main;
				}
				const Canvas canvas{ImGui::GetWindowDrawList(), scale, origin, kColours};
				canvas.draw->AddRectFilled({0, 0}, io.DisplaySize, kColours.dim); // the game, dimmed

				canvas.Text(g.title, 48, 108, 62, kColours.title, "PS5 AZAHAR");
				canvas.Text(g.small, 20, 110, 132, kColours.copy, name);
				canvas.Panel(108, 188, 820, 720);
				canvas.Text(g.small, 20, 138, 208, kColours.kicker, s_page == Page::Controls ? "CONTROLS" : s_page == Page::States ? "SAVE STATES, CHEATS, AMIIBO" :
					s_page == Page::Cheats ? "CHEATS" : "IN THE GAME");
				canvas.Panel(980, 188, 820, 720);

				struct Item
				{
					const char* id;
					std::string label, value;
					bool setting; // Left and Right change it
					const char* help;
				};
				std::vector<Item> items;
				const int resolution = std::clamp(settings.resolution, 1, 10);
				if (s_page == Page::Main)
					items = {
						{"resume", "Back to the game", "", false, "Closes this menu: the game carries on where it is."},
						{"layout", "Screens", kLayouts[std::clamp(settings.layout, 0, 3)], true,
							"How the two screens share the TV: one above the other, the top one alone, the top one large with the "
							"bottom one beside it, or the two side by side.\nIn the game, touchpad click + R1 goes to the next."},
						{"swap", "Main screen", settings.swapScreens ? "Bottom" : "Top", true,
							"Which screen takes the top screen's place.\nIn the game, touchpad click + L1 swaps them."},
						{"border", "Border", kBorderNames[std::clamp(settings.border, 0, kBorderCount - 1)], true,
							"Artwork around the screens, never over them, with a frame round each: it follows every layout. Also in "
							"the launcher's Settings > Borders."},
						{"resolution", "Internal resolution", fmt::format("{}x  ({}x{})", resolution, 400 * resolution, 240 * resolution), true,
							"How large the 3DS's 3D scenes are drawn before they are scaled to the TV. Higher is sharper and asks "
							"more of the GPU, and each time the game reads a picture back the wait grows with it."},
						{"filter", "Texture filter", kFilters[std::clamp(settings.textureFilter, 0, 5)], true,
							"Smooths the game's textures as they are scaled up. None keeps them as the 3DS draws them.\nA filter "
							"redraws every texture the game loads at the internal resolution: at high resolutions it is the costliest "
							"setting here. If a game stutters, try None first."},
						{"cpu", "CPU clock", fmt::format("{}%", settings.cpuClock), true,
							"How fast the 3DS's CPU runs, against the real one's 100%. Below 100% the PS5 has less to do for each "
							"frame, which can bring a slow game up to full speed, but a game that needs the time may slow down or "
							"misbehave. Above 100% smooths games that dropped frames on the 3DS itself, and asks more of the PS5.\n"
							"It changes at once; 100% is how the 3DS is."},
						{"speed", "Speed limit", settings.speedLimit ? fmt::format("{}%", settings.speedLimit) : "None", true,
							"How fast the game may run, against the 3DS's 100%: above it to hurry through slow scenes, or None for as "
							"fast as the PS5 can. The sound is stretched while it runs faster.\nFor this game only: the next one "
							"starts at 100%."},
						{"performance", "Performance overlay", settings.performance ? "On" : "Off", true,
							"The frame rate and the emulation's speed, in the top left corner."},
						{"volume", "Volume", fmt::format("{}%", settings.volume), true, "The game's sound. Left and Right change it by 10%."},
						{"states", "Save states, cheats, amiibo", "", false, "Save the game exactly where it is, in one of five slots "
							"for this game, and load it again later; turn the game's cheats on and off; and scan an amiibo, as the "
							"desktop Azahar does."},
						{"controls", "Controls", "", false, "Motion controls, the sticks' deadzone and where A and B are. They are "
							"kept for the next games too; every button can be set in the launcher's Settings > Controls."},
						{"library", "Back to the library", s_confirmLibrary ? "Press Cross again" : "", false,
							"Leaves the game for the library. What you have not saved in the game is lost."},
					};
				else if (s_page == Page::States)
				{
					std::string time, message, amiibo, amiiboMessage;
					int cheatsOn = 0, cheatCount = 0;
					{
						std::lock_guard lock(s_mutex);
						if (s_stateSlot <= (int)s_stateTimes.size())
							time = s_stateTimes[s_stateSlot - 1];
						message = s_stateMessage;
						s_amiiboIndex = s_amiibo.empty() ? 0 : std::clamp(s_amiiboIndex, 0, (int)s_amiibo.size() - 1);
						amiibo = s_amiibo.empty() ? "None in /data/ps5cemu/amiibo" : s_amiibo[s_amiiboIndex];
						amiiboMessage = s_extrasMessage;
						cheatCount = (int)s_cheats.size();
						for (const auto& cheat : s_cheats)
							cheatsOn += cheat.second ? 1 : 0;
					}
					items = {
						{"slot", "Slot", fmt::format("{}  {}", s_stateSlot, time.empty() ? "(empty)" : time), true,
							"Which of this game's five slots to save to or load from. Left and Right choose it."},
						{"save", "Save to this slot", message, false,
							"Saves the game exactly as it is now. Anything already in the slot is replaced. Save states go with this "
							"version of the app: a newer one may not load them, so keep saving in the game too."},
						{"load", "Load this slot", s_confirmLoad ? "Press Cross again" : "", false,
							"Goes back to the moment the slot was saved. What you did since then is lost, so it asks twice."},
						{"amiibo", "Amiibo", amiibo, true,
							"Left and Right choose an amiibo dump (.bin) from /data/ps5cemu/amiibo; Cross holds it to the 3DS's reader. "
							"Scan it when the game asks for an amiibo. Without the 3DS's aes_keys.txt in azahar/sysdata, games read "
							"it but cannot save to it."},
						{"noamiibo", "Take the amiibo away", amiiboMessage, false, "Takes the amiibo off the reader, as lifting it off would."},
						{"cheats", "Cheats", cheatCount ? fmt::format("{} of {} on", cheatsOn, cheatCount) : "None for this game", false,
							"Turn the game's cheats on and off. They come from azahar/cheats/<title ID>.txt, in the Gateway format "
							"the desktop Azahar uses."},
						{"back", "Back", "", false, "To the menu's first page."},
					};
				}
				else if (s_page == Page::Cheats)
				{
					static constexpr const char* kCheatIds[] = {"cheat0", "cheat1", "cheat2", "cheat3", "cheat4", "cheat5", "cheat6", "cheat7", "cheat8"};
					constexpr int kShown = (int)std::size(kCheatIds);
					std::vector<std::pair<std::string, bool>> cheats;
					{
						std::lock_guard lock(s_mutex);
						cheats = s_cheats;
					}
					if (s_cheatOffset >= (int)cheats.size())
						s_cheatOffset = 0;
					for (int i = 0; i < kShown && s_cheatOffset + i < (int)cheats.size(); i++)
						items.push_back({kCheatIds[i], cheats[s_cheatOffset + i].first, cheats[s_cheatOffset + i].second ? "On" : "Off", true,
							"Cross, Left or Right turns this cheat on or off. It changes at once and is kept for the next time."});
					if ((int)cheats.size() > kShown)
						items.push_back({"morecheats", "More cheats",
							fmt::format("{}-{} of {}", s_cheatOffset + 1, std::min(s_cheatOffset + kShown, (int)cheats.size()), cheats.size()), false,
							"The next page of this game's cheats."});
					if (cheats.empty())
						items.push_back({"nocheats", "No cheats for this game", "", false,
							"Put the game's cheats in /data/ps5cemu/azahar/cheats/<title ID>.txt (the title ID in 16 hex digits, as "
							"on the right), in the Gateway format the desktop Azahar uses, then start the game again."});
					items.push_back({"extras", "Back", "", false, "To save states, cheats and amiibo."});
				}
				else
					items = {
						{"motion", "Motion controls", settings.motion ? "On" : "Off", true,
							"The DualSense's gyroscope and accelerometer as the 3DS's, for the games that aim or steer by tilting."},
						{"deadzone", "Stick deadzone", fmt::format("{}%", settings.deadzone), true,
							"How far a stick moves before the game sees it. Raise it if something drifts when you let go."},
						{"ab", "A and B", settings.aOnCircle ? "A on Circle" : "A on Cross", true,
							"A on Circle and B on Cross, where the 3DS has them, or A on Cross and B on Circle, with X and Y swapped "
							"to match."},
						{"back", "Back", "", false, "To the menu's first page."},
					};

				int focused = 0;
				// nine rows fill the panel at their full height; more are drawn closer, up to thirteen
				const size_t rows = items.size();
				const float spacing = rows > 12 ? 50 : rows > 11 ? 54 : rows > 10 ? 59 : rows > 9 ? 65 : 72;
				const float height = rows > 12 ? 46 : rows > 11 ? 50 : rows > 10 ? 54 : rows > 9 ? 59 : 64, lift = (64 - height) / 2;
				for (int i = 0; i < (int)items.size(); i++)
				{
					const Item& item = items[i];
					const float y = 250 + i * spacing;
					ImGui::SetCursorScreenPos(canvas.At(138, y));
					const bool chosen = ImGui::InvisibleButton(item.id, {760 * scale, height * scale});
					if (i == 0 && (appearing || s_pageChanged))
					{
						ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
						ImGui::GetCurrentContext()->NavDisableHighlight = false;
						s_pageChanged = false;
					}
					const bool isFocused = ImGui::IsItemFocused();
					if (isFocused)
						focused = i;
					int change = chosen ? 1 : 0; // Cross moves a setting on, Left and Right either way
					if (isFocused && item.setting)
					{
						if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft))
							change = -1;
						else if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight))
							change = 1;
					}
					canvas.Row(138, y, 760, height, isFocused);
					canvas.Text(g.row, 24, 164, y + 17 - lift, kColours.text, item.label);
					canvas.TextRight(g.row, 22, 872, y + 19 - lift, kColours.accent, item.value);
					if (change == 0)
						continue;
					const std::string id = item.id;
					Settings next = settings;
					bool changed = true; // a setting's item; the others set it false
					if (id == "resume")
					{
						CloseMenu();
						changed = false;
					}
					else if (id == "layout")
						next.layout = (settings.layout + change + 4) % 4;
					else if (id == "swap")
						next.swapScreens = !settings.swapScreens;
					else if (id == "border")
						next.border = (std::clamp(settings.border, 0, kBorderCount - 1) + change + kBorderCount) % kBorderCount;
					else if (id == "resolution")
						next.resolution = chosen && resolution >= 10 ? 1 : std::clamp(resolution + change, 1, 10);
					else if (id == "filter")
						next.textureFilter = (settings.textureFilter + change + 6) % 6;
					else if (id == "cpu")
						next.cpuClock = NextClock(settings.cpuClock, change);
					else if (id == "speed")
						next.speedLimit = NextSpeedLimit(settings.speedLimit, change);
					else if (id == "performance")
						next.performance = !settings.performance;
					else if (id == "volume")
						next.volume = chosen && settings.volume >= 100 ? 0 : std::clamp(settings.volume + change * 10, 0, 100);
					else if (id == "controls")
					{
						ShowPage(Page::Controls);
						changed = false;
					}
					else if (id == "states")
					{
						ShowPage(Page::States);
						changed = false;
					}
					else if (id == "slot")
					{
						s_stateSlot = (s_stateSlot - 1 + change + kStateSlots) % kStateSlots + 1;
						s_confirmLoad = false;
						changed = false;
					}
					else if (id == "save" || (id == "load" && s_confirmLoad))
					{
						std::lock_guard lock(s_mutex);
						s_stateRequest = id == "save" ? s_stateSlot : -s_stateSlot;
						s_stateMessage = id == "save" ? "Saving..." : "Loading...";
						s_confirmLoad = false;
						changed = false;
					}
					else if (id == "load")
					{
						s_confirmLoad = true;
						changed = false;
					}
					else if (id == "amiibo" || id == "noamiibo" || id.rfind("cheat", 0) == 0)
					{
						std::lock_guard lock(s_mutex);
						changed = false;
						if (id == "amiibo" && !chosen && !s_amiibo.empty())
							s_amiiboIndex = (s_amiiboIndex + change + (int)s_amiibo.size()) % (int)s_amiibo.size();
						else if (id == "amiibo" && !s_amiibo.empty())
							s_extrasRequest = {ExtrasRequest::Amiibo, s_amiiboIndex};
						else if (id == "noamiibo")
							s_extrasRequest = {ExtrasRequest::RemoveAmiibo, 0};
						else if (id != "cheats")
							s_extrasRequest = {ExtrasRequest::Cheat, s_cheatOffset + (id[5] - '0')};
					}
					if (id == "cheats")
						ShowPage(Page::Cheats);
					else if (id == "morecheats")
					{
						s_cheatOffset += 9;
						changed = false;
					}
					else if (id == "extras")
					{
						ShowPage(Page::States);
						changed = false;
					}
					else if (id == "nocheats")
						changed = false;
					else if (id == "library")
					{
						changed = false;
						if (s_confirmLibrary)
							s_libraryRequested = true;
						s_confirmLibrary = true;
					}
					else if (id == "motion")
						next.motion = !settings.motion;
					else if (id == "deadzone")
						next.deadzone = chosen && settings.deadzone >= 50 ? 0 : std::clamp(settings.deadzone + change * 5, 0, 50);
					else if (id == "ab")
						next.aOnCircle = !settings.aOnCircle;
					else if (id == "back")
					{
						ShowPage(Page::Main);
						changed = false;
					}
					if (changed)
						Change(next);
				}
				if (items[focused].id != std::string("library"))
					s_confirmLibrary = false;
				if (items[focused].id != std::string("load"))
					s_confirmLoad = false;

				// the right-hand panel: the game, and what the focused item does
				canvas.Text(g.small, 20, 1016, 208, kColours.kicker, "THIS GAME");
				canvas.Text(g.head, 32, 1016, 248, kColours.title, name, 748);
				canvas.Text(g.small, 20, 1016, 378, kColours.kicker, "TITLE ID");
				canvas.TextRight(g.small, 20, 1764, 378, kColours.accent, fmt::format("{:016X}", titleId));
				canvas.draw->AddLine(canvas.At(1016, 434), canvas.At(1764, 434), kColours.line, scale);
				canvas.Text(g.head, 32, 1016, 456, kColours.title, items[focused].label);
				canvas.Text(g.row, 22, 1016, 508, kColours.copy, items[focused].help, 748);

				// the controller hints, along the bottom
				canvas.draw->AddLine(canvas.At(108, 955), canvas.At(1812, 955), kColours.line, scale);
				float x = 108;
				x = canvas.Hint(g.small, x, 973, "cross", "Choose");
				x = canvas.Hint(g.small, x, 973, "leftright", "Change");
				x = canvas.Hint(g.small, x, 973, "circle", s_page != Page::Main ? "Back" : "Back to the game");
				canvas.Hint(g.small, x, 973, "touchpad", "Touchpad click: touch the bottom screen, in the game");
			}
			ImGui::End();
			ImGui::PopStyleColor();

			// Circle or Options closes it, but not the press of Options that opened it; on the
			// controls' page, Circle goes back to the first
			const bool settled = sceKernelGetProcessTime() - s_openedAt > 300000;
			if (settled && (s_pressed & (ps5pad::kCircle | ps5pad::kOptions)) && !(s_buttons & ps5pad::kTouchPad))
			{
				if (s_page != Page::Main && (s_pressed & ps5pad::kCircle))
					ShowPage(s_page == Page::Cheats ? Page::States : Page::Main);
				else
					CloseMenu();
			}
		}

		// -- the keyboard ----------------------------------------------------------------------------

		std::string s_typed;		  // the renderer's
		bool s_shift = false;		  // capitals and the second symbols
		constexpr const char* kKeys[2][4] = {
			{"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.-"},
			{"!?#$%&*()+", "QWERTYUIOP", "ASDFGHJKL\"", "ZXCVBNM;:_"},
		};

		void FinishKeyboard(int button)
		{
			std::lock_guard lock(s_mutex);
			s_keyboardResult = s_typed;
			s_keyboardButton = button;
			s_keyboardDone = true;
			s_keyboardOpen = false;
		}

		void Type(const std::string& characters, int maxLength)
		{
			if (maxLength <= 0 || (int)(s_typed.size() + characters.size()) <= maxLength)
				s_typed += characters;
		}

		// The keyboard, laid out on the launcher's 1920x1080 as the menu is: what the game asks for,
		// the text, the keys, then the game's own buttons (the last confirms, as Options does)
		void DrawKeyboard(float scale)
		{
			ImGuiIO& io = ImGui::GetIO();
			KeyboardRequest request;
			std::string error;
			{
				std::lock_guard lock(s_mutex);
				request = s_keyboard;
				error = s_keyboardError;
				if (s_keyboardFresh)
				{
					s_typed.clear();
					s_shift = false;
					s_keyboardFresh = false;
				}
			}
			if (request.buttons.empty())
				request.buttons = {"OK"};
			const ImVec2 origin{(io.DisplaySize.x - 1920.0f * scale) * 0.5f, (io.DisplaySize.y - 1080.0f * scale) * 0.5f};
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
			ImGui::SetNextWindowFocus();
			ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(0, 0, 0, 0));
			constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
			if (ImGui::Begin("PS5CEMU-HAR##Keyboard3ds", nullptr, kFlags))
			{
				const bool appearing = ImGui::IsWindowAppearing();
				if (appearing)
					ImGui::GetCurrentContext()->NavDisableHighlight = false;
				const Canvas canvas{ImGui::GetWindowDrawList(), scale, origin, kColours};
				canvas.draw->AddRectFilled({0, 0}, io.DisplaySize, kColours.dim);
				canvas.Panel(260, 110, 1400, 860);
				canvas.Text(g.small, 20, 300, 136, kColours.kicker, "THE GAME ASKS FOR");
				canvas.Text(g.head, 32, 300, 170, kColours.title, request.hint.empty() ? "Some text" : request.hint, 1320);
				canvas.Row(300, 236, 1320, 72, false);
				canvas.Text(g.row, 24, 330, 258, kColours.text, s_typed + "_");
				if (request.maxLength > 0)
					canvas.TextRight(g.small, 20, 1590, 262, kColours.accent, fmt::format("{} / {}", s_typed.size(), request.maxLength));
				if (!error.empty())
					canvas.Text(g.small, 20, 300, 320, kColours.accent, error, 1320);

				// a key: true when chosen (Cross, or a touch of the touchpad's click)
				int keyIndex = 0;
				auto key = [&](float x, float y, float width, const std::string& label) {
					ImGui::SetCursorScreenPos(canvas.At(x, y));
					const bool chosen = ImGui::InvisibleButton(fmt::format("##key{}", keyIndex).c_str(), {width * scale, 72 * scale});
					if (keyIndex++ == 0 && appearing)
						ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
					canvas.Row(x, y, width, 72, ImGui::IsItemFocused());
					const ImVec2 size = g.row->CalcTextSizeA(24 * scale, FLT_MAX, 0.0f, label.c_str());
					canvas.draw->AddText(g.row, 24 * scale, canvas.At(x + width / 2 - size.x / scale / 2, y + 22), kColours.text, label.c_str());
					return chosen;
				};
				const auto& rows = kKeys[s_shift ? 1 : 0];
				for (int row = 0; row < 4; row++)
					for (int column = 0; column < 10; column++)
					{
						const std::string character(1, rows[row][column]);
						if (key(325 + column * 128, 360 + row * 84, 118, character))
							Type(character, request.maxLength);
					}
				if (key(325, 696, 246, s_shift ? "Shift: on" : "Shift"))
					s_shift = !s_shift;
				if (key(581, 696, 502, "Space"))
					Type(" ", request.maxLength);
				if (key(1093, 696, 502, "Delete") && !s_typed.empty())
					s_typed.pop_back();
				// the game's buttons, right-aligned on the last row
				const float buttonWidth = 300;
				const int count = (int)request.buttons.size();
				for (int button = 0; button < count; button++)
				{
					const float x = 1595 - (count - button) * (buttonWidth + 10) + 10;
					if (key(x, 800, buttonWidth, request.buttons[button]))
						FinishKeyboard(button);
				}

				canvas.draw->AddLine(canvas.At(300, 900), canvas.At(1620, 900), kColours.line, scale);
				float x = 300;
				x = canvas.Hint(g.small, x, 915, "cross", "Type");
				x = canvas.Hint(g.small, x, 915, "circle", "Delete");
				x = canvas.Hint(g.small, x, 915, "triangle", "Shift");
				canvas.Hint(g.small, x, 915, "options", request.buttons.back());
			}
			ImGui::End();
			ImGui::PopStyleColor();

			// Circle deletes, Triangle shifts, Options confirms with the last button (as on the Wii U's
			// keyboard); not while the touchpad is held for a shortcut
			if (!(s_buttons & ps5pad::kTouchPad))
			{
				if ((s_pressed & ps5pad::kCircle) && !s_typed.empty())
					s_typed.pop_back();
				if (s_pressed & ps5pad::kTriangle)
					s_shift = !s_shift;
				if (s_pressed & ps5pad::kOptions)
					FinishKeyboard((int)request.buttons.size() - 1);
			}
		}

		void DrawPerformance(float scale)
		{
			double fps, speed;
			std::string breakdown;
			{
				std::lock_guard lock(s_mutex);
				fps = s_fps;
				speed = s_speed;
				breakdown = s_breakdown;
			}
			std::string text = fmt::format("{:.0f} FPS   {:.0f}%", fps, speed);
			if (!breakdown.empty())
				text += "\n" + breakdown;
			ImDrawList* draw = ImGui::GetForegroundDrawList();
			const ImVec2 at{24 * scale, 20 * scale};
			const ImVec2 size = g.small->CalcTextSizeA(20 * scale, FLT_MAX, 0.0f, text.c_str());
			draw->AddRectFilled({at.x - 12 * scale, at.y - 8 * scale}, {at.x + size.x + 12 * scale, at.y + size.y + 8 * scale},
				kColours.panel, 10 * scale);
			draw->AddText(g.small, 20 * scale, at, kColours.title, text.c_str());
		}
	}

	void Start(const std::string& name, uint64_t titleId, const Settings& settings)
	{
		std::lock_guard lock(s_mutex);
		s_name = name;
		s_titleId = titleId;
		s_settings = settings;
		s_changed = false;
	}

	void ToggleMenu()
	{
		if (s_open)
		{
			CloseMenu();
			return;
		}
		s_openedAt = sceKernelGetProcessTime();
		s_open = true;
	}

	bool MenuOpen()
	{
		return s_open;
	}

	bool TakeChanges(Settings& settings)
	{
		std::lock_guard lock(s_mutex);
		if (!s_changed)
			return false;
		s_changed = false;
		settings = s_settings;
		return true;
	}

	bool TakeLibraryRequest()
	{
		return s_libraryRequested.exchange(false);
	}

	void SetBorder(int theme, std::vector<uint8_t> rgba, int width, int height)
	{
		std::lock_guard lock(s_mutex);
		s_borderTheme = rgba.empty() ? 0 : theme;
		s_borderPixels = std::move(rgba);
		s_borderWidth = width;
		s_borderHeight = height;
		s_borderFresh = true;
	}

	void SetScreens(const std::vector<ScreenRect>& screens, float width, float height)
	{
		std::lock_guard lock(s_mutex);
		s_screens = screens;
		s_screensWidth = width;
		s_screensHeight = height;
	}

	void SetStateSlots(const std::vector<std::string>& times)
	{
		std::lock_guard lock(s_mutex);
		s_stateTimes = times;
	}

	void SetStateMessage(const std::string& message)
	{
		std::lock_guard lock(s_mutex);
		s_stateMessage = message;
	}

	void SetExtras(const std::vector<std::string>& amiibo, const std::vector<std::pair<std::string, bool>>& cheats)
	{
		std::lock_guard lock(s_mutex);
		s_amiibo = amiibo;
		s_cheats = cheats;
	}

	void SetExtrasMessage(const std::string& message)
	{
		std::lock_guard lock(s_mutex);
		s_extrasMessage = message;
	}

	bool TakeExtrasRequest(ExtrasRequest& request)
	{
		std::lock_guard lock(s_mutex);
		if (s_extrasRequest.kind == ExtrasRequest::None)
			return false;
		request = s_extrasRequest;
		s_extrasRequest = {};
		return true;
	}

	bool TakeStateRequest(bool& load, int& slot)
	{
		std::lock_guard lock(s_mutex);
		if (!s_stateRequest)
			return false;
		load = s_stateRequest < 0;
		slot = std::abs(s_stateRequest);
		s_stateRequest = 0;
		return true;
	}

	void OpenKeyboard(const KeyboardRequest& request)
	{
		std::lock_guard lock(s_mutex);
		s_keyboard = request;
		s_keyboardError.clear();
		s_keyboardFresh = true;
		s_keyboardDone = false;
		s_keyboardOpen = true;
	}

	bool KeyboardOpen()
	{
		return s_keyboardOpen;
	}

	bool TakeKeyboardResult(std::string& text, int& button)
	{
		std::lock_guard lock(s_mutex);
		if (!s_keyboardDone)
			return false;
		s_keyboardDone = false;
		text = s_keyboardResult;
		button = s_keyboardButton;
		return true;
	}

	void KeyboardError(const std::string& message)
	{
		std::lock_guard lock(s_mutex);
		s_keyboardError = message;
		s_keyboardOpen = true; // the text typed stays, to be corrected
	}

	void SetPerformance(double fps, double speed, const std::string& breakdown)
	{
		std::lock_guard lock(s_mutex);
		s_fps = fps;
		s_speed = speed;
		s_breakdown = breakdown;
	}

	void Record(const Target& target)
	{
		if (g.failed)
			return;
		const float scale = std::max(1.0f, target.height / 1080.0f);
		if (!target.insideRenderPass)
		{
			g.pending = false;
			bool performance;
			{
				std::lock_guard lock(s_mutex);
				performance = s_settings.performance;
			}
			const bool open = s_open;
			const bool keyboard = s_keyboardOpen;
			int borderTheme;
			bool borderFresh;
			{
				std::lock_guard lock(s_mutex);
				borderTheme = s_borderTheme;
				borderFresh = s_borderFresh;
			}
			// textures given up, once the frames that drew them are long done
			for (size_t i = 0; i < g.retired.size();)
				if (++g.retired[i].second > 16)
				{
					ImGui_ImplVulkan_DeleteTexture(g.retired[i].first);
					g.retired.erase(g.retired.begin() + i);
				}
				else
					i++;
			const bool border = borderTheme > 0 && (g.border || borderFresh);
			if (!open && !performance && !keyboard && !border)
			{
				s_buttons = 0; // the menu sees a fresh controller when it next opens
				return;
			}
			if (!g.ready)
			{
				if (!Initialize(target, scale))
				{
					g.failed = true;
					return;
				}
				g.ready = true;
			}
			if (target.renderPass != g.renderPass && target.width == g.width && target.height == g.height)
			{
				// the screen's pass made again: Azahar starts its presentation over when a save state is
				// loaded. The new pass has the old one's formats, so ImGui's pipeline is compatible with
				// it and only the handle changes (tearing ImGui down and up again crashed in the driver).
				g.renderPass = target.renderPass;
				ps5log::Line("[ingame3ds] the screen's render pass was made again: the menu follows it");
			}
			if (target.renderPass != g.renderPass)
				return; // another pass than the screen's (a screenshot's)
			ImGui::SetCurrentContext(g.context);
			// the font, once; its staging buffer goes once the copy has long run
			if (!g.fontsUploaded)
			{
				ImGui_ImplVulkan_CreateFontsTexture(target.commandBuffer);
				g.fontsUploaded = true;
				g.uploadAge = 0;
			}
			else if (g.uploadAge >= 0 && ++g.uploadAge > 16)
			{
				ImGui_ImplVulkan_DestroyFontUploadObjects();
				g.uploadAge = -1;
			}
			// a new border picture: uploaded here, outside the render pass, as the font is
			if (borderFresh)
			{
				std::vector<uint8_t> pixels;
				int width, height;
				{
					std::lock_guard lock(s_mutex);
					pixels.swap(s_borderPixels);
					width = s_borderWidth;
					height = s_borderHeight;
					s_borderFresh = false;
				}
				if (g.border)
					g.retired.emplace_back(g.border, 0);
				g.border = nullptr;
				if (!pixels.empty())
					g.border = ImGui_ImplVulkan_GenerateTexture(target.commandBuffer, pixels, {width, height});
			}
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = {(float)target.width, (float)target.height};
			const uint64_t now = sceKernelGetProcessTime();
			io.DeltaTime = g.lastFrame && now > g.lastFrame ? std::min((now - g.lastFrame) / 1e6f, 0.25f) : 1.0f / 60.0f;
			g.lastFrame = now;
			Input();
			ImGui::NewFrame();
			if (borderTheme > 0 && g.border)
				DrawBorder(borderTheme);
			if (open)
				DrawMenu(scale);
			else if (keyboard)
				DrawKeyboard(scale);
			if (performance)
				DrawPerformance(scale);
			ImGui::Render();
			g.pending = true;
			return;
		}
		if (!g.pending || target.renderPass != g.renderPass)
			return;
		g.pending = false;
		ImGui::SetCurrentContext(g.context);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), target.commandBuffer);
	}
}
