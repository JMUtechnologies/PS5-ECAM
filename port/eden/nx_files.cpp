// SPDX-License-Identifier: GPL-3.0-or-later
#include "nx_files.h"

#ifndef PS5CEMU_LAUNCHER_PREVIEW
#include <openssl/evp.h>
#include <turbojpeg.h>
#endif

#include <fmt/format.h>
#include <sys/types.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

namespace ps5eden::nx
{
	namespace
	{
		constexpr size_t kNcaHeaderSize = 0xC00;
		constexpr uint64_t kMediaUnit = 0x200;
		constexpr uint32_t kMostEntries = 4096;		 // a package's files, at most
		constexpr uint32_t kLargestStrings = 1 << 20; // its names
		constexpr uint64_t kLargestMeta = 4 << 20;	 // a RomFS's file table
		constexpr uint64_t kLargestIcon = 4 << 20;
		constexpr size_t kNacpNames = 16 * 0x300; // a name (0x200) and a publisher (0x100) a language
		// the icons a control RomFS can have, in the order Eden takes them (the languages' order in NACP)
		constexpr const char* kLanguages[] = {"AmericanEnglish", "BritishEnglish", "Japanese", "French", "German",
			"LatinAmericanSpanish", "Spanish", "Italian", "Dutch", "CanadianFrench", "Portuguese", "Russian", "Korean",
			"TraditionalChinese", "SimplifiedChinese", "BrazilianPortuguese"};

		uint32_t U32(const uint8_t* p)
		{
			return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
		}
		uint64_t U64(const uint8_t* p)
		{
			return U32(p) | (uint64_t)U32(p + 4) << 32;
		}

		int Nibble(char c)
		{
			return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		}

		bool Hex(const std::string& text, uint8_t* out, size_t size)
		{
			if (text.size() != size * 2)
				return false;
			for (size_t i = 0; i < size; i++)
			{
				const int high = Nibble(text[i * 2]), low = Nibble(text[i * 2 + 1]);
				if (high < 0 || low < 0)
					return false;
				out[i] = (uint8_t)(high << 4 | low);
			}
			return true;
		}

		std::string ToHex(const uint8_t* data, size_t size)
		{
			static const char digits[] = "0123456789abcdef";
			std::string text;
			for (size_t i = 0; i < size; i++)
			{
				text += digits[data[i] >> 4];
				text += digits[data[i] & 15];
			}
			return text;
		}

		// "name = value" lines of a keys file, lower-case names, spaces and comments aside
		std::map<std::string, std::string> KeyLines(const std::string& path)
		{
			std::map<std::string, std::string> lines;
			std::ifstream in(path);
			for (std::string line; std::getline(in, line);)
			{
				line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char c) { return std::isspace(c); }), line.end());
				const size_t equals = line.find('=');
				if (line.empty() || line[0] == ';' || line[0] == '#' || equals == std::string::npos || equals == 0)
					continue;
				std::string name = line.substr(0, equals);
				for (char& c : name)
					c = (char)std::tolower((unsigned char)c);
				lines[name] = line.substr(equals + 1);
			}
			return lines;
		}

		// A file read at any offset: local, or on a network share (the C library's calls answer both)
		struct File
		{
			std::FILE* file = nullptr;
			uint64_t size = 0;

			explicit File(const std::string& path)
			{
				file = std::fopen(path.c_str(), "rb");
				if (file && fseeko(file, 0, SEEK_END) == 0)
				{
					const off_t end = ftello(file);
					size = end > 0 ? (uint64_t)end : 0;
				}
			}
			~File()
			{
				if (file)
					std::fclose(file);
			}
			File(const File&) = delete;
			File& operator=(const File&) = delete;

			bool Read(uint64_t offset, void* out, uint64_t bytes) const
			{
				return file && offset <= size && bytes <= size - offset && fseeko(file, (off_t)offset, SEEK_SET) == 0 &&
					std::fread(out, 1, (size_t)bytes, file) == bytes;
			}
		};

