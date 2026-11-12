// SPDX-License-Identifier: GPL-3.0-or-later
#include "ingame3ds.h"
#include "buttons_menu.h"
#include "emulator.h"
#include "keyboard_menu.h"
#include "lang.h"
#include "loading_screen.h"
#include "paths.h"
#include "menu_canvas.h"
#include "remote_menu.h"
#include "side_menu.h"
#include "tga.h"
#include "../azahar/controls.h"
#include "../ps5/kernel.h"
#include "../ui/text.h"
#include "../ps5/log.h"
#include "../ps5/pad.h"

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "imgui/imgui_impl_vulkan.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <mutex>
#include <utility>

// ui/images.cpp's: a picture file decoded as RGBA rows, with the launcher's stb_image
namespace ui
{
	bool DecodeFile(const std::string& path, std::vector<uint8_t>& rgba, int& width, int& height);
}

// Cemu's overlay font, compressed into Cemu (resource/CafeDefaultFont.cpp)
uint8* extractCafeDefaultFont(sint32* size);

namespace ps5ingame3ds
{
	namespace
	{
		using ps5menu::Canvas;
		using ps5lang::Percent;
		using ps5lang::Tr;
		using ps5lang::TrC;
		using ps5lang::TrF;
		using ps5lang::TrMark;
		using ps5lang::TrMarkC;

		// What the game's loop and the menu share
		std::mutex s_mutex;
		Settings s_settings;  // under s_mutex
		bool s_changed = false;
		std::string s_name;
		std::string s_details;	 // GameTDB's publisher and year (SetGame)
		std::string s_coverPath; // the game's box art, or its icon
		uint64_t s_titleId = 0;
		Console s_console = Console::N3ds;
		std::string s_system; // a libretro core's system ("NES"), for the badge
		ps5menu::Palette s_systemColours = ps5menu::kBlue; // its colours, from its own (Start)

		// the side's colours: the 3DS's sand, the DS's green, a libretro core's system's own
		const ps5menu::Palette& Colours()
		{
			return s_console == Console::Nds ? ps5menu::kGreen : s_console == Console::Retro ? s_systemColours : ps5menu::kGold;
		}
		double s_fps = 0, s_speed = 0;
		std::string s_breakdown;
		std::atomic<bool> s_open{false};
		std::atomic<uint64_t> s_openedAt{0}; // sceKernelGetProcessTime
		std::atomic<bool> s_libraryRequested{false};
		std::vector<std::string> s_stateTimes; // under s_mutex, with the four below
		std::string s_stateMessage;
		int s_stateRequest = 0; // a save to slot n (n), a load from it (-n)
		bool s_undoAvailable = false; // the game as it was before the last load is kept (SetUndoAvailable)
		bool s_undoRequest = false;
		std::vector<std::string> s_amiibo; // under s_mutex, with the four below
		std::vector<std::pair<std::string, bool>> s_cheats;
		bool s_cheatsShown = true; // a libretro core's: only one that takes cheats has the page (SetCheatsShown)
		bool s_cheevosShown = false; // RetroAchievements, under s_mutex with the two below
		std::string s_cheevosSummary;
		std::vector<Achievement> s_cheevos;
		std::vector<CoreOption> s_coreOptions;			   // under s_mutex: a libretro game's (SetCoreOptions)
		std::vector<std::pair<std::string, int>> s_coreChanged; // under s_mutex: the menu's changes, for TakeCoreOption
		std::atomic<bool> s_resetRequested{false};
		Controllers s_controllers; // under s_mutex: a libretro game's (SetControllers)
		std::atomic<bool> s_levelRequested{false};
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
			uint64_t generation = 0; // Azahar's renderer's (Target::generation)
			VkRenderPass renderPass = VK_NULL_HANDLE;
			uint32_t width = 0, height = 0; // the screen's pass's picture
			ImGui_ImplVulkan_InitInfo info{};
			VkDescriptorPool pool = VK_NULL_HANDLE;
			ImGuiContext* context = nullptr;
			ImFontAtlas* atlas = nullptr;
			ImFont* head = nullptr;	 // the menu's styles (menu_canvas.h): heading
			ImFont* body = nullptr;	 // body
			ImFont* row = nullptr;	 // label
			ImFont* small = nullptr; // caption
			ImFont* chip = nullptr;
			ImFont* big = nullptr; // the loading screen's game name (a 3DS game's only)
			bool fontsUploaded = false;
			int uploadAge = -1;	   // frames since the font upload, until its buffer goes
			bool pending = false;  // a frame's draw data waits for the render pass
			ImTextureID border = nullptr;
			ImTextureID cover = nullptr; // the game's box art, made the first time the menu opens
			ImTextureID pad = nullptr;	 // the Buttons page's DualSense (assets/ui/dualsense.tga), made then too
			bool coverTried = false;
			int coverWidth = 0, coverHeight = 0;
			ps5menu::LoadingPicture blur; // the cover's small copy, the loading screen's ground, made with it
			uint64_t fadeFrom = 0;		  // the game's first frame after the loading screen (sceKernelGetProcessTime)
			std::vector<std::pair<ImTextureID, int>> retired; // textures and their age in frames, until the GPU is done with them
			uint64_t lastFrame = 0;
		};
		Gpu g;
		// The loading screen (SetLoading), under s_mutex: what Azahar is doing and how far; whether it is
		// up, and once it ended, that it fades over the game's first frames
		LoadingStep s_loadingStep = LoadingStep::Loading;
		size_t s_loadingDone = 0, s_loadingOf = 0;
		std::atomic<bool> s_loading{false}, s_loadingFades{false};
		uint64_t s_loadingFrom = 0; // sceKernelGetProcessTime

		void DrawLoading(float scale, float alpha)
		{
			ps5menu::Loading loading;
			LoadingStep step;
			size_t done, of;
			{
				std::lock_guard lock(s_mutex);
				loading.title = s_name;
				loading.details = s_details;
				step = s_loadingStep;
				done = std::min(s_loadingDone, s_loadingOf);
				of = s_loadingOf;
			}
			loading.system = "Nintendo 3DS";
			if (step == LoadingStep::Starting)
				loading.step = ps5menu::StartingStep();
			else if (step == LoadingStep::Loading || of == 0)
				loading.step = ps5menu::LoadingStep();
			else
			{
				loading.step = step == LoadingStep::Pipelines ? ps5menu::PipelineStep(done, of) : ps5menu::ShaderStep(done, of);
				loading.progress = (float)done / of;
			}
			loading.cover = {g.cover, g.coverWidth, g.coverHeight};
			loading.blur = g.blur;
			ps5menu::DrawLoading(ImGui::GetForegroundDrawList(), ImGui::GetIO().DisplaySize, scale, Colours(),
				{g.big, g.chip, g.body, g.row, g.small}, loading, (sceKernelGetProcessTime() - s_loadingFrom) / 1e6f, alpha);
		}
		uint32_t s_buttons = 0, s_pressed = 0;
		uint32_t s_menuButtons = 0; // the same, with the left stick's directions as the D-pad's

