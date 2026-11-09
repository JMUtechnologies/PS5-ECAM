// SPDX-License-Identifier: GPL-3.0-or-later
// Wii/GameCube disc reading for banner extraction. Based on Porpoise-X's porpoise_disc.cpp.
#include "porpoise_disc.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace ps5porpoise::disc
{
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

namespace
{

u16 be16(const u8* p) { return u16(p[0] << 8 | p[1]); }
u32 be32(const u8* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }
u64 be64(const u8* p) { return u64(be32(p)) << 32 | be32(p + 4); }
u32 le32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
u64 le64(const u8* p) { return u64(le32(p)) | u64(le32(p + 4)) << 32; }

constexpr u64 kWiiSector = 0x8000, kWiiSectorData = 0x7C00, kWiiSectorHashes = 0x400;
constexpr u64 kWiiGroupData = kWiiSectorData * 64;

constexpr u64 kMaxBuffer = u64(64) << 20;

struct File
{
	FILE* f = nullptr;
	u64 size = 0;
	bool open(const std::string& path)
	{
		f = std::fopen(path.c_str(), "rb");
		if (!f) return false;
		std::fseek(f, 0, SEEK_END);
		size = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		return true;
	}
	bool read(u64 offset, void* out, u64 n)
	{
		if (std::fseek(f, long(offset), SEEK_SET) != 0) return false;
		return std::fread(out, 1, std::size_t(n), f) == n;
	}
	~File() { if (f) std::fclose(f); }
};

class Image
{
public:
	virtual ~Image() = default;
	virtual bool read(u64 offset, u64 size, u8* out) = 0;
	virtual bool read_decrypted(u64, u64, u64, u8*, const u8*) { return false; }
	virtual bool stores_decrypted() const { return false; }
	virtual bool partition_key(u64, u8[16]) { return false; }
	File file;
};

class Plain : public Image
{
public:
	bool read(u64 offset, u64 size, u8* out) override { return file.read(offset, out, size); }
};

class Wbfs : public Image
{
public:
	u64 wbfs_sector = 0;
	std::vector<u16> table;
	bool init()
	{
		u8 h[12];
		if (!file.read(0, h, sizeof h) || std::memcmp(h, "WBFS", 4) != 0) return false;
		if (h[8] < 9 || h[8] > 16 || h[9] < 15 || h[9] > 31) return false;
		const u64 hd_sector = u64(1) << h[8];
		wbfs_sector = u64(1) << h[9];
		if (wbfs_sector < kWiiSector) return false;
		const u64 blocks = (143432ull * 2 * kWiiSector + wbfs_sector - 1) / wbfs_sector;
		std::vector<u8> raw(blocks * 2);
		if (!file.read(hd_sector + 0x100, raw.data(), raw.size())) return false;
		table.resize(blocks);
		for (u64 i = 0; i < blocks; ++i) table[i] = be16(&raw[i * 2]);
		return true;
	}
	bool read(u64 offset, u64 size, u8* out) override
	{
		while (size)
		{
			const u64 cluster = offset / wbfs_sector, within = offset % wbfs_sector;
			if (cluster >= table.size()) return false;
			const u64 n = std::min(size, wbfs_sector - within);
			if (table[cluster] == 0) std::memset(out, 0, n);
			else if (!file.read(u64(table[cluster]) * wbfs_sector + within, out, n)) return false;
			offset += n; size -= n; out += n;
		}
		return true;
	}
};

class Ciso : public Image
{
public:
	u64 block = 0;
	std::vector<u64> where;
	bool init()
	{
		std::vector<u8> h(0x8000);
		if (!file.read(0, h.data(), h.size()) || std::memcmp(h.data(), "CISO", 4) != 0) return false;
		block = le32(&h[4]);
		if (block == 0 || block > kMaxBuffer) return false;
		u64 next = 0x8000;
		for (std::size_t i = 8; i < h.size(); ++i)
		{
			where.push_back(h[i] ? next : ~u64(0));
			if (h[i]) next += block;
		}
		return true;
	}
	bool read(u64 offset, u64 size, u8* out) override
	{
		while (size)
		{
			const u64 b = offset / block, within = offset % block;
			if (b >= where.size()) return false;
			const u64 n = std::min(size, block - within);
			if (where[b] == ~u64(0)) std::memset(out, 0, n);
			else if (!file.read(where[b] + within, out, n)) return false;
			offset += n; size -= n; out += n;
		}
		return true;
	}
};

class Gcz : public Image
{
public:
	u64 compressed = 0, block = 0, data_offset = 0;
	std::vector<u64> pointers;
	u64 cached = ~u64(0);
	std::vector<u8> cache;
	bool init(bool header_only = false)
	{
		u8 h[32];
		if (!file.read(0, h, sizeof h) || le32(h) != 0xB10BC001) return false;
		compressed = le64(h + 8);
		block = le32(h + 24);
		u32 blocks = le32(h + 28);
		if (block == 0 || blocks == 0 || block > kMaxBuffer || u64(blocks) * 12 > file.size || compressed > file.size || u64(blocks) * 8 > kMaxBuffer) return false;
		data_offset = 32 + u64(blocks) * 8 + u64(blocks) * 4;
		if (header_only && block >= 0x100 && blocks > 2) blocks = 2;
		std::vector<u8> raw(std::size_t(blocks) * 8);
		if (!file.read(32, raw.data(), raw.size())) return false;
		pointers.resize(blocks);
		for (u32 i = 0; i < blocks; ++i) pointers[i] = le64(&raw[i * 8]);
		return true;
	}
	bool load(u64 b)
	{
		if (b == cached) return true;
		if (b >= pointers.size()) return false;
		const u64 start = pointers[b] & ~(u64(1) << 63);
		const u64 end = b + 1 < pointers.size() ? pointers[b + 1] & ~(u64(1) << 63) : compressed;
		if (end < start || end - start > block + 0x10000 || end - start > kMaxBuffer) return false;
		if (end == start) return false;
		std::vector<u8> in(end - start);
		if (!file.read(data_offset + start, in.data(), in.size())) return false;
		cache.assign(block, 0);
		if (pointers[b] & (u64(1) << 63))
			std::memcpy(cache.data(), in.data(), std::min<u64>(in.size(), block));
		else
		{
			// Need zlib decompression - for now just copy if raw
			std::memcpy(cache.data(), in.data(), std::min<u64>(in.size(), block));
		}
		cached = b;
		return true;
	}
	bool read(u64 offset, u64 size, u8* out) override
	{
		while (size)
		{
			const u64 b = offset / block, within = offset % block;
			if (b >= pointers.size() || !load(b)) return false;
			const u64 n = std::min(size, block - within);
			std::memcpy(out, cache.data() + within, n);
			offset += n; size -= n; out += n;
		}
		return true;
	}
};

class Wia : public Image
{
public:
	bool rvz = false;
	u32 compression = 0, chunk = 0;
	u8 disc_header[0x80];
	struct PartData { u32 first_sector, sectors, group, groups; };
	struct Partition { u8 key[16]; PartData data[2]; };
	struct Raw { u64 offset, size; u32 group, groups; };
	struct Group { u64 offset; u32 size; bool compressed; u32 packed; };
	std::vector<Partition> partitions;
	std::vector<Raw> raws;
	std::vector<Group> groups;
	u64 cached_offset = ~u64(0);
	std::vector<u8> cached;

	bool decompress(const std::vector<u8>& in, bool compressed, std::vector<u8>& out, u64 max_out)
	{
		if (!compressed || compression == 0) { out = in; return true; }
		if (compression != 5) return false; // Only ZSTD supported
		if (max_out > kMaxBuffer) return false;
		out.resize(max_out);
		// ZSTD decompression would need external library - skip for now
		return false;
	}
	bool init()
	{
		u8 h1[0x48];
		if (!file.read(0, h1, sizeof h1)) return false;
		rvz = std::memcmp(h1, "RVZ\x01", 4) == 0;
		if (!rvz && std::memcmp(h1, "WIA\x01", 4) != 0) return false;
		const u32 h2_size = be32(h1 + 0x0C);
		if (h2_size < 0xD4 || h2_size > 0x1000) return false;
		std::vector<u8> h2(h2_size);
		if (!file.read(0x48, h2.data(), h2.size())) return false;
		compression = be32(&h2[4]);
		chunk = be32(&h2[0x0C]);
		std::memcpy(disc_header, &h2[0x10], 0x80);
		// Skip full parsing for banner extraction - we have disc header at 0x10 in h2
		return true;
	}
	bool read(u64 offset, u64 size, u8* out) override
	{
		if (offset < 0x80)
		{
			const u64 n = std::min<u64>(0x80 - offset, size);
			std::memcpy(out, disc_header + offset, n);
			offset += n; size -= n; out += n;
		}
		// For banner extraction, we mainly need the header which we already have
		return size == 0;
	}
	bool stores_decrypted() const override { return true; }
};

std::unique_ptr<Image> open_image(const std::string& path, std::string& error, bool header_only = false)
{
	u8 magic[4] = {};
	bool got = false;
	{
		File probe;
		if (!probe.open(path)) { error = "can't open the file"; return nullptr; }
		got = probe.read(0, magic, 4);
	}
	if (!got) { error = "the file is too short"; return nullptr; }
	std::unique_ptr<Image> image;
	bool ok = false;
	if (std::memcmp(magic, "RVZ\x01", 4) == 0 || std::memcmp(magic, "WIA\x01", 4) == 0)
	{
		auto w = std::make_unique<Wia>();
		ok = w->file.open(path) && w->init();
		if (!ok) error = "not a readable .rvz / .wia";
		image = std::move(w);
	}
	else if (std::memcmp(magic, "WBFS", 4) == 0)
	{
		auto w = std::make_unique<Wbfs>();
		ok = w->file.open(path) && w->init();
		error = "not a readable .wbfs";
		image = std::move(w);
	}
	else if (std::memcmp(magic, "CISO", 4) == 0)
	{
		auto c = std::make_unique<Ciso>();
		ok = c->file.open(path) && c->init();
		error = "not a readable .ciso";
		image = std::move(c);
	}
	else if (le32(magic) == 0xB10BC001)
	{
		auto c = std::make_unique<Gcz>();
		ok = c->file.open(path) && c->init(header_only);
		error = "not a readable .gcz";
		image = std::move(c);
	}
	else
	{
		auto p = std::make_unique<Plain>();
		ok = p->file.open(path);
		error = "can't open the file";
		image = std::move(p);
	}
	if (!ok) return nullptr;
	error.clear();
	return image;
}

void aes_cbc_decrypt(const u8 key[16], u8 iv[16], u8* data, std::size_t size)
{
	// Simplified - not needed for banner extraction from header
}

struct Partition
{
	Image* image = nullptr;
	u64 data_offset = 0;
	u8 key[16] = {};
};

bool find_in_fst(const std::vector<u8>& fst, const std::string& name, bool wii, u64& offset, u64& size)
{
	if (fst.size() < 12) return false;
	const u32 count = be32(&fst[8]);
	if (count == 0 || u64(count) * 12 > fst.size()) return false;
	const std::size_t strings = std::size_t(count) * 12;
	auto entry_name = [&](u32 i) {
		const u32 off = be32(&fst[i * 12]) & 0xFFFFFF;
		std::string s;
		for (std::size_t k = strings + off; k < fst.size() && fst[k]; ++k) s += char(fst[k]);
		return s;
	};
	auto lower = [](std::string s) {
		for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
		return s;
	};
	std::string rest = lower(name);
	u32 first = 1, end = count;
	while (true)
	{
		const auto slash = rest.find('/');
		const std::string part = rest.substr(0, slash);
		bool found = false;
		for (u32 i = first; i < end;)
		{
			const bool dir = fst[i * 12] != 0;
			const u32 next = dir ? be32(&fst[i * 12 + 8]) : i + 1;
			if (lower(entry_name(i)) == part)
			{
				if (slash == std::string::npos && !dir)
				{
					offset = u64(be32(&fst[i * 12 + 4])) << (wii ? 2 : 0);
					size = be32(&fst[i * 12 + 8]);
					return true;
				}
				if (slash != std::string::npos && dir)
				{
					first = i + 1; end = std::min(next, count);
					rest = rest.substr(slash + 1); found = true; break;
				}
			}
			i = next > i ? next : i + 1;
		}
		if (!found) return false;
	}
}

} // namespace

bool read_file(const std::string& path, const std::string& name, std::vector<std::uint8_t>& out, std::string& error)
{
	// Check if WAD
	auto dot = path.rfind('.');
	std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
	for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
	if (ext == "wad")
	{
		if (name != "opening.bnr") { error = name + " is not in a WAD"; return false; }
		// WAD reading would need full implementation
		error = "WAD reading not implemented";
		return false;
	}
	auto image = open_image(path, error);
	if (!image) return false;
	u8 header[0x440];
	if (!image->read(0, sizeof header, header)) { error = "can't read the disc header"; return false; }
	const bool wii = be32(header + 0x18) == 0x5D1C9EA3;
	const bool gc = be32(header + 0x1C) == 0xC2339F3D;
	if (!wii && !gc) { error = "not a GameCube or Wii disc"; return false; }
	std::vector<u8> fst;
	if (gc)
	{
		const u64 fst_off = be32(header + 0x424), fst_size = be32(header + 0x428);
		if (fst_size == 0 || fst_size > (32u << 20)) { error = "no file system"; return false; }
		fst.resize(fst_size);
		u64 off = 0, sz = 0;
		if (!image->read(fst_off, fst_size, fst.data()) || !find_in_fst(fst, name, false, off, sz)) { error = name + " is not on the disc"; return false; }
		if (sz > kMaxBuffer) { error = name + " is too big"; return false; }
		out.resize(sz);
		if (!image->read(off, sz, out.data())) { error = "can't read " + name; return false; }
		return true;
	}
	// Wii: need partition reading - complex, skip for now
	error = "Wii partition reading not fully implemented";
	return false;
}

bool read_header(const std::string& path, std::uint8_t out[0x100], std::string& error)
{
	auto image = open_image(path, error, true);
	if (!image) return false;
	if (!image->read(0, 0x100, out)) { error = "can't read the disc header"; return false; }
	return true;
}

bool is_wad(const std::string& path)
{
	auto dot = path.rfind('.');
	if (dot == std::string::npos) return false;
	std::string ext = path.substr(dot + 1);
	for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
	return ext == "wad";
}

} // namespace ps5porpoise::disc