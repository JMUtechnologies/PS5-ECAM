// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the launcher on a PC, to see its layout without a console (tools/preview-launcher.sh).
//
// The launcher's own code (port/frontend: launcher.cpp, settings.cpp, bubbles.cpp, wave.cpp,
// ProsperoEden's bitmap fonts; port/azahar's library and controls) runs on RmlUi built for the PC,
// and draws in software here as SDL draws it on the console: textures modulated by RmlUi's vertex
// colours and blended without premultiplying, on whole pixels. In place of Cemu and the console it
// has sample Wii U games, graphic packs and controllers (console.cpp, which the new launcher's preview
// shares); the 3DS games are files the script makes (tools/launcher-preview/make-3ds-samples.py),
// read by Azahar's side's own library. A script presses the DualSense's buttons and saves frames as
// PNGs.
//
//   launcher-preview UI_FOLDER OUTPUT_FOLDER SCRIPT GAMES_FOLDER 3DS_GAMES_FOLDER
//
// The script has one command a line ('#' starts a comment):
//   press BUTTON...  each button down for 3 frames, then up for 3, one after another
//                    (touchpad+options: the two together)
//   hold BUTTON N    down for N frames, then up for 3
//   wait N           N frames
//   shot NAME        the frame on screen as OUTPUT_FOLDER/NAME.png
//   record NAME      from here, every other frame (30 a second) as OUTPUT_FOLDER/NAME/00000.png on,
//                    for a video of the launcher (ffmpeg -framerate 30 -i NAME/%05d.png makes one)
//   stop             no more recording
// Buttons: up down left right cross circle square triangle l1 r1 l2 r2 l3 r3 options create
// touchpad, and the sticks: ls-up ls-down ls-left ls-right rs-up rs-down rs-left rs-right.

#include "app/boxart.h"
#include "app/compatibility.h"
#include "app/emulator.h"
#include "app/gameinfo.h"
#include "app/pack_updates.h"
#include "app/updates.h"
#include "app/usb_devices.h"
#include "azahar/library.h"
#include "frontend/bubbles.h"
#include "frontend/launcher.h"
#include "frontend/wave.h"
#include "frontend/settings.h"
#include "frontend/sound.h"
#include "frontend/ui_host.h"
#include "ps5/kernel.h"
#include "ps5/log.h"
#include "ps5/notify.h"
#include "ps5/pad.h"
#include "ps5/privilege.h"

#include "bitmap_font_engine.h"
#include "console.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>

namespace
{
	constexpr const char* kFonts[] = {"20", "24", "28", "32", "36", "40", "48"};

	std::string s_ui;

	// -- drawing ---------------------------------------------------------------------------------

	struct Texture
	{
		int width = 0, height = 0;
		std::vector<uint8_t> bgra;
	};

	void Blend(uint8_t* pixel, float b, float g, float r, float a)
	{
		// as SDL_BLENDMODE_BLEND: the colour weighted by its alpha, without premultiplying
		pixel[0] = (uint8_t)std::lround(b * a + pixel[0] * (1.0f - a));
		pixel[1] = (uint8_t)std::lround(g * a + pixel[1] * (1.0f - a));
		pixel[2] = (uint8_t)std::lround(r * a + pixel[2] * (1.0f - a));
	}