		// The menu's Buttons (buttons_menu.h): the wait for a press, the input held now for its test's row, and
		// the console's buttons as the side's settings name them (ps5azahar's): the 3DS's, or the DS's, which
		// the libretro sides share
		ps5menu::ButtonsMenu s_buttonsMenu;
		int s_heldInput = ps5menu::kInputNone;
		static_assert((int)ps5emu::PadInput::TouchPad == ps5menu::kInputTouchPad && (int)ps5emu::PadInput::Options == ps5menu::kInputOptions &&
					  (int)ps5emu::PadInput::RightStickUp == ps5menu::kInputRightStickUp);

		ps5settings::N3ds Mapped(const Settings& settings)
		{
			ps5settings::N3ds mapped;
			mapped.buttons = settings.buttons;
			return mapped;
		}

		// The words of a libretro game's special controllers (port/libretro/devices.h, game.cpp): marked here so
		// that the in-game fonts have their characters, translated where they show
		std::string ControllerWords(const std::string& english)
		{
			using ps5lang::TrMark;
			// tr: the Buttons page's Controller row: the game's own choice, the console's pad, a special controller
			static constexpr const char* kWords[] = {TrMark("Auto"), TrMark("Standard"), TrMark("Arkanoid paddle"),
				// tr: what the DualSense does with a special controller plugged in (the Arkanoid paddle, the
				// Zapper or Super Scope light guns, the SNES Mouse, the Power Pad mat)
				TrMark("Turn the knob"), TrMark("Fire"), TrMark("Aim"), TrMark("Aim at the middle"), TrMark("Shoot away from the screen"),
				TrMark("Cursor"), TrMark("Turbo"), TrMark("Pause"), TrMark("Move"), TrMark("Left button"), TrMark("Right button"),
				// tr: {0} is a number on the Power Pad's mat
				TrMark("Mat {0}"), TrMark("Tilt"),
				// tr: the DualSense's inputs for a special controller
				TrMark("Touchpad, left stick or D-pad"), TrMark("Touchpad, motion or right stick"), TrMark("Touchpad or left stick"),
				TrMark("Cross or R2"), TrMark("Circle or L2"), TrMark("Tilt or turn the DualSense")};
			(void)kWords;
			// a Power Pad's button: "Mat 7"
			if (english.rfind("Mat ", 0) == 0 && english.size() > 4 && std::isdigit((unsigned char)english[4]))
				return ps5lang::TrF("Mat {0}", std::atoi(english.c_str() + 4));
			const std::string words = ps5lang::Tr(english.c_str());
			return words != english ? words : ps5menu::ButtonLabel(english);
		}

		std::vector<ps5menu::ButtonRow> ButtonRows(const Settings& settings)
		{
			const ps5settings::N3ds mapped = Mapped(settings);
			std::vector<ps5menu::ButtonRow> rows;
			if (s_console == Console::N3ds)
				for (const auto& mapping : ps5azahar::ListMappings(mapped))
					rows.push_back({mapping.button, mapping.input});
			else
				for (const ps5azahar::Button button : ps5azahar::DsButtons())
				{
					const auto mapping = ps5azahar::Mapping(mapped, button, true);
					rows.push_back({mapping.button, mapping.input});
				}
			return rows;
		}

		// A button of the list given a DualSense input (the one that had it takes this one's), or every
		// button back: the A and B row says what came of it
		void SetButton(Settings& settings, int index, ps5emu::PadInput input, bool reset)
		{
			ps5settings::N3ds mapped = Mapped(settings);
			const bool ds = s_console != Console::N3ds;
			if (reset)
				mapped.buttons.clear();
			else if (index >= 0 && (!ds || index < (int)ps5azahar::DsButtons().size()))
				ps5azahar::SetMapping(mapped, ds ? (size_t)ps5azahar::DsButtons()[index] : (size_t)index, input, ds);
			settings.buttons = mapped.buttons;
			settings.aOnCircle = ps5azahar::MappedInput(mapped, ps5azahar::Button::A) == ps5emu::PadInput::Circle;
		}
		// the menu's panel: its place in the list and the open category (the renderer's), and a new
		// opening, asked for by ToggleMenu on the game's loop
		ps5menu::SideMenu s_side;
		std::atomic<bool> s_menuFresh{false};
		int s_stateSlot = 1;
		std::string s_stateAction; // the last save or load asked for in this opening: its tile shows how it went

		// tr: the 3DS's screen layouts
		constexpr const char* kLayouts[] = {TrMark("Top above bottom"), TrMark("Top screen only"), TrMark("Large top screen"), TrMark("Side by side")};
		// tr: texture filters: None, then the filters' own names
		constexpr const char* kFilters[] = {TrMarkC("texture filter", "None"), "Anime4K", TrMark("Bicubic"), "ScaleForce", "xBRZ", "MMPX"};

		std::string LayoutName(int layout)
		{
			return Tr(kLayouts[std::clamp(layout, 0, 3)]);
		}

		std::string FilterName(int filter)
		{
			filter = std::clamp(filter, 0, 5);
			if (filter == 1)
				filter = 0; // Anime4K, OpenGL's only: Azahar's Vulkan renderer draws with none
			return filter == 0 ? TrC("texture filter", kFilters[0]) : Tr(kFilters[filter]);
		}

		std::string BorderName(int border)
		{
			border = std::clamp(border, 0, kBorderCount - 1);
			return border + 1 < kBorderCount ? TrC("border", kBorderNames[border]) : kBorderNames[border];
		}
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
			{0.55f, IM_COL32(255, 255, 255, 30), false}, // PS5-ECAM
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

		// Cemu's Vulkan entry points from Azahar's instance and device (Cemu's renderer never ran),
		// a descriptor pool for the font, an ImGui context of its own with Cemu's font at the
		// menu's four sizes, and ImGui's Vulkan backend on Azahar's render pass.
		bool CreatePool(VkDevice device)
		{
			const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
			VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			poolInfo.maxSets = 16; // the font, the border, borders on their way out
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &size;
			return vkCreateDescriptorPool(device, &poolInfo, nullptr, &g.pool) == VK_SUCCESS;
		}

		void InitializeBackend(const Target& target)
		{
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
			g.generation = target.generation;
			g.renderPass = target.renderPass;
			g.width = target.width;
			g.height = target.height;
		}

		// Azahar made its renderer again, device and all (it does when a save state is loaded): what
		// the menu made on the old device went with it, so it is forgotten, not destroyed, and made
		// again on the new one. The ImGui context and the fonts' atlas stay.
		bool Reattach(const Target& target)
		{
			ps5log::Line("[ingame3ds] Azahar made its renderer again: the menu starts over on the new device");
			ImGui::SetCurrentContext(g.context);
			ImGui_ImplVulkan_ForgetDeviceObjects();
			g.border = nullptr;
			g.cover = nullptr;
			g.pad = nullptr;
			g.coverTried = false;
			g.retired.clear();
			g.fontsUploaded = false;
			g.uploadAge = -1;
			g.pending = false;
			g.pool = VK_NULL_HANDLE;
			if (!InitializeInstanceVulkan(target.instance) || !InitializeDeviceVulkan(target.device) || !CreatePool(target.device))
			{
				ps5log::Line("[ingame3ds] the menu could not start on the new device: no menu");
				return false;
			}
			InitializeBackend(target);
			std::lock_guard lock(s_mutex);
			s_borderFresh = s_borderTheme > 0 && !s_borderPixels.empty(); // the border's picture again
			return true;
		}

