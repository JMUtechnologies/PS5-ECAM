// SPDX-License-Identifier: GPL-3.0-or-later
// Wii banner (opening.bnr) extraction for launcher images. Based on Porpoise-X's porpoise_banner.cpp.
#include "porpoise_banner.h"
#include "porpoise_disc.h"
#include "../ps5/log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma clang diagnostic pop

namespace ps5porpoise
{
namespace
{
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s16 = std::int16_t;

u16 be16(const u8* p) { return u16(p[0] << 8 | p[1]); }
u32 be32(const u8* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }
u32 le32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }

struct Reader
{
	const u8* data = nullptr;
	std::size_t size = 0;
	bool ok(std::size_t at, std::size_t n) const { return at <= size && n <= size - at; }
	u8 u8_(std::size_t at) const { return ok(at, 1) ? data[at] : 0; }
	u16 u16_(std::size_t at) const { return ok(at, 2) ? u16(data[at] << 8 | data[at + 1]) : 0; }
	u32 u32_(std::size_t at) const
	{
		return ok(at, 4) ? u32(data[at]) << 24 | u32(data[at + 1]) << 16 | u32(data[at + 2]) << 8 | data[at + 3] : 0;
	}
	float f32(std::size_t at) const
	{
		if (!ok(at, 4)) return 0.0f;
		u32 v = u32_(at);
		return *reinterpret_cast<const float*>(&v);
	}
	std::string str(std::size_t at, std::size_t max_len) const
	{
		std::string s;
		for (std::size_t i = 0; i < max_len && ok(at + i, 1); ++i)
		{
			if (data[at + i] == 0) break;
			s += char(data[at + i]);
		}
		return s;
	}
};

std::string lower(std::string s)
{
	for (char& c : s)
		if (c >= 'A' && c <= 'Z')
			c = char(c - 'A' + 'a');
	return s;
}

bool read_u8(const std::vector<u8>& bytes, std::size_t base, std::map<std::string, std::pair<std::size_t, std::size_t>>& files)
{
	Reader r{ bytes.data(), bytes.size() };
	if (r.u32_(base) != 0x55AA382D) return false;
	const std::size_t root = base + r.u32_(base + 4);
	const u32 count = r.u32_(root + 8);
	if (count == 0 || count > 100000 || !r.ok(root, std::size_t(count) * 12)) return false;
	const std::size_t names = root + std::size_t(count) * 12;
	std::vector<std::pair<u32, std::string>> dirs = { {count, ""} };
	for (u32 i = 1; i < count; ++i)
	{
		while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
		const std::size_t n = root + std::size_t(i) * 12;
		const bool dir = r.u8_(n) == 1;
		const std::string name = r.str(names + (r.u32_(n) & 0xFFFFFF), 256);
		const std::string path = dirs.back().second.empty() ? name : dirs.back().second + "/" + name;
		if (dir) dirs.push_back({ r.u32_(n + 8), path });
		else files[lower(path)] = { base + r.u32_(n + 4), r.u32_(n + 8) };
	}
	return true;
}

std::vector<u8> unlz(const u8* p, std::size_t n)
{
	std::size_t at = 0;
	if (n >= 4 && std::memcmp(p, "LZ77", 4) == 0) at = 4;
	if (n < at + 4 || (p[at] != 0x10 && p[at] != 0x11)) return std::vector<u8>(p, p + n);
	const u8 type = p[at];
	std::size_t out_size = std::size_t(p[at + 1]) | std::size_t(p[at + 2]) << 8 | std::size_t(p[at + 3]) << 16;
	at += 4;
	if (out_size == 0 && n >= at + 4)
	{
		out_size = std::size_t(p[at]) | std::size_t(p[at + 1]) << 8 | std::size_t(p[at + 2]) << 16 | std::size_t(p[at + 3]) << 24;
		at += 4;
	}
	if (out_size > (64u << 20)) return {};
	std::vector<u8> out; out.reserve(out_size);
	while (out.size() < out_size && at < n)
	{
		const u8 flags = p[at++];
		for (int b = 0; b < 8 && out.size() < out_size; ++b)
		{
			if (!(flags & (0x80 >> b)))
			{
				if (at >= n) break;
				out.push_back(p[at++]); continue;
			}
			std::size_t len = 0, disp = 0;
			if (type == 0x10)
			{
				if (at + 2 > n) break;
				len = (p[at] >> 4) + 3;
				disp = ((std::size_t(p[at]) & 0xF) << 8 | p[at + 1]) + 1;
				at += 2;
			}
			else
			{
				if (at + 2 > n) break;
				const u8 ind = p[at] >> 4;
				if (ind == 0)
				{
					if (at + 3 > n) break;
					len = ((std::size_t(p[at]) & 0xF) << 4 | p[at + 1] >> 4) + 0x11;
					disp = ((std::size_t(p[at + 1]) & 0xF) << 8 | p[at + 2]) + 1;
					at += 3;
				}
				else if (ind == 1)
				{
					if (at + 4 > n) break;
					len = ((std::size_t(p[at]) & 0xF) << 12 | std::size_t(p[at + 1]) << 4 | p[at + 2] >> 4) + 0x111;
					disp = ((std::size_t(p[at + 2]) & 0xF) << 8 | p[at + 3]) + 1;
					at += 4;
				}
				else
				{
					len = ind + 1;
					disp = ((std::size_t(p[at]) & 0xF) << 8 | p[at + 1]) + 1;
					at += 2;
				}
			}
			if (disp > out.size()) return {};
			for (std::size_t k = 0; k < len && out.size() < out_size; ++k) out.push_back(out[out.size() - disp]);
		}
	}
	return out;
}

bool inner_file(const std::vector<u8>& bnr, const std::string& name, std::vector<u8>& out)
{
	std::map<std::string, std::pair<std::size_t, std::size_t>> files;
	bool found = false;
	for (std::size_t base : { std::size_t(0x600), std::size_t(0x640), std::size_t(0) })
		if (bnr.size() > base + 4 && read_u8(bnr, base, files)) { found = true; break; }
	if (!found) return false;
	const auto it = files.find(name);
	if (it == files.end() || it->second.first + it->second.second > bnr.size()) return false;
	std::size_t at = it->second.first, n = it->second.second;
	if (n >= 32 && std::memcmp(&bnr[at], "IMD5", 4) == 0) { at += 32; n -= 32; }
	out = unlz(&bnr[at], n);
	return !out.empty();
}

struct Tex
{
	int w = 0, h = 0;
	std::vector<float> rgba;
};

void rgb5a3(u16 v, float* o)
{
	if (v & 0x8000) { o[0] = float((v >> 10) & 31) / 31.0f; o[1] = float((v >> 5) & 31) / 31.0f; o[2] = float(v & 31) / 31.0f; o[3] = 1; }
	else { o[0] = float((v >> 8) & 15) / 15.0f; o[1] = float((v >> 4) & 15) / 15.0f; o[2] = float(v & 15) / 15.0f; o[3] = float((v >> 12) & 7) / 7.0f; }
}

void rgb565(u16 v, float* o)
{
	o[0] = float((v >> 11) & 31) / 31.0f; o[1] = float((v >> 5) & 63) / 63.0f; o[2] = float(v & 31) / 31.0f; o[3] = 1;
}

void palette_entry(u16 v, u32 fmt, float* o)
{
	if (fmt == 0) { o[0] = o[1] = o[2] = float(v & 0xFF) / 255.0f; o[3] = float(v >> 8) / 255.0f; }
	else if (fmt == 1) rgb565(v, o); else rgb5a3(v, o);
}

bool decode_tpl(const u8* p, std::size_t n, Tex& t)
{
	Reader r{ p, n };
	if (r.u32_(0) != 0x0020AF30 || r.u32_(4) == 0) return false;
	const std::size_t table = r.u32_(8);
	const std::size_t img = r.u32_(table), pal = r.u32_(table + 4);
	const int h = r.u16_(img), w = r.u16_(img + 2);
	const u32 fmt = r.u32_(img + 4);
	const std::size_t data = r.u32_(img + 8);
	if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
	std::vector<u16> palette; u32 pal_fmt = 2;
	if (pal) { const u16 count = r.u16_(pal); pal_fmt = r.u32_(pal + 4); const std::size_t pd = r.u32_(pal + 8); for (u16 i = 0; i < count; ++i) palette.push_back(r.u16_(pd + std::size_t(i) * 2)); }
	t.w = w; t.h = h; t.rgba.assign(std::size_t(w) * h * 4, 0.0f);
	int bw = 4, bh = 4;
	switch (fmt) { case 0: case 8: case 14: bw = 8; bh = 8; break; case 1: case 2: case 9: bw = 8; bh = 4; break; default: break; }
	const int bx_n = (w + bw - 1) / bw, by_n = (h + bh - 1) / bh; std::size_t at = data;
	auto put = [&](int x, int y, const float* c) { if (x < w && y < h) std::memcpy(&t.rgba[(std::size_t(y) * w + x) * 4], c, sizeof(float) * 4); };
	for (int by = 0; by < by_n; ++by)
		for (int bx = 0; bx < bx_n; ++bx)
		{
			const int x0 = bx * bw, y0 = by * bh; float c[4];
			switch (fmt)
			{
			case 0:
				for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; x += 2) { const u8 b = r.u8_(at++); c[0] = c[1] = c[2] = c[3] = float(b >> 4) / 15.0f; put(x0 + x, y0 + y, c); c[0] = c[1] = c[2] = c[3] = float(b & 15) / 15.0f; put(x0 + x + 1, y0 + y, c); } break;
			case 1: for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) { c[0] = c[1] = c[2] = c[3] = float(r.u8_(at++)) / 255.0f; put(x0 + x, y0 + y, c); } break;
			case 2: for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) { const u8 b = r.u8_(at++); c[0] = c[1] = c[2] = float(b & 15) / 15.0f; c[3] = float(b >> 4) / 15.0f; put(x0 + x, y0 + y, c); } break;
			case 3: for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) { c[3] = float(r.u8_(at)) / 255.0f; c[0] = c[1] = c[2] = float(r.u8_(at + 1)) / 255.0f; at += 2; put(x0 + x, y0 + y, c); } break;
			case 4: case 5: for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) { const u16 v = r.u16_(at); at += 2; if (fmt == 4) rgb565(v, c); else rgb5a3(v, c); put(x0 + x, y0 + y, c); } break;
			case 6: for (int i = 0; i < 16; ++i) { c[3] = float(r.u8_(at + i * 2)) / 255.0f; c[0] = float(r.u8_(at + i * 2 + 1)) / 255.0f; c[1] = float(r.u8_(at + 32 + i * 2)) / 255.0f; c[2] = float(r.u8_(at + 32 + i * 2 + 1)) / 255.0f; put(x0 + i % 4, y0 + i / 4, c); } at += 64; break;
			case 8: for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; x += 2) { const u8 b = r.u8_(at++); for (int k = 0; k < 2; ++k) { const u8 idx = k ? b & 15 : b >> 4; palette_entry(idx < palette.size() ? palette[idx] : 0, pal_fmt, c); put(x0 + x + k, y0 + y, c); } } break;
			case 9: for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) { const u8 idx = r.u8_(at++); palette_entry(idx < palette.size() ? palette[idx] : 0, pal_fmt, c); put(x0 + x, y0 + y, c); } break;
			case 10: for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) { const u16 idx = r.u16_(at) & 0x3FFF; at += 2; palette_entry(idx < palette.size() ? palette[idx] : 0, pal_fmt, c); put(x0 + x, y0 + y, c); } break;
			case 14: for (int sub = 0; sub < 4; ++sub) { const u16 c0 = r.u16_(at), c1 = r.u16_(at + 2); float pal4[4][4]; rgb565(c0, pal4[0]); rgb565(c1, pal4[1]); if (c0 > c1) for (int k = 0; k < 4; ++k) { pal4[2][k] = (2 * pal4[0][k] + pal4[1][k]) / 3; pal4[3][k] = (pal4[0][k] + 2 * pal4[1][k]) / 3; } else for (int k = 0; k < 4; ++k) { pal4[2][k] = (pal4[0][k] + pal4[1][k]) / 2; pal4[3][k] = 0; } for (int y = 0; y < 4; ++y) { const u8 bits = r.u8_(at + 4 + y); for (int x = 0; x < 4; ++x) put(x0 + (sub & 1) * 4 + x, y0 + (sub >> 1) * 4 + y, pal4[(bits >> (6 - x * 2)) & 3]); } at += 8; } break;
			default: return false;
			}
		}
	return true;
}

