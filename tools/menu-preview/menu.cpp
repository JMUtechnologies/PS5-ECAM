// SPDX-License-Identifier: GPL-3.0-or-later
// The in-game menus' sheet (port/app/side_menu.h) on the PC, as PNGs, to work on its look without a
// console: the same drawing code, given ImGui's own atlas and a small rasteriser for its triangles.
// tools/preview-menu.sh builds and runs it; the shots go to build/menu-preview.
//
//   menu-preview OUT_DIR FONTS_DIR [BACKDROP.png]
//
// A 3DS game's menu, as ingame3ds.cpp fills it: on its quick actions, on a row, with a category open,
// and a row held. The backdrop stands in for the game behind the menu. Then the loading screen
// (port/app/loading_screen.h) as each side fills it, with a drawn stand-in for the game's cover: the
// Switch's, the Wii U's on its shaders and its pipelines, the 3DS's while how far is not known, and
// halfway through its fade into the game.

#include "../../port/app/buttons_menu.h"
#include "../../port/app/loading_screen.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// what the menu's code reaches on the console, as the launcher preview (console.cpp) stands in for it
extern "C" int sceSystemServiceParamGetInt(int, int* value)
{
	*value = 1; // English (US)
	return 0;
}
namespace ps5log
{
	void Write(std::string_view) {}
}
namespace ui
{
	void Log(const std::string&) {}
}

namespace
{
	constexpr int kW = 1920, kH = 1080;

	struct Image
	{
		std::vector<float> rgb = std::vector<float>(kW * kH * 3);
	};

	struct Texture
	{
		const unsigned char* rgba = nullptr;
		int w = 0, h = 0;
	};

	void Backdrop(Image& image, const std::string& path)
	{
		int w = 0, h = 0, n = 0;
		unsigned char* data = path.empty() ? nullptr : stbi_load(path.c_str(), &w, &h, &n, 3);
		for (int y = 0; y < kH; y++)
			for (int x = 0; x < kW; x++)
			{
				float* p = &image.rgb[(y * kW + x) * 3];
				if (data)
				{
					const unsigned char* q = &data[((y * h / kH) * w + x * w / kW) * 3];
					p[0] = q[0] / 255.0f, p[1] = q[1] / 255.0f, p[2] = q[2] / 255.0f;
				}
				else
				{
					// a bright, busy stand-in: sky over grass
					const float t = (float)y / kH;
					p[0] = 0.35f + 0.3f * t, p[1] = 0.6f + 0.2f * std::sin(x * 0.01f), p[2] = 0.95f - 0.6f * t;
				}
			}
		if (data)
			stbi_image_free(data);
	}