		bool Initialize(const Target& target, float scale)
		{
			if (!InitializeGlobalVulkan() || !InitializeInstanceVulkan(target.instance) || !InitializeDeviceVulkan(target.device))
			{
				ps5log::Line("[ingame3ds] Cemu's Vulkan commands did not load from Azahar's device: no menu");
				return false;
			}
			if (!CreatePool(target.device))
			{
				ps5log::Line("[ingame3ds] no descriptor pool: no menu");
				return false;
			}

			// Lexend's three weights at the menu's sizes, or the Wii U's system font if they are missing, with
			// the console's font for what Lexend lacks (a Russian menu, a Japanese title), cut to the
			// characters the language's words and the game's name have
			std::string title;
			{
				std::lock_guard lock(s_mutex);
				title = s_name;
			}
			const ImWchar* ranges = ps5menu::GlyphRanges(title);
			int mergeFace = 0;
			const auto& merge = ps5menu::MergeFont(mergeFace);
			g.atlas = new ImFontAtlas();
			auto add = [&](ps5menu::Weight weight, float size) {
				ImFontConfig config{};
				config.FontDataOwnedByAtlas = false; // kept: the atlas reads them
				const auto& file = ps5menu::FontFile(weight);
				if (!file.empty())
				{
					config.OversampleH = 2;
					ImFont* font = g.atlas->AddFontFromMemoryTTF((void*)file.data(), (int)file.size(), size * scale, &config, ranges);
					if (!merge.empty())
					{
						ImFontConfig second{};
						second.FontDataOwnedByAtlas = false;
						second.MergeMode = true;
						second.FontNo = mergeFace;
						second.OversampleH = 2;
						g.atlas->AddFontFromMemoryTTF((void*)merge.data(), (int)merge.size(), size * scale, &second, ranges);
					}
					return font;
				}
				static sint32 fallbackSize = 0;
				static uint8* fallback = extractCafeDefaultFont(&fallbackSize);
				return g.atlas->AddFontFromMemoryTTF(fallback, fallbackSize, size * scale, &config);
			};
			using ps5menu::Weight;
			g.head = add(Weight::SemiBold, ps5menu::kHeading);
			g.body = add(Weight::Medium, ps5menu::kBody);
			g.row = add(Weight::Regular, ps5menu::kLabel);
			g.small = add(Weight::Regular, ps5menu::kCaption);
			g.chip = add(Weight::SemiBold, ps5menu::kChip);
			if (s_console == Console::N3ds)
				g.big = add(Weight::SemiBold, ps5menu::kLoadingTitle);
			g.context = ImGui::CreateContext(g.atlas);
			ImGui::SetCurrentContext(g.context);
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

			InitializeBackend(target);
			ps5log::Line("[ingame3ds] the menu is ready ({} images in flight{})", g.info.ImageCount,
				merge.empty() ? std::string() : fmt::format("; its second font {}, {} KiB", ps5lang::GetMenuFont().path, merge.size() >> 10));
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
			s_menuButtons = buttons;
			if (connected)
			{
				if (data.leftY < 64)
					s_menuButtons |= ps5pad::kUp;
				else if (data.leftY > 192)
					s_menuButtons |= ps5pad::kDown;
				if (data.leftX < 64)
					s_menuButtons |= ps5pad::kLeft;
				else if (data.leftX > 192)
					s_menuButtons |= ps5pad::kRight;
			}
			io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
			io.MouseDown[0] = false;
		}