constexpr u32 fourcc(const char* s) { return u32(u8(s[0])) << 24 | u32(u8(s[1])) << 16 | u32(u8(s[2])) << 8 | u8(s[3]); }

struct SimpleLayout
{
	float width = 0, height = 0;
	std::vector<std::string> tex_names;
	std::vector<Tex> textures;
	std::vector<int> pic_mat_indices; // material indices for picture panes
};

bool read_layout_simple(const std::vector<u8>& arc_bytes, const std::map<std::string, std::pair<std::size_t, std::size_t>>& files, const std::string& brlyt, SimpleLayout& lay)
{
	const auto it = files.find(brlyt);
	if (it == files.end()) return false;
	Reader r{ arc_bytes.data() + it->second.first, std::min(it->second.second, arc_bytes.size() - it->second.first) };
	if (r.u32_(0) != fourcc("RLYT")) return false;
	const std::size_t first = r.u16_(12);
	const u16 sections = r.u16_(14);
	std::size_t at = first;
	for (u16 s = 0; s < sections && r.ok(at, 8); ++s)
	{
		const u32 magic = r.u32_(at), size = r.u32_(at + 4);
		const std::size_t body = at + 8;
		if (magic == fourcc("lyt1")) { lay.width = r.f32(body + 4); lay.height = r.f32(body + 8); }
		else if (magic == fourcc("txl1")) { const u16 n = r.u16_(body); const std::size_t table = body + 4; for (u16 i = 0; i < n; ++i) lay.tex_names.push_back(lower(r.str(table + r.u32_(table + std::size_t(i) * 8), 128))); }
		else if (magic == fourcc("pic1") || magic == fourcc("bnd1")) { const u16 mat_idx = r.u16_(body + 0x44 + 16); lay.pic_mat_indices.push_back(mat_idx); }
		if (size < 8) break;
		at += size;
	}
	for (const std::string& name : lay.tex_names)
	{
		Tex t; const auto ti = files.find("arc/timg/" + lower(name));
		if (ti != files.end() && ti->second.first + ti->second.second <= arc_bytes.size()) decode_tpl(arc_bytes.data() + ti->second.first, ti->second.second, t);
		lay.textures.push_back(std::move(t));
	}
	return true;
}