#ifndef PS5CEMU_LAUNCHER_PREVIEW
		// One AES-128 decryption (ECB, XTS or CTR), the whole of in to out
		bool Decrypt(const EVP_CIPHER* cipher, const uint8_t* key, const uint8_t* iv, const uint8_t* in, uint8_t* out, size_t size)
		{
			std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
			int written = 0, last = 0;
			return context && EVP_DecryptInit_ex(context.get(), cipher, nullptr, key, iv) == 1 &&
				EVP_CIPHER_CTX_set_padding(context.get(), 0) == 1 && EVP_DecryptUpdate(context.get(), out, &written, in, (int)size) == 1 &&
				EVP_DecryptFinal_ex(context.get(), out + written, &last) == 1 && (size_t)(written + last) == size;
		}

		bool DecryptKey(const Key& with, const uint8_t* in, Key& out)
		{
			return Decrypt(EVP_aes_128_ecb(), with.data(), nullptr, in, out.data(), 16);
		}

		// An NCA's header: 0x200-byte sectors, each with its number big-endian in the tweak
		bool DecryptHeader(const Keys& keys, const uint8_t* in, uint8_t* out)
		{
			for (size_t sector = 0; sector < kNcaHeaderSize / 0x200; sector++)
			{
				uint8_t tweak[16] = {};
				uint64_t number = sector;
				for (int i = 15; i >= 8; i--, number >>= 8)
					tweak[i] = (uint8_t)number;
				if (!Decrypt(EVP_aes_128_xts(), keys.header.data(), tweak, in + sector * 0x200, out + sector * 0x200, 0x200))
					return false;
			}
			return true;
		}

		// A section's bytes from offset on (an offset in the NCA): AES-CTR, the counter the section's
		// upper IV (big-endian) and the offset in 16-byte blocks
		bool DecryptCtr(const Key& key, const uint8_t* upper, uint64_t offset, uint8_t* data, size_t size)
		{
			uint8_t counter[16];
			for (int i = 0; i < 8; i++)
				counter[i] = upper[7 - i];
			uint64_t block = offset >> 4;
			for (int i = 15; i >= 8; i--, block >>= 8)
				counter[i] = (uint8_t)block;
			return Decrypt(EVP_aes_128_ctr(), key.data(), counter, data, data, size);
		}
