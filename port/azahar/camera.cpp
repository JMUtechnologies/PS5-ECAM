// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the 3DS's cameras on a PS5, which has none to lend: both see a picture, the newest in
// azahar/camera (a PNG or JPEG, put there by FTP or sent from a phone with the remote touch screen's
// files page), fitted into the frame on white, so a game's QR code reader can read it: Pokemon's QR
// scanner, Mii QR codes, Animal Crossing's designs. With no picture there they see black, as Azahar's
// blank camera does. The folder is read again each time the game starts a camera, so a picture sent
// while a game runs is seen at the next scan.

#include "camera.h"

#include "core/frontend/camera/factory.h"
#include "core/frontend/camera/interface.h"
#include "core/hle/service/cam/cam.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// ui/images.cpp's: a picture file decoded as RGBA rows, with the launcher's stb_image
namespace ui
{
	bool DecodeFile(const std::string& path, std::vector<uint8_t>& rgba, int& width, int& height);
}

namespace ps5azahar
{
	namespace
	{
		namespace fs = std::filesystem;

		// The newest PNG or JPEG in the folder, or empty
		std::string NewestPicture(const std::string& folder)
		{
			std::error_code ec;
			std::string newest;
			fs::file_time_type newestTime{};
			for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
			{
				std::string extension = it->path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				if ((extension != ".png" && extension != ".jpg" && extension != ".jpeg") || it->path().filename().string()[0] == '.' ||
					!it->is_regular_file(ec))
					continue;
				const auto time = it->last_write_time(ec);
				if (newest.empty() || time > newestTime)
				{
					newest = it->path().string();
					newestTime = time;
				}
			}
			return newest;
		}

		class PictureCamera final : public Camera::CameraInterface
		{
		public:
			PictureCamera(std::string folder, Service::CAM::Flip flip)
				: m_folder(std::move(folder)), m_baseMirror(Mirrors(flip)), m_baseInvert(Inverts(flip))
			{
				m_mirror = m_baseMirror;
				m_invert = m_baseInvert;
			}

			void StartCapture() override
			{
				// the newest picture, read again at each start
				m_rgba.clear();
				m_width = m_height = 0;
				const std::string path = NewestPicture(m_folder);
				if (!path.empty() && !ui::DecodeFile(path, m_rgba, m_width, m_height))
					m_rgba.clear();
				m_frame.clear();
			}
			void StopCapture() override {}
			void SetResolution(const Service::CAM::Resolution& resolution) override
			{
				m_outWidth = resolution.width;
				m_outHeight = resolution.height;
				m_frame.clear();
			}
			void SetFlip(Service::CAM::Flip flip) override
			{
				m_mirror = m_baseMirror != Mirrors(flip);
				m_invert = m_baseInvert != Inverts(flip);
				m_frame.clear();
			}
			void SetEffect(Service::CAM::Effect) override {}
			void SetFormat(Service::CAM::OutputFormat format) override
			{
				m_rgb565 = format == Service::CAM::OutputFormat::RGB565;
				m_frame.clear();
			}
			void SetFrameRate(Service::CAM::FrameRate) override {}
			std::vector<u16> ReceiveFrame() override
			{
				if (m_frame.empty())
					Build();
				return m_frame;
			}
			bool IsPreviewAvailable() override { return true; }

		private:
			static bool Mirrors(Service::CAM::Flip flip) { return flip == Service::CAM::Flip::Horizontal || flip == Service::CAM::Flip::Reverse; }
			static bool Inverts(Service::CAM::Flip flip) { return flip == Service::CAM::Flip::Vertical || flip == Service::CAM::Flip::Reverse; }