bool extract_banner_from_opening_bnr(const std::vector<u8>& bnr, std::vector<std::uint8_t>& out_png, int& out_w, int& out_h, std::string& error)
{
	std::vector<u8> arc;
	if (!inner_file(bnr, "meta/banner.bin", arc)) { error = "no meta/banner.bin"; return false; }
	std::map<std::string, std::pair<std::size_t, std::size_t>> files;
	if (!read_u8(arc, 0, files)) { error = "banner.bin is not an archive"; return false; }
	SimpleLayout lay;
	if (!read_layout_simple(arc, files, "arc/blyt/banner.brlyt", lay)) { error = "no banner.brlyt"; return false; }
	// Find the texture used by the first picture pane
	if (lay.pic_mat_indices.empty()) { error = "no picture pane in banner"; return false; }
	// We need to find which texture the material uses. For simplicity, use the largest texture.
	int best_idx = -1; int best_area = 0;
	for (int i = 0; i < (int)lay.textures.size(); ++i) { int area = lay.textures[i].w * lay.textures[i].h; if (area > best_area) { best_area = area; best_idx = i; } }
	if (best_idx < 0) { error = "no textures in banner"; return false; }
	const Tex& t = lay.textures[best_idx];
	out_w = t.w; out_h = t.h;
	std::vector<u8> rgba(std::size_t(t.w) * t.h * 4);
	for (std::size_t i = 0; i < t.rgba.size(); ++i) rgba[i] = std::uint8_t(std::lround(t.rgba[i] * 255.0f));
	// Write as PNG to memory
	std::vector<unsigned char> png_data;
	int stride = t.w * 4;
	int png_len = 0;
	unsigned char* png = stbi_write_png_to_mem(rgba.data(), stride, t.w, t.h, 4, &png_len);
	if (!png || png_len <= 0) { error = "failed to encode PNG"; return false; }
	out_png.assign(png, png + png_len);
	free(png);
	return true;
}
} // namespace

bool ExtractWiiBanner(const std::string& disc_path, const std::string& output_path, std::string& error)
{
	// Read opening.bnr from the disc
	std::vector<u8> bnr;
	if (!disc::read_file(disc_path, "opening.bnr", bnr, error)) return false;
	// Extract banner texture and save as PNG
	std::vector<std::uint8_t> png_data; int w, h;
	if (!extract_banner_from_opening_bnr(bnr, png_data, w, h, error)) return false;
	// Write PNG to file
	FILE* f = std::fopen(output_path.c_str(), "wb");
	if (!f) { error = "can't write output file"; return false; }
	std::fwrite(png_data.data(), 1, png_data.size(), f);
	std::fclose(f);
	return true;
}
} // namespace ps5porpoise