	class Renderer final : public Rml::RenderInterfaceCompatibility
	{
	public:
		void RenderGeometry(Rml::Vertex* vertices, int, int* indices, int indexCount, Rml::TextureHandle handle,
			const Rml::Vector2f& translation) override
		{
			const Texture* texture = reinterpret_cast<const Texture*>(handle);
			for (int i = 0; i + 2 < indexCount; i += 3)
				Triangle(vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]], texture, translation);
		}

		bool LoadTexture(Rml::TextureHandle& handle, Rml::Vector2i& dimensions, const Rml::String& source) override
		{
			Rml::FileInterface* files = Rml::GetFileInterface();
			const Rml::FileHandle file = files->Open(source);
			if (!file)
				return false;
			files->Seek(file, 0, SEEK_END);
			std::vector<uint8_t> data(files->Tell(file));
			files->Seek(file, 0, SEEK_SET);
			files->Read(data.data(), data.size(), file);
			files->Close(file);
			if (data.size() < 18 || data[2] != 2 || data[16] != 32 || !(data[17] & 0x20))
			{
				std::fprintf(stderr, "%s is not a 32-bit top-down TGA\n", source.c_str());
				return false;
			}
			auto* texture = new Texture{data[12] | data[13] << 8, data[14] | data[15] << 8};
			const size_t bytes = (size_t)texture->width * texture->height * 4;
			texture->bgra.assign(data.begin() + 18, data.begin() + 18 + std::min(bytes, data.size() - 18));
			texture->bgra.resize(bytes);
			handle = reinterpret_cast<Rml::TextureHandle>(texture);
			dimensions = {texture->width, texture->height};
			return true;
		}

		bool GenerateTexture(Rml::TextureHandle& handle, const Rml::byte* rgba, const Rml::Vector2i& dimensions) override
		{
			auto* texture = new Texture{dimensions.x, dimensions.y};
			texture->bgra.resize((size_t)dimensions.x * dimensions.y * 4);
			for (size_t i = 0; i < texture->bgra.size(); i += 4)
			{
				texture->bgra[i + 0] = rgba[i + 2];
				texture->bgra[i + 1] = rgba[i + 1];
				texture->bgra[i + 2] = rgba[i + 0];
				texture->bgra[i + 3] = rgba[i + 3];
			}
			handle = reinterpret_cast<Rml::TextureHandle>(texture);
			return true;
		}

		void ReleaseTexture(Rml::TextureHandle handle) override { delete reinterpret_cast<Texture*>(handle); }
		void EnableScissorRegion(bool enable) override { m_scissor = enable; }
		void SetScissorRegion(int x, int y, int width, int height) override { m_clip = {x, y, x + width, y + height}; }

	private:
		// Pixel centres inside the triangle, sampled a hair off the centre so that a pixel on the
		// edge two triangles share is drawn by exactly one of them.
		void Triangle(const Rml::Vertex& a, const Rml::Vertex& b, const Rml::Vertex& c, const Texture* texture, Rml::Vector2f t)
		{
			const double ax = a.position.x + t.x, ay = a.position.y + t.y, bx = b.position.x + t.x, by = b.position.y + t.y;
			const double cx = c.position.x + t.x, cy = c.position.y + t.y;
			const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
			if (area == 0.0)
				return;
			int left = (int)std::floor(std::min({ax, bx, cx})), right = (int)std::ceil(std::max({ax, bx, cx}));
			int top = (int)std::floor(std::min({ay, by, cy})), bottom = (int)std::ceil(std::max({ay, by, cy}));
			left = std::max(left, m_scissor ? m_clip[0] : 0);
			top = std::max(top, m_scissor ? m_clip[1] : 0);
			right = std::min(right, m_scissor ? m_clip[2] : preview::kWidth);
			bottom = std::min(bottom, m_scissor ? m_clip[3] : preview::kHeight);
			left = std::max(left, 0), top = std::max(top, 0), right = std::min(right, preview::kWidth), bottom = std::min(bottom, preview::kHeight);
			for (int y = top; y < bottom; y++)
				for (int x = left; x < right; x++)
				{
					const double px = x + 0.5 + 1e-5, py = y + 0.5 + 2e-5;
					const double wa = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
					const double wb = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area;
					const double wc = 1.0 - wa - wb;
					if (wa < 0.0 || wb < 0.0 || wc < 0.0)
						continue;
					auto mix = [&](float va, float vb, float vc) { return (float)(va * wa + vb * wb + vc * wc); };
					float r = mix(a.colour.red, b.colour.red, c.colour.red);
					float g = mix(a.colour.green, b.colour.green, c.colour.green);
					float bl = mix(a.colour.blue, b.colour.blue, c.colour.blue);
					float alpha = mix(a.colour.alpha, b.colour.alpha, c.colour.alpha) / 255.0f;
					if (texture)
					{
						const float u = mix(a.tex_coord.x, b.tex_coord.x, c.tex_coord.x), v = mix(a.tex_coord.y, b.tex_coord.y, c.tex_coord.y);
						const int tx = std::clamp((int)std::floor(u * texture->width), 0, texture->width - 1);
						const int ty = std::clamp((int)std::floor(v * texture->height), 0, texture->height - 1);
						const uint8_t* texel = &texture->bgra[((size_t)ty * texture->width + tx) * 4];
						bl *= texel[0] / 255.0f;
						g *= texel[1] / 255.0f;
						r *= texel[2] / 255.0f;
						alpha *= texel[3] / 255.0f;
					}
					if (alpha > 0.0f)
						Blend(&preview::frame[((size_t)y * preview::kWidth + x) * 4], bl, g, r, alpha);
				}
		}

		bool m_scissor = false;
		std::array<int, 4> m_clip{0, 0, preview::kWidth, preview::kHeight};
	};

	class System final : public Rml::SystemInterface
	{
	public:
		double GetElapsedTime() override { return preview::timeUs / 1000000.0; }
		bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
		{
			if (type <= Rml::Log::LT_WARNING)
				std::fprintf(stderr, "[ui] %s\n", message.c_str());
			return true;
		}
		void JoinPath(Rml::String& translated, const Rml::String& document, const Rml::String& path) override
		{
			if (!path.empty() && path[0] == '/')
				translated = path;
			else
				Rml::SystemInterface::JoinPath(translated, document, path);
		}
	};

	class Files final : public Rml::FileInterface
	{
	public:
		Rml::FileHandle Open(const Rml::String& path) override
		{
			const std::string resolved = !path.empty() && path[0] == '/' ? path : ps5ui::AssetPath(path);
			std::FILE* file = std::fopen(resolved.c_str(), "rb");
			if (!file)
				std::fprintf(stderr, "[ui] cannot open %s\n", resolved.c_str());
			return reinterpret_cast<Rml::FileHandle>(file);
		}
		void Close(Rml::FileHandle file) override { std::fclose(reinterpret_cast<std::FILE*>(file)); }
		size_t Read(void* buffer, size_t size, Rml::FileHandle file) override { return std::fread(buffer, 1, size, reinterpret_cast<std::FILE*>(file)); }
		bool Seek(Rml::FileHandle file, long offset, int origin) override { return std::fseek(reinterpret_cast<std::FILE*>(file), offset, origin) == 0; }
		size_t Tell(Rml::FileHandle file) override { return (size_t)std::ftell(reinterpret_cast<std::FILE*>(file)); }
	};

	// The background in the columns from left to right: as ui_host.cpp draws it.
	void DrawBubbles(ps5ui::Bubbles& bubbles, int left, int right)
	{
		static const std::vector<uint8_t> gradient = ps5ui::Bubbles::Gradient();
		static std::map<int, std::vector<uint8_t>> discs;
		for (int y = 0; y < preview::kHeight; y++)
			std::copy(&gradient[((size_t)y * preview::kWidth + left) * 4], &gradient[((size_t)y * preview::kWidth + right) * 4], &preview::frame[((size_t)y * preview::kWidth + left) * 4]);
		for (const auto& bubble : bubbles.List())
		{
			auto& disc = discs[bubble.radius];
			if (disc.empty())
				disc = ps5ui::Bubbles::Disc(bubble.radius);
			const int size = ps5ui::Bubbles::DiscSize(bubble.radius);
			const int bubbleLeft = (int)std::lround(bubble.x) - size / 2, top = (int)std::lround(bubble.y) - size / 2;
			for (int y = 0; y < size; y++)
				for (int x = 0; x < size; x++)
				{
					const int fx = bubbleLeft + x, fy = top + y;
					if (fx < left || fy < 0 || fx >= right || fy >= preview::kHeight)
						continue;
					const float alpha = bubble.alpha * disc[(size_t)y * size + x] / 255.0f;
					const uint8_t* colour = ps5ui::Bubbles::kColour;
					if (alpha > 0.0f)
						Blend(&preview::frame[((size_t)fy * preview::kWidth + fx) * 4], colour[0], colour[1], colour[2], alpha);
				}
		}
	}

	void DrawWave(const ps5ui::Wave& wave, int left, int right)
	{
		static const std::vector<uint8_t> gradient = ps5ui::Wave::Gradient();
		static std::vector<std::vector<uint8_t>> pictures;
		if (pictures.empty())
			for (int layer = 0; layer < ps5ui::Wave::kLayers; layer++)
				pictures.push_back(ps5ui::Wave::Picture(layer));
		for (int y = 0; y < preview::kHeight; y++)
			std::copy(&gradient[((size_t)y * preview::kWidth + left) * 4], &gradient[((size_t)y * preview::kWidth + right) * 4], &preview::frame[((size_t)y * preview::kWidth + left) * 4]);
		for (int layer = 0; layer < ps5ui::Wave::kLayers; layer++)
		{
			const auto& info = ps5ui::Wave::LayerOf(layer);
			const int width = ps5ui::Wave::PictureWidth(layer), offset = wave.Offset(layer);
			for (int y = 0; y < info.height; y++)
				for (int x = left; x < right; x++)
				{
					const uint8_t* p = &pictures[layer][((size_t)y * width + offset + x) * 4];
					if (p[3])
						Blend(&preview::frame[((size_t)(info.top + y) * preview::kWidth + x) * 4], p[0], p[1], p[2], p[3] / 255.0f);
				}
		}
	}


	struct Host
	{
		System system;
		Files files;
		BitmapFontEngine fonts;
		Renderer render;
		ps5ui::Bubbles bubbles;
		ps5ui::Wave wave;
		ps5ui::Scene scene = ps5ui::Scene::Both;
		Rml::Context* context = nullptr;
		std::map<std::string, Rml::ElementDocument*> documents;
	};
	Host* s_host = nullptr;
}