#endif

		struct Entry
		{
			std::string name;
			uint64_t offset = 0; // in the file
			uint64_t size = 0;
		};

		// A PFS0 (an NSP) or an HFS0 (an XCI's partitions) at base: its files
		bool ReadPartition(const File& file, uint64_t base, bool hashed, std::vector<Entry>& entries)
		{
			uint8_t head[16];
			if (!file.Read(base, head, sizeof(head)) || std::memcmp(head, hashed ? "HFS0" : "PFS0", 4) != 0)
				return false;
			const uint32_t count = U32(head + 4), strings = U32(head + 8);
			const size_t entrySize = hashed ? 0x40 : 0x18;
			if (count == 0 || count > kMostEntries || strings > kLargestStrings)
				return false;
			std::vector<uint8_t> table(count * entrySize + strings + 1);
			if (!file.Read(base + sizeof(head), table.data(), table.size() - 1))
				return false;
			const uint64_t data = base + sizeof(head) + count * entrySize + strings;
			const char* names = (const char*)table.data() + count * entrySize;
			for (uint32_t i = 0; i < count; i++)
			{
				const uint8_t* entry = table.data() + i * entrySize;
				const uint32_t name = U32(entry + 16);
				if (name >= strings)
					return false;
				entries.push_back({std::string(names + name, strnlen(names + name, strings - name)), data + U64(entry), U64(entry + 8)});
			}
			return true;
		}

		bool EndsWith(std::string text, const char* suffix)
		{
			for (char& c : text)
				c = (char)std::tolower((unsigned char)c);
			const size_t length = std::strlen(suffix);
			return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
		}

		// A name that is text: UTF-8 with at least one character that has a shape, and no control
		// characters (a name can be empty or hold something else, and the next language's is then shown)
		bool UsableName(const std::string& name)
		{
			bool shaped = false;
			for (size_t i = 0; i < name.size();)
			{
				const unsigned char lead = name[i];
				const int length = lead < 0x80 ? 1 : lead >= 0xc2 && lead < 0xe0 ? 2 : lead >= 0xe0 && lead < 0xf0 ? 3 : lead >= 0xf0 && lead < 0xf5 ? 4 : 0;
				if (length == 0 || i + length > name.size())
					return false;
				uint32_t c = length == 1 ? lead : lead & (0xffu >> (length + 1));
				for (int k = 1; k < length; k++)
				{
					if (((unsigned char)name[i + k] & 0xc0) != 0x80)
						return false;
					c = c << 6 | ((unsigned char)name[i + k] & 0x3f);
				}
				if (c < 0x20 || c == 0x7f || c == 0xfffd)
					return false;
				shaped |= c > 0x20 && c != '?' && c != 0x3000 && c != 0xa0;
				i += length;
			}
			return shaped;
		}

		struct Nca
		{
			Entry entry;
			std::array<uint8_t, kNcaHeaderSize> header{};
			uint8_t contentType = 0;
			uint64_t programId = 0;
		};

		// The control NCA's RomFS: its control.nacp's first usable name, and the icon asked for
		bool ReadControl(const File& file, const Nca& nca, const Keys& keys, const std::map<std::string, Key>& tickets, bool icon, Info& info,
			std::string& why)
		{
#ifdef PS5CEMU_LAUNCHER_PREVIEW
			return false;
#else
			const uint8_t* h = nca.header.data();
			uint8_t generation = std::max(h[0x206], h[0x220]);
			if (generation > 0)
				generation--;
			// the sections' key: the ticket's title key for a rights ID, else the key area's CTR key
			Key key{};
			const uint8_t* rights = h + 0x230;
			if (std::any_of(rights, rights + 16, [](uint8_t b) { return b != 0; }))
			{
				const std::string id = ToHex(rights, 16);
				auto ticket = tickets.find(id);
				if (ticket == tickets.end())
					ticket = keys.titleKeys.find(id);
				const auto kek = keys.titlekek.find(generation);
				if (ticket == keys.titleKeys.end() || kek == keys.titlekek.end() || !DecryptKey(kek->second, ticket->second.data(), key))
				{
					why = fmt::format("no title key for rights ID {} (titlekek_{:02x})", id, generation);
					return false;
				}
			}
			else
			{
				const uint8_t index = h[0x207];
				const auto area = index < 3 ? keys.keyArea[index].find(generation) : keys.keyArea[0].end();
				if (index >= 3 || area == keys.keyArea[index].end() || !DecryptKey(area->second, h + 0x300 + 2 * 16, key))
				{
					why = fmt::format("no key_area_key_{}_{:02x}", index == 1 ? "ocean" : index == 2 ? "system" : "application", generation);
					return false;
				}
			}
			// its RomFS section, with integrity hashing: the data is the last level
			for (int section = 0; section < 4; section++)
			{
				const uint8_t* fs = h + 0x400 + section * 0x200;
				const uint64_t start = U32(h + 0x240 + section * 16) * kMediaUnit, end = U32(h + 0x244 + section * 16) * kMediaUnit;
				const uint8_t fsType = fs[2], hashType = fs[3], encryption = fs[4];
				if (end <= start || fsType != 0 || (hashType != 3 && hashType != 6) || std::memcmp(fs + 8, "IVFC", 4) != 0)
					continue;
				// none, AES-CTR, or AES-CTR on the data with its hash layers left plain (5)
				if (encryption != 1 && encryption != 3 && encryption != 5)
				{
					why = fmt::format("its control data is encrypted another way ({})", encryption);
					return false;
				}
				// the data's level: the last of the integrity levels (max_layers - 2, as Eden takes it)
				const uint32_t layers = U32(fs + 8 + 0xC);
				if (layers < 2 || layers > 7)
					continue;
				const uint8_t* upper = fs + 0x140;
				const uint64_t romfs = start + U64(fs + 8 + 0x10 + (layers - 2) * 0x18);
				// the bytes at offset in the NCA, decrypted
				auto read = [&](uint64_t offset, uint64_t size, std::vector<uint8_t>& out) {
					if (offset < start || offset + size > end || offset + size > nca.entry.size)
						return false;
					const uint64_t from = offset & ~15ull, to = (offset + size + 15) & ~15ull;
					std::vector<uint8_t> raw(to - from);
					if (!file.Read(nca.entry.offset + from, raw.data(), std::min<uint64_t>(raw.size(), nca.entry.size - from)))
						return false;
					if (encryption != 1 && !DecryptCtr(key, upper, from, raw.data(), raw.size()))
						return false;
					out.assign(raw.begin() + (offset - from), raw.begin() + (offset - from + size));
					return true;
				};
				std::vector<uint8_t> header;
				if (!read(romfs, 0x50, header) || U64(header.data()) != 0x50)
				{
					why = "its control data could not be decrypted (a key that does not fit?)";
					return false;
				}
				const uint64_t metaOffset = U64(header.data() + 0x38), metaSize = U64(header.data() + 0x40), dataOffset = U64(header.data() + 0x48);
				std::vector<uint8_t> meta;
				if (metaSize > kLargestMeta || !read(romfs + metaOffset, metaSize, meta))
					return false;
				// the files at its top, by name
				std::map<std::string, std::pair<uint64_t, uint64_t>> files;
				for (uint64_t at = 0; at + 0x20 <= meta.size();)
				{
					const uint8_t* entry = meta.data() + at;
					const uint32_t nameSize = U32(entry + 0x1C);
					if (at + 0x20 + nameSize > meta.size())
						break;
					files[std::string((const char*)entry + 0x20, nameSize)] = {U64(entry + 8), U64(entry + 0x10)};
					at += 0x20 + ((nameSize + 3) & ~3u);
				}
				auto nacp = files.find("control.nacp");
				if (nacp == files.end())
					nacp = files.find("Control.nacp");
				std::vector<uint8_t> names;
				if (nacp != files.end() && nacp->second.second >= kNacpNames && read(romfs + dataOffset + nacp->second.first, kNacpNames, names))
					for (size_t language = 0; language < 16 && info.name.empty(); language++)
					{
						const char* text = (const char*)names.data() + language * 0x300;
						const std::string name(text, strnlen(text, 0x200));
						if (UsableName(name))
							info.name = name;
					}
				if (icon)
					for (const char* language : kLanguages)
						if (const auto found = files.find(std::string("icon_") + language + ".dat"); found != files.end())
						{
							if (found->second.second > 0 && found->second.second <= kLargestIcon)
								read(romfs + dataOffset + found->second.first, found->second.second, info.icon);
							break;
						}
				return true;
			}
			why = "its control NCA has no RomFS";
			return false;
#endif
		}
	}

	bool LoadKeys(const std::string& folder, Keys& keys)
	{
		keys = Keys{};
		for (const auto& [name, value] : KeyLines(folder + "/prod.keys"))
		{
			if (name == "header_key")
				keys.hasHeader = Hex(value, keys.header.data(), keys.header.size());
			Key key;
			const size_t last = name.size() >= 3 ? name.size() - 3 : 0;
			if (name.size() < 4 || name[last] != '_' || !Hex(value, key.data(), key.size()))
				continue;
			const int index = Nibble(name[last + 1]) * 16 + Nibble(name[last + 2]);
			if (index < 0 || Nibble(name[last + 1]) < 0 || Nibble(name[last + 2]) < 0)
				continue;
			const std::string kind = name.substr(0, last);
			if (kind == "key_area_key_application")
				keys.keyArea[0][index] = key;
			else if (kind == "key_area_key_ocean")
				keys.keyArea[1][index] = key;
			else if (kind == "key_area_key_system")
				keys.keyArea[2][index] = key;
			else if (kind == "titlekek")
				keys.titlekek[index] = key;
		}
		for (const auto& [name, value] : KeyLines(folder + "/title.keys"))
		{
			Key key;
			uint8_t rights[16];
			if (Hex(name, rights, sizeof(rights)) && Hex(value, key.data(), key.size()))
				keys.titleKeys[name] = key;
		}
		return keys.hasHeader;
	}

	bool Read(const std::string& path, const Keys& keys, bool icon, Info& info, std::string& why)
	{
		info = Info{};
#ifdef PS5CEMU_LAUNCHER_PREVIEW
		why = "not read in the preview";
		return false;
#else
		if (!keys.hasHeader)
		{
			why = "no header_key in prod.keys";
			return false;
		}
		const File file(path);
		if (!file.file)
		{
			why = "it could not be opened";
			return false;
		}
		// the package's files: an NSP's, or an XCI's secure partition's (after a 0x1000-byte key area
		// some dumps keep before the card's header)
		std::vector<Entry> entries;
		if (EndsWith(path, ".xci"))
		{
			uint8_t magic[4];
			uint64_t base = 0;
			if (!file.Read(0x100, magic, 4) || std::memcmp(magic, "HEAD", 4) != 0)
				base = 0x1000;
			uint8_t offset[8];
			std::vector<Entry> partitions;
			if (!file.Read(base + 0x100, magic, 4) || std::memcmp(magic, "HEAD", 4) != 0 || !file.Read(base + 0x130, offset, 8) ||
				!ReadPartition(file, base + U64(offset), true, partitions))
			{
				why = "it is not a gamecard image";
				return false;
			}
			const auto secure = std::find_if(partitions.begin(), partitions.end(), [](const Entry& entry) { return entry.name == "secure"; });
			if (secure == partitions.end() || !ReadPartition(file, secure->offset, true, entries))
			{
				why = "its secure partition could not be read";
				return false;
			}
		}
		else if (!ReadPartition(file, 0, false, entries))
		{
			why = "it is not an NSP";
			return false;
		}
		// its tickets' title keys (common tickets: a personalised one needs the console's own keys)
		std::map<std::string, Key> tickets;
		for (const Entry& entry : entries)
		{
			if (!EndsWith(entry.name, ".tik") || entry.size < 0x2C0 || entry.size > 0x1000)
				continue;
			std::vector<uint8_t> ticket(entry.size);
			if (!file.Read(entry.offset, ticket.data(), ticket.size()))
				continue;
			const uint32_t type = U32(ticket.data());
			const size_t signature = type == 0x10000 || type == 0x10003 ? 0x200 + 0x3C : type == 0x10001 || type == 0x10004 ? 0x100 + 0x3C :
				type == 0x10002 || type == 0x10005 ? 0x3C + 0x40 : 0;
			const size_t data = 4 + signature;
			if (signature == 0 || data + 0x170 > ticket.size() || ticket[data + 0x141] != 0)
				continue;
			Key key;
			std::memcpy(key.data(), ticket.data() + data + 0x40, 16);
			tickets[ToHex(ticket.data() + data + 0x160, 16)] = key;
		}
		// each NCA's header: what it holds, and for which title
		std::vector<Nca> ncas;
		for (const Entry& entry : entries)
		{
			if (!EndsWith(entry.name, ".nca") || entry.size < kNcaHeaderSize)
				continue;
			std::array<uint8_t, kNcaHeaderSize> raw;
			Nca nca;
			nca.entry = entry;
			if (!file.Read(entry.offset, raw.data(), raw.size()) || !DecryptHeader(keys, raw.data(), nca.header.data()) ||
				std::memcmp(nca.header.data() + 0x200, "NCA3", 4) != 0)
				continue;
			nca.contentType = nca.header[0x205];
			nca.programId = U64(nca.header.data() + 0x210);
			ncas.push_back(std::move(nca));
		}
		if (ncas.empty())
		{
			why = "none of its NCAs could be read (is prod.keys' header_key right?)";
			return false;
		}
		// the title: its content meta's (an application's before a patch's, as an XCI can carry both),
		// else its program's, else its data's (DLC)
		auto pick = [&](uint8_t contentType) {
			uint64_t game = 0, update = 0, other = 0;
			for (const Nca& nca : ncas)
			{
				if (nca.contentType != contentType)
					continue;
				uint64_t& slot = (nca.programId & 0xFFF) == 0 ? game : (nca.programId & 0xFFF) == 0x800 ? update : other;
				if (slot == 0 || nca.programId < slot)
					slot = nca.programId;
			}
			info.titleId = game ? game : update ? update : other;
			info.kind = game ? Kind::Game : update ? Kind::Update : other ? Kind::Dlc : Kind::Unknown;
			return info.titleId != 0;
		};
		if (!pick(1) && !pick(0) && !pick(4) && !pick(5))
		{
			why = "it holds no game, update or DLC";
			return false;
		}
		// its own name (and icon): from the control NCA of that title, else any
		const Nca* control = nullptr;
		for (const Nca& nca : ncas)
			if (nca.contentType == 2 && (!control || nca.programId == info.titleId))
				control = &nca;
		if (control && info.kind == Kind::Game)
		{
			std::string controlWhy;
			if (!ReadControl(file, *control, keys, tickets, icon, info, controlWhy))
				why = controlWhy;
		}
		return true;
#endif
	}

	bool WriteIconTga(const std::vector<uint8_t>& jpeg, const std::string& path)
	{
#ifdef PS5CEMU_LAUNCHER_PREVIEW
		return false;
#else
		tjhandle decoder = tjInitDecompress();
		if (!decoder)
			return false;
		int width = 0, height = 0, subsampling = 0, colourspace = 0;
		std::vector<uint8_t> rgba;
		bool ok = tjDecompressHeader3(decoder, jpeg.data(), (unsigned long)jpeg.size(), &width, &height, &subsampling, &colourspace) == 0 &&
			width > 0 && height > 0 && width <= 1024 && height <= 1024;
		if (ok)
		{
			rgba.resize((size_t)width * height * 4);
			ok = tjDecompress2(decoder, jpeg.data(), (unsigned long)jpeg.size(), rgba.data(), width, 0, height, TJPF_BGRA, 0) == 0;
		}
		tjDestroy(decoder);
		if (!ok)
			return false;
		uint8_t header[18] = {};
		header[2] = 2;
		header[12] = (uint8_t)width, header[13] = (uint8_t)(width >> 8), header[14] = (uint8_t)height, header[15] = (uint8_t)(height >> 8);
		header[16] = 32, header[17] = 0x28;
		const std::string staged = path + ".new";
		{
			std::ofstream out(staged, std::ios::binary | std::ios::trunc);
			out.write((const char*)header, sizeof(header));
			out.write((const char*)rgba.data(), (std::streamsize)rgba.size());
			if (!out)
				ok = false;
		}
		if (!ok || std::rename(staged.c_str(), path.c_str()) != 0)
		{
			std::remove(staged.c_str());
			return false;
		}
		return true;
#endif
	}
}