	// ImGui's triangles, each pixel's centre tested against them: colours and the atlas's alpha
	// interpolated, blended over what is there
	void Raster(Image& image, const ImDrawData* data, const Texture& atlas, const std::vector<Texture>& pictures = {})
	{
		for (int l = 0; l < data->CmdListsCount; l++)
		{
			const ImDrawList* list = data->CmdLists[l];
			for (const ImDrawCmd& cmd : list->CmdBuffer)
			{
				const int x0c = std::max(0, (int)cmd.ClipRect.x), y0c = std::max(0, (int)cmd.ClipRect.y);
				const int x1c = std::min(kW, (int)std::ceil(cmd.ClipRect.z)), y1c = std::min(kH, (int)std::ceil(cmd.ClipRect.w));
				const Texture* found = &atlas;
				for (const Texture& picture : pictures)
					if (picture.rgba && cmd.TextureId == (ImTextureID)picture.rgba)
						found = &picture;
				const Texture& texture = *found;
				const bool textured = cmd.TextureId == texture.rgba;
				for (unsigned e = 0; e + 2 < cmd.ElemCount; e += 3)
				{
					const ImDrawVert* v[3];
					for (int k = 0; k < 3; k++)
						v[k] = &list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + e + k]];
					const float ax = v[0]->pos.x, ay = v[0]->pos.y, bx = v[1]->pos.x, by = v[1]->pos.y, cx = v[2]->pos.x, cy = v[2]->pos.y;
					const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
					if (std::fabs(area) < 1e-6f)
						continue;
					const int minX = std::max(x0c, (int)std::floor(std::min({ax, bx, cx})));
					const int maxX = std::min(x1c - 1, (int)std::ceil(std::max({ax, bx, cx})));
					const int minY = std::max(y0c, (int)std::floor(std::min({ay, by, cy})));
					const int maxY = std::min(y1c - 1, (int)std::ceil(std::max({ay, by, cy})));
					for (int y = minY; y <= maxY; y++)
						for (int x = minX; x <= maxX; x++)
						{
							const float px = x + 0.5f, py = y + 0.5f;
							float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
							float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area;
							float w2 = 1 - w0 - w1;
							if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f)
								continue;
							float c[4] = {};
							for (int k = 0; k < 4; k++)
							{
								const float ck[3] = {(float)((v[0]->col >> (8 * k)) & 255), (float)((v[1]->col >> (8 * k)) & 255),
									(float)((v[2]->col >> (8 * k)) & 255)};
								c[k] = (w0 * ck[0] + w1 * ck[1] + w2 * ck[2]) / 255.0f;
							}
							if (textured)
							{
								const float u = w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x;
								const float t = w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y;
								// bilinear, as the console's samplers are (a small picture drawn large is its colours blurred)
								const float fx = std::clamp(u * texture.w - 0.5f, 0.0f, texture.w - 1.0f);
								const float fy = std::clamp(t * texture.h - 0.5f, 0.0f, texture.h - 1.0f);
								const int tx = (int)fx, ty = (int)fy, tx1 = std::min(tx + 1, texture.w - 1), ty1 = std::min(ty + 1, texture.h - 1);
								const float ax_ = fx - tx, ay_ = fy - ty;
								auto texel = [&](int x, int y, int k) { return texture.rgba[(y * texture.w + x) * 4 + k] / 255.0f; };
								for (int k = 0; k < 4; k++)
									c[k] *= (texel(tx, ty, k) * (1 - ax_) + texel(tx1, ty, k) * ax_) * (1 - ay_) +
										(texel(tx, ty1, k) * (1 - ax_) + texel(tx1, ty1, k) * ax_) * ay_;
							}
							float* p = &image.rgb[(y * kW + x) * 3];
							for (int k = 0; k < 3; k++)
								p[k] = p[k] * (1 - c[3]) + c[k] * c[3];
						}
				}
			}
		}
	}

	void Save(const Image& image, const std::string& path)
	{
		std::vector<unsigned char> out(kW * kH * 3);
		for (size_t i = 0; i < out.size(); i++)
			out[i] = (unsigned char)std::clamp(image.rgb[i] * 255.0f + 0.5f, 0.0f, 255.0f);
		stbi_write_png(path.c_str(), kW, kH, 3, out.data(), kW * 3);
		std::printf("%s\n", path.c_str());
	}

	std::vector<unsigned char> Read(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}

	// A stand-in for a game's cover (no real box art in the repository): a sky, hills and a sun, and its
	// small copy, each pixel the average of those it covers, as ps5tga::Load makes the blur's
	std::vector<unsigned char> Cover(int w, int h)
	{
		std::vector<unsigned char> rgba((size_t)w * h * 4);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				const float u = (float)x / w, v = (float)y / h;
				float r = 0.95f - 0.5f * v, g = 0.45f + 0.25f * v, b = 0.35f + 0.5f * v;
				if (std::hypot(u - 0.68f, v - 0.3f) < 0.13f)
					r = 1.0f, g = 0.86f, b = 0.4f;
				if (v > 0.62f + 0.06f * std::sin(u * 9.0f))
					r = 0.16f, g = 0.42f + 0.2f * (1 - v), b = 0.3f;
				if (v < 0.16f)
					r = 0.08f, g = 0.1f, b = 0.2f; // the title band
				unsigned char* p = &rgba[((size_t)y * w + x) * 4];
				p[0] = (unsigned char)(r * 255), p[1] = (unsigned char)(g * 255), p[2] = (unsigned char)(b * 255), p[3] = 255;
			}
		return rgba;
	}

	std::vector<unsigned char> Shrink(const std::vector<unsigned char>& rgba, int w, int h, int size)
	{
		std::vector<unsigned char> out((size_t)size * size * 4);
		for (int y = 0; y < size; y++)
			for (int x = 0; x < size; x++)
				for (int k = 0; k < 4; k++)
				{
					float sum = 0;
					int n = 0;
					for (int sy = y * h / size; sy < (y + 1) * h / size; sy++)
						for (int sx = x * w / size; sx < (x + 1) * w / size; sx++, n++)
							sum += rgba[((size_t)sy * w + sx) * 4 + k];
					out[((size_t)y * size + x) * 4 + k] = (unsigned char)(sum / std::max(n, 1));
				}
		return out;
	}

	std::vector<ps5menu::Row> Rows()
	{
		using ps5menu::Row;
		auto setting = [](const char* id, const char* label, const char* value, const char* help, float slider = -1) {
			Row row{id, label, value, true, help};
			row.slider = slider;
			return row;
		};
		Row graphics{"graphics", "Graphics", "1080p", false, "Resolution, filtering and the screens' shader."};
		graphics.rows = {setting("res", "Resolution", "1080p", "The 3D screen drawn at up to 6 times its own size."),
			setting("filter", "Texture filtering", "Linear", "Smooth or sharp pixels."),
			setting("bright", "Brightness", "60%", "The screens' brightness.", 0.6f)};
		Row controls{"buttons", "Buttons", "12 buttons set", false, "What each DualSense button does in this game."};
		controls.rows = {Row{"buttons-test", "Controller test", "-", false, "This player's DualSense input, live."}};
		const std::pair<const char*, int> kSet[] = {{"A", 2}, {"B", 1}, {"X", 4}, {"Y", 3}, {"L", 5}, {"R", 6}, {"ZL", 7}, {"ZR", 8},
			{"Start", 12}, {"Select", 11}, {"Home", 25}};
		for (size_t i = 0; i < std::size(kSet); i++)
		{
			Row row{"button:" + std::to_string(i), kSet[i].first, ps5menu::kInputNames[kSet[i].second], false, "Press Cross, then the DualSense button to use."};
			row.pad = kSet[i].second;
			controls.rows.push_back(row);
		}
		std::vector<Row> rows{graphics, controls,
			setting("speed", "Speed", "100%", "Faster or slower than the console.", 0.5f),
			Row{"cheats", "Cheats", "2 on", false, "This game's cheats."},
			Row{"saves", "Saves", "", false, "Back up or bring back this game's save."},
			setting("volume", "Volume", "80%", "The game's volume.", 0.8f),
			Row{"screenshot", "Screenshot", "", false, "Takes a picture of the screens."},
			Row{"gameonly", "Settings for this game only", "On", true,
				"On: this game keeps its own settings, and what you change here stays with it. Off: it uses the system's settings from its next start."}};
		Row quit{"quit", "Quit the game", "", false, "Back to the Library. What is not saved is lost."};
		quit.apart = true;
		quit.hold = true;
		rows.push_back(quit);
		return rows;
	}

	// a libretro game's (an NES game with the Arkanoid paddle): its Buttons page with the Controller row and what
	// the DualSense does for the paddle, as ingame3ds.cpp makes them
	std::vector<ps5menu::Row> RetroRows()
	{
		using ps5menu::Row;
		Row controls{"buttons", "Buttons", "8 buttons set", false, "Choose the DualSense button for each button."};
		controls.rows = {Row{"buttons-test", "Controller test", "-", false, "This player's DualSense input, live."},
			Row{"controller", "Controller", "Auto: Arkanoid paddle", true,
				"The controller plugged into the console for this game. Auto picks the one the game was made for, such as the Zapper for Duck Hunt."},
			Row{"special:0", "Turn the knob", "Touchpad, left stick or D-pad", false, "What the DualSense does for the controller plugged in."},
			Row{"special:1", "Fire", "Cross or R2", false, "What the DualSense does for the controller plugged in."}};
		const std::pair<const char*, int> kSet[] = {{"A", 2}, {"B", 1}, {"Start", 12}, {"Select", 11}};
		for (size_t i = 0; i < std::size(kSet); i++)
		{
			Row row{"button:" + std::to_string(i), kSet[i].first, ps5menu::kInputNames[kSet[i].second], false, "Press Cross, then the DualSense button to use."};
			row.pad = kSet[i].second;
			controls.rows.push_back(row);
		}
		Row quit{"library", "Quit to the library", "", false, "Closes the game and goes to the library. Unsaved progress is lost. Hold Cross."};
		quit.apart = true;
		quit.hold = true;
		return {Row{"screens", "Screen and border", "None", false, "The border around the screen."}, controls,
			Row{"volume", "Volume", "100%", true, "The game's volume."}, quit};
	}

	std::vector<ps5menu::Tile> Tiles()
	{
		return {{"resume", "Resume", "resume"}, {"save", "Save state", "save", "Slot 1"}, {"load", "Load state", "load", "Slot 1", true},
			{"layout", "Screens", "screens", "Side by side"}, {"gap", "Screen gap", "gap", "Off"}};
	}
}

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		std::fprintf(stderr, "menu-preview OUT_DIR FONTS_DIR [BACKDROP.png]\n");
		return 2;
	}
	const std::string out = argv[1], fonts = argv[2], backdrop = argc > 3 ? argv[3] : "";
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = {(float)kW, (float)kH};
	io.DeltaTime = 1.0f / 60;
	static std::vector<unsigned char> regular = Read(fonts + "/Lexend-Regular.ttf"), medium = Read(fonts + "/Lexend-Medium.ttf"),
									  semi = Read(fonts + "/Lexend-SemiBold.ttf");
	ImFontConfig config;
	config.FontDataOwnedByAtlas = false;
	auto font = [&](std::vector<unsigned char>& file, float size) {
		return io.Fonts->AddFontFromMemoryTTF(file.data(), (int)file.size(), size, &config);
	};
	const ps5menu::Fonts set{font(semi, ps5menu::kHeading), font(medium, ps5menu::kBody), font(regular, ps5menu::kLabel),
		font(regular, ps5menu::kCaption), font(semi, ps5menu::kChip)};
	const ps5menu::LoadingFonts loadingFonts{font(semi, ps5menu::kLoadingTitle), set.chip, set.body, set.label, set.caption};
	unsigned char* pixels;
	int w, h;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
	io.Fonts->SetTexID((ImTextureID)pixels);
	const Texture atlas{pixels, w, h};

	// the Buttons page's DualSense, as the package makes it from port/ui/dualsense.png
	int padW = 0, padH = 0, padN = 0;
	unsigned char* padPixels = stbi_load((fonts + "/../dualsense.png").c_str(), &padW, &padH, &padN, 4);
	const Texture pad{padPixels, padW, padH};
	ps5menu::Header header;
	header.pad = (ImTextureID)padPixels;
	header.system = "3DS";
	header.status = "Paused";
	header.title = "Pokémon Ultra Sun";
	header.details = "2 h 40 min played · Slot 1 saved today";
	const std::vector<ps5menu::Row> rows = Rows();
	const std::vector<ps5menu::Tile> tiles = Tiles();
	const std::vector<std::pair<const char*, std::string>> hints{{"cross", "Choose"}, {"leftright", "Change"}, {"circle", "Back to the game"}};

	struct Shot
	{
		const char* name;
		std::vector<uint32_t> presses; // one a frame
		uint32_t held = 0;
		bool retro = false; // the libretro game's rows (RetroRows)
	};
	const std::vector<Shot> shots{
		{"menu-tiles", {}},
		{"menu-row", {ps5pad::kDown, 0, ps5pad::kDown, 0, ps5pad::kDown}},
		{"menu-open", {ps5pad::kDown, 0, ps5pad::kCross, 0, ps5pad::kDown, 0, ps5pad::kDown, 0, ps5pad::kDown}},
		{"menu-hold", {ps5pad::kUp, 0, ps5pad::kCross}, ps5pad::kCross},
		{"menu-buttons", {ps5pad::kDown, 0, ps5pad::kDown, 0, ps5pad::kCross, 0, ps5pad::kDown, 0, ps5pad::kDown}},
		{"menu-controller", {ps5pad::kDown, 0, ps5pad::kDown, 0, ps5pad::kCross, 0, ps5pad::kDown, 0, ps5pad::kDown}, 0, true},
	};
	const std::vector<ps5menu::Row> retroRows = RetroRows();
	for (const Shot& shot : shots)
	{
		ps5menu::SideMenu menu;
		uint64_t now = 1000000;
		// the presses, a frame each, then frames for the springs to settle (and a hold to fill)
		std::vector<uint32_t> frames = shot.presses;
		for (int i = 0; i < 40; i++)
			frames.push_back(shot.held);
		for (size_t f = 0; f < frames.size(); f++)
		{
			bool close = false;
			now += f + 1 == frames.size() ? 0 : 1000000 / 60 * (shot.held && f > shot.presses.size() + 20 ? 0 : 1);
			const std::vector<ps5menu::Row>& shown = shot.retro ? retroRows : rows;
			menu.Update(shown, tiles, frames[f], now, close);
			ImGui::NewFrame();
			const ps5menu::Canvas canvas{ImGui::GetForegroundDrawList(), 1.0f, {0, 0}, ps5menu::kGold};
			ps5menu::Header top = header;
			if (shot.retro)
			{
				top.system = "NES";
				top.title = "Arkanoid";
				top.details = "Mesen";
			}
			menu.Draw(canvas, set, top, shown, tiles, hints);
			ImGui::Render();
			if (f + 1 == frames.size())
			{
				Image image;
				Backdrop(image, backdrop);
				Raster(image, ImGui::GetDrawData(), atlas, {pad});
				Save(image, out + "/" + shot.name + ".png");
			}
		}
	}

	// the loading screen, as each side fills it
	const int coverW = 360, coverH = 495; // a 3DS or Switch case's shape, ps5tga::Load's fit in 512x704
	static const std::vector<unsigned char> cover = Cover(coverW, coverH);
	static const std::vector<unsigned char> blur = Shrink(cover, coverW, coverH, ps5menu::kLoadingBlurSize);
	const std::vector<Texture> pictures{{cover.data(), coverW, coverH}, {blur.data(), ps5menu::kLoadingBlurSize, ps5menu::kLoadingBlurSize}};
	struct LoadingShot
	{
		const char* name;
		const char* system;
		const ps5menu::Palette& colours;
		std::string step;
		float progress, time, alpha;
	};
	const std::vector<LoadingShot> loadingShots{
		{"loading-switch", "Nintendo Switch", ps5menu::kRed, ps5menu::ShaderStep(1834, 4120), 1834.0f / 4120, 0, 1},
		{"loading-wiiu-shaders", "Wii U", ps5menu::kBlue, ps5menu::ShaderStep(2210, 3400), 2210.0f / 3400, 0, 1},
		{"loading-wiiu-pipelines", "Wii U", ps5menu::kBlue, ps5menu::PipelineStep(910, 5120), 910.0f / 5120, 0, 1},
		{"loading-3ds", "Nintendo 3DS", ps5menu::kGold, ps5menu::LoadingStep(), -1, 1.1f, 1},
		{"loading-3ds-fade", "Nintendo 3DS", ps5menu::kGold, ps5menu::StartingStep(), -1, 2.0f, ps5menu::LoadingFadeAlpha(ps5menu::kLoadingFade / 2)},
	};
	for (const LoadingShot& shot : loadingShots)
	{
		ps5menu::Loading loading;
		loading.system = shot.system;
		loading.title = "The Legend of the Stand-in Cover: A Long Name Over Two Lines";
		loading.details = "Playtendoh  /  2026";
		loading.step = shot.step;
		loading.progress = shot.progress;
		loading.cover = {(ImTextureID)cover.data(), coverW, coverH};
		loading.blur = {(ImTextureID)blur.data(), ps5menu::kLoadingBlurSize, ps5menu::kLoadingBlurSize};
		ImGui::NewFrame();
		ps5menu::DrawLoading(ImGui::GetBackgroundDrawList(), io.DisplaySize, 1.0f, shot.colours, loadingFonts, loading, shot.time, shot.alpha);
		ImGui::Render();
		Image image;
		Backdrop(image, backdrop);
		Raster(image, ImGui::GetDrawData(), atlas, pictures);
		Save(image, out + "/" + shot.name + ".png");
	}
	ImGui::DestroyContext();
	return 0;
}