// -- the launcher's host (frontend/ui_host.h) ----------------------------------------------------

namespace ps5ui
{
	std::string AssetPath(const std::string& relative) { return s_ui + "/" + relative; }

	bool Start(std::string& error)
	{
		s_host = new Host();
		Rml::SetSystemInterface(&s_host->system);
		Rml::SetFileInterface(&s_host->files);
		Rml::SetRenderInterface(s_host->render.GetAdaptedInterface());
		Rml::SetFontEngineInterface(&s_host->fonts);
		Rml::Initialise();
		for (const char* size : {"56", "64", "72"}) // the titles' sizes, bold only
			Rml::LoadFontFace(AssetPath(fmt::format("fonts/Lexend-Bold-{}.fnt", size)));
		for (const char* weight : {"", "-Bold"})
			for (const char* size : kFonts)
				if (!Rml::LoadFontFace(AssetPath(fmt::format("fonts/Lexend{}-{}.fnt", weight, size))))
				{
					error = "missing font";
					return false;
				}
		s_host->context = Rml::CreateContext("preview", {preview::kWidth, preview::kHeight});
		return true;
	}

	Rml::ElementDocument* Show(const std::string& name)
	{
		auto it = s_host->documents.find(name);
		if (it == s_host->documents.end())
		{
			Rml::ElementDocument* document = s_host->context->LoadDocument(AssetPath(name));
			if (!document)
				return nullptr;
			it = s_host->documents.emplace(name, document).first;
		}
		for (auto& [other, document] : s_host->documents)
			if (other != name)
				document->Hide();
		it->second->Show();
		return it->second;
	}