		// The menu's rows as things are now: a few settings, and categories that open in place
		std::vector<ps5menu::Row> MenuRows(const Settings& settings)
		{
			using Row = ps5menu::Row;
			std::string time, stateMessage, amiibo, amiiboMessage;
			std::vector<std::pair<std::string, bool>> cheats;
			bool noAmiibo, undo, cheatsShown;
			bool cheevosShown;
			std::string cheevosSummary;
			std::vector<Achievement> cheevos;
			{
				std::lock_guard lock(s_mutex);
				cheevosShown = s_cheevosShown;
				cheevosSummary = s_cheevosSummary;
				cheevos = s_cheevos;
				if (s_stateSlot <= (int)s_stateTimes.size())
					time = s_stateTimes[s_stateSlot - 1];
				stateMessage = s_stateMessage;
				undo = s_undoAvailable;
				s_amiiboIndex = s_amiibo.empty() ? 0 : std::clamp(s_amiiboIndex, 0, (int)s_amiibo.size() - 1);
				noAmiibo = s_amiibo.empty();
				amiibo = noAmiibo ? Tr("None") : s_amiibo[s_amiiboIndex];
				amiiboMessage = s_extrasMessage;
				cheats = s_cheats;
				cheatsShown = s_cheatsShown;
			}
			const int resolution = std::clamp(settings.resolution, 1, 10);
			const bool ds = s_console == Console::Nds;
			const bool single = s_console == Console::Retro; // one screen: the DS's rows that apply to it
			const char* filter = kScreenFilters[std::clamp(settings.screenFilter, 0, kScreenFilterCount - 1)];
			std::vector<Row> rows;
			const std::string on = Tr("On"), off = Tr("Off");
			if (single)
				rows.push_back({"screens", Tr("Screen and border"), BorderName(settings.border), false, Tr("The border around the screen."), {
					{"border", Tr("Border"), BorderName(settings.border), true, Tr("Artwork around the screen.")},
				}});
			else
			{
			rows.push_back({"screens", Tr("Screens and border"), fmt::format("{} \u00b7 {}", LayoutName(settings.layout), BorderName(settings.border)), false,
				Tr("Screen layout, main screen and border."), {
				{"layout", Tr("Layout"), LayoutName(settings.layout), true,
					Tr("How the two screens fit on the TV. In the game, touchpad click + R1 changes it.")},
				// tr: the 3DS's top and bottom screens
				{"swap", Tr("Main screen"), settings.swapScreens ? Tr("Bottom") : Tr("Top"), true,
					Tr("Which screen goes on top. In the game, touchpad click + L1 swaps them.")},
				{"border", Tr("Border"), BorderName(settings.border), true,
					Tr("Artwork around the screens. Also in Settings > Screens.")},
			}});
			// the console turned with its screens, for the games played holding it on its side
			rows.back().rows.insert(rows.back().rows.begin() + 2,
				{"rotation", Tr("Screen rotation"), Tr(kRotations[std::clamp(settings.rotation, 0, kRotationCount - 1)]), true,
					ds ? Tr("For games played with the DS held sideways, like a book. Both screens turn together.") :
						 Tr("For games played with the 3DS held sideways, like a book. Both screens and touch turn together.")});
			// a gap between the screens, as the hinge makes: also a quick action
			rows.back().rows.insert(rows.back().rows.begin() + 3,
				{"gap", Tr("Screen gap"), settings.screenGap ? on : off, true,
					Tr("Adds a gap between the two screens, like the console's hinge.")});
			// the bottom screen touched while only the top one shows
			rows.back().rows.insert(rows.back().rows.begin() + 4,
				{"hiddentouch", Tr("Touch without the bottom screen"), settings.hiddenTouch ? on : off, true,
					Tr("With only the top screen shown, the touchpad still touches the bottom screen, with a cursor on the top one.")});
			}
			if (single)
			{
				// the picture: its size and shape on the TV
				const int size = std::clamp(settings.pictureSize, 0, kPictureSizeCount - 1), shape = std::clamp(settings.pictureShape, 0, kPictureShapeCount - 1);
				rows.push_back({"picture", Tr("Picture"), fmt::format("{} \u00b7 {}", Tr(kPictureSizes[size]), Tr(kPictureShapes[shape])), false,
					Tr("How big the picture is on the TV, and its shape."), {
					{"picturesize", Tr("Size"), Tr(kPictureSizes[size]), true,
						Tr("Fit makes it as large as the screen allows. Whole pixels keeps every pixel the same size, with a frame around it. Fill screen stretches it edge to edge.")},
					{"pictureshape", Tr("Shape"), Tr(kPictureShapes[shape]), true,
						Tr("Original is the console's own. 4:3 is an old TV's. Square pixels shows them as the game draws them.")},
				}});
				// the emulator's own options, as Settings > Emulator has them, changed for this game's console now
				std::vector<CoreOption> options;
				{
					std::lock_guard lock(s_mutex);
					options = s_coreOptions;
				}
				if (!options.empty())
				{
					Row emulator{"emulator", Tr("Emulator"), ps5lang::TrP((long long)options.size(), "{0} option", "{0} options"), false,
						Tr("Every option of this console's emulator. Changes apply now and are kept for every game on it.")};
					for (const CoreOption& option : options)
						emulator.rows.push_back({"core:" + option.key, option.label,
							option.values.empty() ? std::string() : option.values[std::clamp(option.index, 0, (int)option.values.size() - 1)], true, option.help});
					rows.push_back(std::move(emulator));
				}
			}
			if (single)
				rows.push_back({"graphics", Tr("Graphics"), Tr(filter), false, Tr("The screen filter and the performance overlay."), {
					{"dsfilter", Tr("Screen filter"), Tr(filter), true,
						Tr("Sharp keeps pixels crisp and even. Smooth blends them. CRT draws the lines and colours of a tube TV, Curved CRT its rounded glass too.")},
					{"performance", Tr("Performance overlay"), settings.performance ? on : off, true,
						Tr("Shows frame rate and speed in the top-left corner.")},
				}});
			else if (ds)
				rows.push_back({"graphics", Tr("Graphics"), Tr(filter), false, Tr("The screen filter and the performance overlay."), {
					{"dsfilter", Tr("Screen filter"), Tr(filter), true,
						Tr("How the DS screens are scaled. Sharp keeps pixels crisp and even, Smooth blends them, Square pixels can look uneven.")},
					{"performance", Tr("Performance overlay"), settings.performance ? on : off, true,
						Tr("Shows frame rate and speed in the top-left corner.")},
				}});
			else
			{
				rows.push_back({"graphics", Tr("Graphics"), fmt::format("{}x \u00b7 {}", resolution, FilterName(settings.textureFilter)), false,
					Tr("Internal resolution, texture filter and the performance overlay."), {
					{"resolution", Tr("Internal resolution"), fmt::format("{}x  ({}x{})", resolution, 400 * resolution, 240 * resolution), true,
						Tr("Higher draws 3D scenes sharper but needs more power.")},
					{"filter", Tr("Texture filter"), FilterName(settings.textureFilter), true,
						Tr("Smooths textures. If a game stutters, try None first.")},
					{"framegen", Tr("Frame generation"), settings.frameGeneration ? on : off, true,
						Tr("Adds a frame between each pair, so 30 fps games play at 60.")},
					{"performance", Tr("Performance overlay"), settings.performance ? on : off, true,
						Tr("Shows frame rate and speed in the top-left corner.")},
				}});
			}
			// tr: no speed limit
			Row speedLimit{"speed", Tr("Speed limit"), settings.speedLimit ? Percent(settings.speedLimit) : std::string(TrC("speed limit", "None")), true,
				Tr("Above 100% speeds through slow parts. None runs as fast as possible. This game only.")};
			// the same word on the category as on its row
			const std::string speed = settings.speedLimit ? Percent(settings.speedLimit) : std::string(TrC("speed limit", "None"));
			if (ds || single)
				rows.push_back({"pace", Tr("Speed"), speed, false, Tr("How fast the game may run."), {speedLimit}});
			else
			{
				rows.push_back({"pace", Tr("Speed"), speed, false, Tr("CPU clock and speed limit."), {
					{"cpu", Tr("CPU clock"), Percent(settings.cpuClock), true,
						Tr("Lower can help a slow game reach full speed. Higher smooths games that dropped frames on the 3DS.")},
					speedLimit,
				}});
			}
			Row volume{"volume", Tr("Volume"), Percent(settings.volume), true, Tr("The game's sound. Left and Right change it by 10%.")};
			volume.slider = settings.volume / 100.0f;
			rows.push_back({"states", Tr("Save states"), TrF("Slot {0}", s_stateSlot), false,
				Tr("Save exactly where you are and come back later. Five slots per game."), {
				// tr: a save state slot's number, then when it was saved (or empty)
				{"slot", Tr("Slot"), fmt::format("{}  {}", s_stateSlot, time.empty() ? Tr("(empty)") : time), true,
					Tr("The slot to save to or load from. In the game, touchpad click + Square saves, and holding touchpad click + Triangle loads.")},
				{"save", Tr("Save to this slot"), stateMessage, false,
					Tr("Saves now, replacing what is in the slot. Keep saving in the game too: updates may not load old states.")},
			}});
			Row cheatRows{"cheats", Tr("Cheats"), "", false,
				single ? Tr("The game's cheats, from cheats/<game file>.cht in its system's folder (RetroArch's format).") :
				ds	   ? Tr("The game's cheats, from melonds/cheats/<game file>.mch (melonDS's format).") :
						 Tr("The game's cheats, from azahar/cheats/<title ID>.txt (Gateway format).")};
			for (size_t i = 0; i < cheats.size(); i++)
				cheatRows.rows.push_back({fmt::format("cheat{}", i), cheats[i].first, cheats[i].second ? on : off, true,
					Tr("Cross, Left or Right turns it on or off. It is remembered.")});
			if (cheats.empty())
				cheatRows.rows.push_back({"nocheats", Tr("No cheats for this game"), "", false,
					// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
					single ? Tr("Get cheats in the game's settings in the library, or put a .cht file in the system's cheats folder, then restart the game.") :
					ds ? TrF("Get cheats in the game's settings in the library, or put them in {0}/melonds/cheats/<game file>.mch, then restart the game.", ecam_layout::Parent(ps5paths::MelonDs())) :
						 // tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
						 TrF("Get cheats in the game's settings in the library, or put them in {0}/azahar/cheats/<title ID>.txt, then restart the game.", ecam_layout::Parent(ps5paths::Azahar()))});
			else
				// tr: how many of the game's cheats are on: {0} of {1}
				cheatRows.value = TrF("{0} of {1} on", std::count_if(cheats.begin(), cheats.end(), [](const auto& c) { return c.second; }), cheats.size());
			if (!single || cheatsShown)
				rows.push_back(cheatRows);
			if (cheevosShown) // the DS's and a libretro side's (app/achievements.h)
			{
				// RetroAchievements (6.16): how many are earned, then each, the earned first
				const int earned = (int)std::count_if(cheevos.begin(), cheevos.end(), [](const Achievement& a) { return a.unlocked; });
				Row cheevoRows{"cheevos", Tr("Achievements"), cheevos.empty() ? std::string() : TrF("{0} of {1}", earned, cheevos.size()), false,
					cheevosSummary.empty() ? std::string(Tr("This game's RetroAchievements.")) : cheevosSummary};
				for (size_t i = 0; i < cheevos.size(); i++)
				{
					const Achievement& a = cheevos[i];
					// tr: an achievement earned, and when ({0})
					cheevoRows.rows.push_back({fmt::format("cheevo{}", i), a.title,
						a.unlocked ? (a.when.empty() ? std::string(Tr("Earned")) : TrF("Earned {0}", a.when)) : ps5lang::TrP((int)a.points, "{0} point", "{0} points"),
						false, a.description});
				}
				if (cheevos.empty())
					cheevoRows.rows.push_back({"nocheevos", cheevosSummary.empty() ? std::string(Tr("No achievements")) : cheevosSummary, "", false,
						Tr("RetroAchievements has no set for this game yet.")});
				rows.push_back(cheevoRows);
			}
			if (!ds && !single)
			{
				rows.push_back({"amiibos", Tr("Amiibo"), amiibo, false, Tr("Scan an amiibo when the game asks for one."), {
					{"amiibo", Tr("Amiibo"), amiibo, true,
						// tr: {0} is the app's data folder: /data/ps5cemu, or the PS5CEMU-HAR-data folder on the drive the app is installed on
						noAmiibo ? TrF("Put amiibo dumps (.bin) in {0}/amiibo to scan them here.", ps5paths::Root()) :
								   Tr("Left and Right choose an amiibo. Cross scans it.")},
					{"noamiibo", Tr("Remove amiibo"), amiiboMessage, false, Tr("Takes the amiibo off the reader.")},
				}});
			}
			rows.push_back(volume);
			const std::string ab = settings.aOnCircle ? Tr("A on Circle") : Tr("A on Cross");
			if (single)
				rows.push_back({"controls", Tr("Controls"), ab, false, Tr("Stick deadzone and button layout."), {
					{"deadzone", Tr("Stick deadzone"), Percent(settings.deadzone), true,
						Tr("How far the left stick moves before it presses the D-pad. Raise it if there is drift.")},
					{"ab", Tr("A and B"), ab, true,
						Tr("A on Circle and B on Cross like Nintendo's layout, or swapped, with X and Y swapped too.")},
				}});
			else if (ds)
				// tr: the DS's lid, closed or open
				rows.push_back({"controls", Tr("Controls"), settings.lidClosed ? std::string(Tr("Lid closed")) : ab, false,
					Tr("Lid, stick deadzone and button layout. Hold R3 to blow into the mic."), {
					{"lid", Tr("Lid"), settings.lidClosed ? Tr("Closed") : Tr("Open"), true,
						Tr("Closes the DS lid for games that ask you to. Choose it again to open it.")},
					{"deadzone", Tr("Stick deadzone"), Percent(settings.deadzone), true,
						Tr("How far the left stick moves before it presses the D-pad. Raise it if there is drift.")},
					{"ab", Tr("A and B"), ab, true,
						Tr("A on Circle and B on Cross as on the DS, or swapped, with X and Y swapped too.")},
				}});
			else
			{
				rows.push_back({"controls", Tr("Controls"), ab, false, Tr("Motion controls, stick deadzone and button layout."), {
					{"motion", Tr("Motion controls"), settings.motion ? on : off, true,
						Tr("Use DualSense motion to aim or steer by tilting.")},
					{"deadzone", Tr("Stick deadzone"), Percent(settings.deadzone), true,
						Tr("How far a stick moves before the game sees it. Raise it if there is drift.")},
					{"ab", Tr("A and B"), ab, true,
						Tr("A on Circle and B on Cross as on the 3DS, or swapped, with X and Y swapped too.")},
				}});
			}
			// every button, as Settings > Controls > Buttons has them, set here without leaving the game; the
			// touchpad is the touch screen, so its click cancels
			Row buttons = s_buttonsMenu.Category(ButtonRows(settings), s_heldInput, sceKernelGetProcessTime(), true,
				Tr("Puts every button back to default. Hold Cross to confirm."));
			if (single)
			{
				// a libretro game's special controller (port/libretro/devices.h): which, and what the DualSense does
				// with it, under the test's row
				Controllers controllers;
				{
					std::lock_guard lock(s_mutex);
					controllers = s_controllers;
				}
				std::vector<Row> special;
				if (!controllers.choices.empty())
				{
					const int count = (int)controllers.choices.size();
					const int choice = std::clamp(settings.controller, 0, count - 1);
					const std::string value = choice > 0 ? ControllerWords(controllers.choices[choice]) :
						controllers.found.empty() ? ControllerWords("Auto") : TrF("Auto: {0}", ControllerWords(controllers.found));
					// tr: the Buttons page's row that plugs a special controller in, for this game: Auto, Standard, the Zapper...
					special.push_back({"controller", Tr("Controller"), value, true,
						Tr("The controller plugged into the console for this game. Auto picks the one the game was made for, such as the Zapper for Duck Hunt.")});
				}
				for (size_t i = 0; i < controllers.mapping.size(); i++)
					special.push_back({"special:" + std::to_string(i), ControllerWords(controllers.mapping[i].first),
						ControllerWords(controllers.mapping[i].second), false, Tr("What the DualSense does for the controller plugged in.")});
				if (controllers.motion)
					// tr: a row that takes how the DualSense is held now as level, for a game played by tilting it
					special.push_back({"level", Tr("Level the motion"), "", false,
						Tr("Hold the DualSense as you like to play, then press Cross. The game sees it as held level from there.")});
				buttons.rows.insert(buttons.rows.begin() + std::min<size_t>(1, buttons.rows.size()), special.begin(), special.end());
			}
			rows.push_back(std::move(buttons));
			// a home screen tile's game closes the app instead, as the row says (#48)
			Row library = ps5emu::ExitAfterGame() ?
				Row{"library", Tr("Quit to the home screen"), "", false,
					Tr("Closes the game and Playtendoh. Unsaved progress is lost. Hold Cross.")} :
				Row{"library", Tr("Quit to the library"), "", false,
					Tr("Closes the game and goes to the library. Unsaved progress is lost. Hold Cross.")};
			library.apart = true;
			library.hold = true;
			// loading a state, as a row too: held, as the quick action is
			Row load{"load", Tr("Load this slot"), time.empty() ? Tr("Empty") : "", false,
				Tr("Goes back to when the slot was saved. Progress since then is lost. Hold Cross.")};
			load.hold = !time.empty();
			// and the game as it was just before the last load, while there is one: held too
			Row back{"undo", Tr("Undo the last load"), "", false,
				Tr("Goes back to just before the last load. Progress since that load is lost. Hold Cross.")};
			back.hold = true;
			for (Row& row : rows)
				if (row.id == "states")
				{
					row.rows.push_back(load);
					if (undo)
						row.rows.push_back(back);
				}
			if (!single)
				rows.push_back(ps5menu::RemoteRow(ds ? Tr("The touch screen") : Tr("The bottom screen")));
			// what the menu changes, kept for this game alone or for every game on its system (Settings > the
			// game's Game settings has the same, a row at a time)
			rows.push_back({"gameonly", Tr("Settings for this game only"), settings.gameOnly ? on : off, true,
				Tr("On: this game keeps its own settings, and what you change here stays with it. Off: it uses the system's settings from its next start.")});
			rows.push_back(library);
			return rows;
		}