			// The frame the game asked for: the picture fitted into it on white (black with none), as
			// RGB565 or as YUV 4:2:2, two pixels a Y each and a U and a V between them (BT.601, as
			// libyuv makes it for Azahar's other cameras)
			void Build()
			{
				const int width = std::max(m_outWidth, 0), height = std::max(m_outHeight, 0);
				if (m_rgba.empty())
				{
					// no picture: black, as Azahar's blank camera makes it (0x8000: Y 0, U and V 128)
					m_frame.assign((size_t)width * height, m_rgb565 ? 0 : 0x8000);
					return;
				}
				std::vector<uint8_t> rgb((size_t)width * height * 3, 255);
				if (width > 0 && height > 0)
				{
					const float scale = std::min((float)width / m_width, (float)height / m_height);
					const int fitWidth = std::max(1, (int)(m_width * scale)), fitHeight = std::max(1, (int)(m_height * scale));
					const int left = (width - fitWidth) / 2, top = (height - fitHeight) / 2;
					for (int y = 0; y < fitHeight; y++)
						for (int x = 0; x < fitWidth; x++)
						{
							// the picture's pixels under this one, averaged (it is shrunk, mostly)
							const int x0 = x * m_width / fitWidth, x1 = std::max(x0 + 1, (x + 1) * m_width / fitWidth);
							const int y0 = y * m_height / fitHeight, y1 = std::max(y0 + 1, (y + 1) * m_height / fitHeight);
							int sum[4] = {}, count = 0;
							for (int sy = y0; sy < y1; sy++)
								for (int sx = x0; sx < x1; sx++)
								{
									const uint8_t* p = &m_rgba[((size_t)sy * m_width + sx) * 4];
									for (int c = 0; c < 4; c++)
										sum[c] += p[c];
									count++;
								}
							// over white, by its alpha (a transparent PNG's QR code)
							const int alpha = sum[3] / count;
							uint8_t* out = &rgb[((size_t)(top + y) * width + left + x) * 3];
							for (int c = 0; c < 3; c++)
								out[c] = (uint8_t)((sum[c] / count * alpha + 255 * (255 - alpha)) / 255);
						}
				}
				m_frame.assign((size_t)width * height, 0);
				auto source = [&](int x, int y) {
					return &rgb[((size_t)(m_invert ? height - 1 - y : y) * width + (m_mirror ? width - 1 - x : x)) * 3];
				};
				auto luma = [](const uint8_t* p) { return std::clamp(((66 * p[0] + 129 * p[1] + 25 * p[2] + 128) >> 8) + 16, 0, 255); };
				for (int y = 0; y < height; y++)
					for (int x = 0; x < width; x++)
					{
						const uint8_t* p = source(x, y);
						u16& out = m_frame[(size_t)y * width + x];
						if (m_rgb565)
						{
							out = (u16)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3);
							continue;
						}
						// a pair of pixels: each its Y, the first U and the second V, from the pair's average
						const int first = x & ~1, second = std::min(first + 1, width - 1);
						const uint8_t *a = source(first, y), *b = source(second, y);
						const int r = (a[0] + b[0]) / 2, g = (a[1] + b[1]) / 2, bl = (a[2] + b[2]) / 2;
						const int chroma = x == first ? ((-38 * r - 74 * g + 112 * bl + 128) >> 8) + 128 :
														((112 * r - 94 * g - 18 * bl + 128) >> 8) + 128;
						out = (u16)(luma(p) | std::clamp(chroma, 0, 255) << 8);
					}
			}

			std::string m_folder;
			bool m_baseMirror, m_baseInvert, m_mirror = false, m_invert = false, m_rgb565 = false;
			int m_outWidth = 0, m_outHeight = 0;
			std::vector<uint8_t> m_rgba; // the picture, as it was read
			int m_width = 0, m_height = 0;
			std::vector<u16> m_frame; // the last frame made, until something it depends on changes
		};

		class PictureFactory final : public Camera::CameraFactory
		{
		public:
			std::unique_ptr<Camera::CameraInterface> Create(const std::string& config, const Service::CAM::Flip& flip) override
			{
				return std::make_unique<PictureCamera>(config, flip);
			}
		};
	}

	void RegisterPictureCamera()
	{
		static bool s_registered = false;
		if (!s_registered)
			Camera::RegisterFactory(kPictureCamera, std::make_unique<PictureFactory>());
		s_registered = true;
	}
}