	void SetScene(Scene scene) { s_host->scene = scene; }

	void Frame()
	{
		s_host->context->Update();
		s_host->bubbles.Advance(1.0 / 60.0);
		s_host->wave.Advance(1.0 / 60.0);
		const Scene scene = s_host->scene;
		if (scene != Scene::Wave)
			DrawBubbles(s_host->bubbles, 0, scene == Scene::Both ? preview::kWidth / 2 : preview::kWidth);
		if (scene != Scene::Bubbles)
			DrawWave(s_host->wave, scene == Scene::Both ? preview::kWidth / 2 : 0, preview::kWidth);
		s_host->context->Render();
		preview::timeUs += 16667;
		preview::AdvanceScript();
	}

	void Stop() {}
}

int main(int argc, char* argv[])
{
	if (argc != 6)
	{
		std::fprintf(stderr, "launcher-preview UI_FOLDER OUTPUT_FOLDER SCRIPT GAMES_FOLDER 3DS_GAMES_FOLDER\n");
		return 2;
	}
	s_ui = argv[1];
	preview::output = argv[2];
	ps5gameinfo::SetFolder("port/app/gametdb"); // the repository's: the preview runs from its root
	ps5compat::SetPath("docs/COMPATIBILITY.md");
	if (std::getenv("PREVIEW_UPDATE"))
		ps5update::Start(); // as the app's check finding a newer release
	preview::LoadScript(argv[3]);
	ps5settings::Launcher settings;
	settings.gamesFolder = argv[4];
	settings.lastGame = 0x00050000101C9400;
	settings.recent = {0x00050000101C9400, 0x0005000010143500, 0x000500001010EC00, 0x0005000010101D00};
	settings.n3ds.gamesFolder = argv[5];
	settings.n3ds.lastGame = 0x0004000000053F00;
	settings.n3ds.recent = {0x0004000000053F00, 0x0004000000030600, 0x000400000017C100};
	ps5launcher::Status status;
	status.coreReady = true;
	status.diagnostics = {"PS5CEMU-HAR preview: Cemu at 4e3c824, Azahar at 4aef900", "Jailbroken by the HEN: /data reachable, JIT memory available",
		"Boot log: /data/ps5cemu/logs/boot.log", "Cemu's log: /data/ps5cemu/log.txt"};
	// the start screen's counts, as the last session would have saved them
	settings.gameCount = 4;
	settings.n3ds.gameCount = 3;
	// as main_ps5.cpp: only the chosen side starts (the preview's Cemu is always ready)
	ps5launcher::Run(settings, status, [&](ps5launcher::System system) {
		if (system == ps5launcher::System::N3ds)
			ps5azahar::StartScan(settings.n3ds.gamesFolder);
	});
	// a game was chosen: the loading screen is on
	preview::WritePng(preview::output + "/launch.png");
	return 0;
}