		// The quick actions above the list: back to the game, a state saved to or loaded (held) from
		// the slot the list chooses, and the next layout of the screens
		std::vector<ps5menu::Tile> Tiles(const Settings& settings)
		{
			bool empty;
			std::string message;
			{
				std::lock_guard lock(s_mutex);
				empty = s_stateSlot > (int)s_stateTimes.size() || s_stateTimes[s_stateSlot - 1].empty();
				message = s_stateMessage;
			}
			// tr: the quick actions: big tiles at the top of the in-game menu, a word or two each
			std::vector<ps5menu::Tile> tiles{{"resume", Tr("Resume"), "resume"},
				{"save", Tr("Save state"), "save", TrF("Slot {0}", s_stateSlot)},
				{"load", Tr("Load state"), "load", empty ? TrF("Slot {0} empty", s_stateSlot) : TrF("Slot {0}", s_stateSlot), !empty},
				{"layout", Tr("Screens"), "screens", LayoutName(settings.layout)},
				// tr: the quick action that puts a gap between the two screens, and its state
				{"gap", Tr("Screen gap"), "gap", settings.screenGap ? Tr("On") : Tr("Off")}};
			if (s_console == Console::Retro)
				tiles.resize(3); // one screen: no layouts, no gap
			// tr: the quick action that resets the console, as its reset button does: Cross held, as nothing saved since is kept
			if (s_console != Console::N3ds)
				tiles.insert(tiles.begin() + 3, {"reset", Tr("Reset"), "reset", Tr("Hold Cross"), true,
					Tr("Restarts the game, as the console's reset button does. What isn't saved is lost.")});
			// how the last save or load went, on its tile (Made by another version, Saved slot 1...)
			for (ps5menu::Tile& tile : tiles)
				if (!message.empty() && tile.id == s_stateAction)
				{
					tile.caption = message;
					tile.help = TrF("Slot {0}: {1}", s_stateSlot, message);
				}
			return tiles;
		}

		// What a row chosen or changed does
		void Act(const ps5menu::Action& action, const Settings& settings)
		{
			const std::string& id = action.id;
			const int change = action.change;
			const bool chosen = action.chosen;
			const int resolution = std::clamp(settings.resolution, 1, 10);
			Settings next = settings;
			bool changed = true; // a setting's row; the others set it false
			if (ps5menu::ButtonsMenu::Asked asked; s_buttonsMenu.Act(action, sceKernelGetProcessTime(), asked))
			{
				changed = asked.reset;
				if (asked.reset)
					SetButton(next, -1, ps5emu::PadInput::None, true);
			}
			else if (id == "resume")
			{
				CloseMenu();
				changed = false;
			}
			else if (id == "reset")
			{
				s_resetRequested = true;
				CloseMenu();
				changed = false;
			}
			else if (id == "controller")
			{
				std::lock_guard lock(s_mutex);
				const int count = std::max(1, (int)s_controllers.choices.size());
				next.controller = (std::clamp(settings.controller, 0, count - 1) + (change ? change : 1) + count) % count;
			}
			else if (id == "level")
			{
				s_levelRequested = true;
				changed = false;
			}
			else if (id.rfind("special:", 0) == 0)
				changed = false;
			else if (id == "picturesize")
				next.pictureSize = (std::clamp(settings.pictureSize, 0, kPictureSizeCount - 1) + change + kPictureSizeCount) % kPictureSizeCount;
			else if (id == "pictureshape")
				next.pictureShape = (std::clamp(settings.pictureShape, 0, kPictureShapeCount - 1) + change + kPictureShapeCount) % kPictureShapeCount;
			else if (id.rfind("core:", 0) == 0)
			{
				// an emulator option: the next value, for the game's loop to give the core
				changed = false;
				std::lock_guard lock(s_mutex);
				for (CoreOption& option : s_coreOptions)
					if ("core:" + option.key == id && !option.values.empty())
					{
						const int count = (int)option.values.size();
						option.index = (std::clamp(option.index, 0, count - 1) + (change ? change : 1) + count) % count;
						s_coreChanged.emplace_back(option.key, option.index);
					}
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
			{
				// the next filter, past Anime4K (1), which is Azahar's OpenGL renderer's only
				next.textureFilter = settings.textureFilter == 1 ? 0 : settings.textureFilter;
				do
					next.textureFilter = (next.textureFilter + change + 6) % 6;
				while (next.textureFilter == 1);
			}
			else if (id == "dsfilter")
			{
				const int count = s_console == Console::Retro ? kScreenFilterCount : kDsScreenFilterCount;
				next.screenFilter = (std::clamp(settings.screenFilter, 0, count - 1) + change + count) % count;
			}
			else if (id == "rotation")
				next.rotation = (std::clamp(settings.rotation, 0, kRotationCount - 1) + change + kRotationCount) % kRotationCount;
			else if (id == "hiddentouch")
				next.hiddenTouch = !settings.hiddenTouch;
			else if (id == "gap")
				next.screenGap = !settings.screenGap;
			else if (id == "framegen")
				next.frameGeneration = !settings.frameGeneration;
			else if (id == "lid")
				next.lidClosed = !settings.lidClosed;
			else if (id == "cpu")
				next.cpuClock = NextClock(settings.cpuClock, change);
			else if (id == "speed")
				next.speedLimit = NextSpeedLimit(settings.speedLimit, change);
			else if (id == "performance")
				next.performance = !settings.performance;
			else if (id == "gameonly")
				next.gameOnly = !settings.gameOnly;
			else if (id == "volume")
				next.volume = chosen && settings.volume >= 100 ? 0 : std::clamp(settings.volume + change * 10, 0, 100);
			else if (id == "motion")
				next.motion = !settings.motion;
			else if (id == "deadzone")
				next.deadzone = chosen && settings.deadzone >= 50 ? 0 : std::clamp(settings.deadzone + change * 5, 0, 50);
			else if (id == "ab")
			{
				// A, B, X and Y: on Circle, Cross, Triangle and Square, or on Cross, Circle, Square and Triangle
				using ps5azahar::Button;
				using ps5emu::PadInput;
				const bool circle = !settings.aOnCircle;
				ps5settings::N3ds mapped = Mapped(settings);
				const bool ds = s_console != Console::N3ds;
				ps5azahar::SetMapping(mapped, (size_t)Button::A, circle ? PadInput::Circle : PadInput::Cross, ds);
				ps5azahar::SetMapping(mapped, (size_t)Button::B, circle ? PadInput::Cross : PadInput::Circle, ds);
				ps5azahar::SetMapping(mapped, (size_t)Button::X, circle ? PadInput::Triangle : PadInput::Square, ds);
				ps5azahar::SetMapping(mapped, (size_t)Button::Y, circle ? PadInput::Square : PadInput::Triangle, ds);
				next.buttons = mapped.buttons;
				next.aOnCircle = circle;
			}
			else if (id == ps5menu::kRemoteRow)
			{
				ps5menu::ToggleRemote(); // the app's setting, not the game's
				changed = false;
			}
			else
			{
				changed = false;
				std::lock_guard lock(s_mutex);
				if (id == "slot")
				{
					s_stateSlot = (s_stateSlot - 1 + change + kStateSlots) % kStateSlots + 1;
					s_stateAction.clear();
				}
				else if (id == "load" && (s_stateSlot > (int)s_stateTimes.size() || s_stateTimes[s_stateSlot - 1].empty()))
				{
					s_stateMessage = TrF("Slot {0} is empty", s_stateSlot);
					s_stateAction = id;
				}
				else if (id == "save" || id == "load")
				{
					// loading is held to here (side_menu.h)
					s_stateRequest = id == "save" ? s_stateSlot : -s_stateSlot;
					s_stateAction = id;
					s_stateMessage = id == "save" ? Tr("Saving…") : Tr("Loading…");
				}
				else if (id == "undo" && s_undoAvailable)
				{
					s_undoRequest = true;
					s_stateAction = "load";
					s_stateMessage = Tr("Loading…");
				}
				else if (id == "amiibo" && !chosen && !s_amiibo.empty())
					s_amiiboIndex = (s_amiiboIndex + change + (int)s_amiibo.size()) % (int)s_amiibo.size();
				else if (id == "amiibo" && !s_amiibo.empty())
					s_extrasRequest = {ExtrasRequest::Amiibo, s_amiiboIndex};
				else if (id == "noamiibo")
					s_extrasRequest = {ExtrasRequest::RemoveAmiibo, 0};
				else if (id.rfind("cheat", 0) == 0 && id.size() > 5 && std::isdigit((unsigned char)id[5]))
					s_extrasRequest = {ExtrasRequest::Cheat, std::atoi(id.c_str() + 5)};
				else if (id == "library")
					s_libraryRequested = true; // held to here
			}
			if (changed)
				Change(next);
		}

		void DrawMenu(float scale)
		{
			Settings settings;
			std::string name, details;
			{
				std::lock_guard lock(s_mutex);
				settings = s_settings;
				name = s_name;
				details = s_details;
			}
			if (s_menuFresh.exchange(false))
			{
				// opened: on its first row, every category closed, the shortcut's buttons not counted
				s_side.Reset();
				s_buttonsMenu.Reset();
				s_stateAction.clear();
			}
			// Buttons: player 1's DualSense for the test's row and, while a button waits for its press, for
			// that; the sheet's controller is still meanwhile
			uint32_t menuButtons = s_menuButtons;
			{
				ps5pad::Data data{};
				const bool read = ps5pad::Read(0, data) && !(data.buttons & ps5pad::kIntercepted);
				const bool touchpad = read && (data.buttons & ps5pad::kTouchPad);
				s_heldInput = !read ? ps5menu::kInputNone : touchpad ? ps5menu::kInputTouchPad : ps5menu::HeldInput(data);
				if (s_buttonsMenu.Busy())
				{
					menuButtons = 0;
					const auto asked = read ? s_buttonsMenu.Capture(ps5menu::HeldInput(data), ps5menu::PadIdle(data), touchpad, true, true,
												  sceKernelGetProcessTime()) : ps5menu::ButtonsMenu::Asked{};
					if (asked.index >= 0)
					{
						SetButton(settings, asked.index, (ps5emu::PadInput)asked.input, false);
						Change(settings);
					}
				}
			}
			bool close = false;
			const ps5menu::Action action = s_side.Update(MenuRows(settings), Tiles(settings), menuButtons, sceKernelGetProcessTime(), close);
			if (close)
			{
				CloseMenu();
				return;
			}
			if (!action.id.empty())
			{
				Act(action, settings);
				std::lock_guard lock(s_mutex);
				settings = s_settings; // as the change left them, for the rows drawn below
			}

			ImGuiIO& io = ImGui::GetIO();
			const ImVec2 origin{(io.DisplaySize.x - 1920.0f * scale) * 0.5f, (io.DisplaySize.y - 1080.0f * scale) * 0.5f};
			const Canvas canvas{ImGui::GetForegroundDrawList(), scale, origin, Colours()};
			ps5menu::Header header;
			header.system = s_console == Console::Nds ? "DS" : s_console == Console::Retro ? s_system : "3DS";
			// tr: the in-game menu's badge: the game waits behind the menu
			header.status = Tr("Paused");
			header.title = name;
			header.details = details;
			header.cover = g.cover;
			header.coverWidth = (float)g.coverWidth;
			header.coverHeight = (float)g.coverHeight;
			header.pad = g.pad;
			s_side.Draw(canvas, {g.head, g.body, g.row, g.small, g.chip}, header, MenuRows(settings), Tiles(settings),
				{{"cross", Tr("Choose")}, {"leftright", Tr("Change")}, {"circle", s_side.Open().empty() ? Tr("Back to the game") : Tr("Back")}});
			ps5menu::DrawRemoteCard(canvas, {g.head, g.body, g.row, g.small, g.chip}, s_side.Focus());
		}

		// -- the keyboard ----------------------------------------------------------------------------

		ps5menu::KeyboardSheet s_sheet; // the renderer's

		void FinishKeyboard(int button)
		{
			std::lock_guard lock(s_mutex);
			s_keyboardResult = s_sheet.typed;
			s_keyboardButton = button;
			s_keyboardDone = true;
			s_keyboardOpen = false;
		}

		// The keyboard (app/keyboard_menu.h): what the game asks for, its length and its buttons, the
		// last of which confirms, as Options does
		void DrawKeyboard(float scale)
		{
			ps5menu::KeyboardAsk ask;
			int maxLength;
			{
				std::lock_guard lock(s_mutex);
				ask.hint = s_keyboard.hint;
				ask.error = s_keyboardError;
				ask.buttons = s_keyboard.buttons;
				maxLength = s_keyboard.maxLength;
				if (s_keyboardFresh)
				{
					s_sheet.Start({});
					s_keyboardFresh = false;
				}
			}
			ask.maxLength = maxLength > 0 ? maxLength : -1;
			ask.counted = maxLength > 0;
			const int button =
				s_sheet.Draw("Playtendoh##Keyboard3ds", scale, {g.head, g.body, g.row, g.small, g.chip}, Colours(), ask, s_pressed, s_buttons);
			if (button >= 0)
				FinishKeyboard(button);
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
				Colours().panel, 10 * scale);
			draw->AddText(g.small, 20 * scale, at, Colours().title, text.c_str());
		}
	}

	void Start(const std::string& name, uint64_t titleId, const Settings& settings, Console console, const std::string& system, uint32_t colour)
	{
		std::lock_guard lock(s_mutex);
		s_name = name;
		s_titleId = titleId;
		s_console = console;
		s_cheatsShown = console != Console::Retro; // a libretro core's says so (SetCheatsShown)
		s_system = system;
		// its colour, and the dark of it for what is written on it (as the sides' palettes have)
		const uint32_t ink = (colour >> 16 & 0xff) * 12 / 100 << 16 | (colour >> 8 & 0xff) * 12 / 100 << 8 | (colour & 0xff) * 12 / 100;
		s_systemColours = ps5menu::Side(colour, ink);
		s_settings = settings;
		s_changed = false;
		s_controllers = {}; // a libretro game's, from its loop (SetControllers)
		s_levelRequested = false;
	}

	void SetGame(const std::string& details, const std::string& coverPath)
	{
		std::lock_guard lock(s_mutex);
		s_details = details;
		s_coverPath = coverPath;
	}

	void SetLoading(LoadingStep step, size_t done, size_t of)
	{
		{
			std::lock_guard lock(s_mutex);
			s_loadingStep = step;
			s_loadingDone = done;
			s_loadingOf = of;
		}
		if (!s_loading.exchange(true))
			s_loadingFrom = sceKernelGetProcessTime();
	}

	void EndLoading()
	{
		if (!s_loading.exchange(false))
			return;
		g.fadeFrom = 0;
		s_loadingFades = true;
	}

	void ToggleMenu()
	{
		if (s_open)
		{
			CloseMenu();
			return;
		}
		s_openedAt = sceKernelGetProcessTime();
		s_menuFresh = true;
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

	// The border's picture, from the app's assets/borders (tools/render-borders.py: PNG), for the menu's
	// renderer to draw round the screens
	void LoadBorder(int theme)
	{
		theme = std::clamp(theme, 0, kBorderCount - 1);
		std::vector<uint8_t> rgba;
		int width = 0, height = 0;
		if (theme > 0)
		{
			std::string name = kBorderNames[theme];
			std::transform(name.begin(), name.end(), name.begin(), ::tolower);
			const std::string path = ps5paths::Assets() + "/borders/" + name + ".png";
			if (!ui::DecodeFile(path, rgba, width, height))
			{
				rgba.clear();
				width = height = 0;
				ps5log::Line("[ingame3ds] border {}: {} could not be read", kBorderNames[theme], path);
			}
		}
		SetBorder(theme, std::move(rgba), width, height);
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

	void SetCheatsShown(bool shown)
	{
		std::lock_guard lock(s_mutex);
		s_cheatsShown = shown;
	}

	void SetAchievements(bool shown, const std::string& summary, const std::vector<Achievement>& achievements)
	{
		std::lock_guard lock(s_mutex);
		s_cheevosShown = shown;
		s_cheevosSummary = summary;
		s_cheevos = achievements;
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

	void SetUndoAvailable(bool available)
	{
		std::lock_guard lock(s_mutex);
		s_undoAvailable = available;
	}

	void SetCoreOptions(const std::vector<CoreOption>& options)
	{
		std::lock_guard lock(s_mutex);
		s_coreOptions = options;
	}

	bool TakeCoreOption(std::string& key, int& index)
	{
		std::lock_guard lock(s_mutex);
		if (s_coreChanged.empty())
			return false;
		key = s_coreChanged.front().first;
		index = s_coreChanged.front().second;
		s_coreChanged.erase(s_coreChanged.begin());
		return true;
	}

	bool TakeResetRequest() { return s_resetRequested.exchange(false); }

	void SetControllers(const Controllers& controllers)
	{
		std::lock_guard lock(s_mutex);
		s_controllers = controllers;
	}

	bool TakeLevelRequest() { return s_levelRequested.exchange(false); }

	bool TakeUndoRequest()
	{
		std::lock_guard lock(s_mutex);
		return std::exchange(s_undoRequest, false);
	}

	bool AskState(bool load, std::string& why)
	{
		std::lock_guard lock(s_mutex);
		if (load && (s_stateSlot > (int)s_stateTimes.size() || s_stateTimes[s_stateSlot - 1].empty()))
		{
			why = TrF("Slot {0} is empty", s_stateSlot);
			return false;
		}
		s_stateRequest = load ? -s_stateSlot : s_stateSlot;
		s_stateAction = load ? "load" : "save";
		s_stateMessage = load ? Tr("Loading…") : Tr("Saving…");
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
		// Azahar's renderer made again (a loaded save state): its device is new even when it has the
		// old one's handle, as it often does (the menu then drew with the old device's objects, and
		// the driver crashed)
		if (g.ready && (target.generation != g.generation || target.device != g.device))
		{
			if (target.insideRenderPass)
				return; // the pass before it starts over
			if (!Reattach(target))
			{
				g.failed = true;
				return;
			}
		}
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
			// the loading screen, then the game's first frames with it fading over them
			const bool loading = s_loading;
			float fade = 0;
			if (!loading && s_loadingFades)
			{
				const uint64_t now = sceKernelGetProcessTime();
				if (!g.fadeFrom)
					g.fadeFrom = now;
				fade = ps5menu::LoadingFadeAlpha((now - g.fadeFrom) / 1e6f);
				if (fade <= 0)
					s_loadingFades = false;
			}
			if (!open && !performance && !keyboard && !border && !loading && fade <= 0)
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
			// a new border picture: uploaded here, outside the render pass, as the font is (the
			// picture is kept, for a new device)
			if (borderFresh)
			{
				std::vector<uint8_t> pixels;
				int width, height;
				{
					std::lock_guard lock(s_mutex);
					pixels = s_borderPixels;
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
			// the game's box art, the first time the menu opens or the loading screen shows
			if ((open || loading) && !g.coverTried)
			{
				g.coverTried = true;
				std::string path;
				{
					std::lock_guard lock(s_mutex);
					path = s_coverPath;
				}
				std::vector<uint8_t> rgba;
				int width = 0, height = 0;
				if (!path.empty() && ps5tga::Load(path, ps5menu::kLoadingCoverWidth, ps5menu::kLoadingCoverHeight, rgba, width, height, ui::DecodeFile))
				{
					g.cover = ImGui_ImplVulkan_GenerateTexture(target.commandBuffer, rgba, {width, height});
					g.coverWidth = width;
					g.coverHeight = height;
					// its small copy: the loading screen's ground, its colours blurred
					if (ps5tga::Load(path, ps5menu::kLoadingBlurSize, ps5menu::kLoadingBlurSize, rgba, width, height))
						g.blur = {ImGui_ImplVulkan_GenerateTexture(target.commandBuffer, rgba, {width, height}), width, height};
				}
				else
					ps5log::Line("[ingame3ds] no box art for the menu ({})", path.empty() ? "none fetched" : path);
				if (!g.pad && ps5tga::Load(ps5paths::Assets() + "/ui/dualsense.tga", 1000, 700, rgba, width, height))
					g.pad = ImGui_ImplVulkan_GenerateTexture(target.commandBuffer, rgba, {width, height});
			}
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = {(float)target.width, (float)target.height};
			const uint64_t now = sceKernelGetProcessTime();
			io.DeltaTime = g.lastFrame && now > g.lastFrame ? std::min((now - g.lastFrame) / 1e6f, 0.25f) : 1.0f / 60.0f;
			g.lastFrame = now;
			Input();
			ImGui::NewFrame();
			if (loading)
				DrawLoading(scale, 1.0f);
			else
			{
				if (borderTheme > 0 && g.border)
					DrawBorder(borderTheme);
				if (open)
					DrawMenu(scale);
				else if (keyboard)
					DrawKeyboard(scale);
				if (performance)
					DrawPerformance(scale);
				if (fade > 0)
					DrawLoading(scale, fade);
			}
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
