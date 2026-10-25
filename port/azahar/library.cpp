// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"
#include "azahar.h"
#include "../app/game_archives.h"
#include "../app/lang.h"
#include "../app/paths.h"
#include "../ps5/log.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <zstd.h>

namespace fs = std::filesystem;

namespace ps5azahar
{
	namespace
	{
		constexpr uint64_t kMediaUnit = 0x200;
		constexpr size_t kSmdhSize = 0x36C0;
		constexpr int kIconSize = 48;
		constexpr int kScanDepth = 4;
		// a Z3DS file's header, and the most of its metadata read (a CIA's SMDH and title info, in Azahar's)
		constexpr size_t kZ3dsHeaderSize = 0x20, kZ3dsMetadataMax = 0x10000;
		// the end of zstd's seekable format: its seek table's footer ends with this
		constexpr uint32_t kSeekTableMagic = 0x8F92EAB1;

		std::mutex s_mutex;
		std::vector<ps5emu::Game> s_games; // under s_mutex
		std::atomic<bool> s_scanning{false};

		struct File
		{
			std::ifstream stream;
			uint64_t size = 0;

			explicit File(const std::string& path) : stream(path, std::ios::binary)
			{
				std::error_code ec;
				size = fs::file_size(path, ec);
			}

			bool Read(uint64_t offset, void* out, size_t bytes)
			{
				if (!stream || offset + bytes > size)
					return false;
				stream.seekg((std::streamoff)offset);
				stream.read(static_cast<char*>(out), (std::streamsize)bytes);
				return (size_t)stream.gcount() == bytes;
			}
		};

		// Where a file's bytes come from: offset, out, bytes; false when they are not there. A plain file's
		// are read anywhere; an archive's entry's only at offsets that go up (app/game_archives.h), as the
		// NCCHs and 3DSXs below are read
		using Reader = std::function<bool(uint64_t offset, void* out, size_t bytes)>;

		std::vector<uint8_t> ReadBytes(const Reader& read, uint64_t offset, size_t bytes)
		{
			std::vector<uint8_t> data(bytes);
			if (!read(offset, data.data(), bytes))
				data.clear();
			return data;
		}

		uint32_t U32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
		uint64_t U64(const uint8_t* p) { return U32(p) | (uint64_t)U32(p + 4) << 32; }
		uint64_t U64BE(const uint8_t* p)
		{
			uint64_t value = 0;
			for (int i = 0; i < 8; i++)
				value = value << 8 | p[i];
			return value;
		}

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = (char)std::tolower((unsigned char)c);
			return text;
		}

		// UTF-16LE (up to a NUL) as UTF-8, its line breaks as spaces
		std::string Utf8(const uint8_t* data, size_t bytes)
		{
			std::string out;
			for (size_t i = 0; i + 1 < bytes; i += 2)
			{
				uint32_t c = data[i] | data[i + 1] << 8;
				if (c == 0)
					break;
				if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes)
				{
					const uint32_t low = data[i + 2] | data[i + 3] << 8;
					if (low >= 0xDC00 && low < 0xE000)
					{
						c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
						i += 2;
					}
				}
				if (c == '\n' || c == '\r')
					c = ' ';
				if (c < 0x80)
					out += (char)c;
				else if (c < 0x800)
					out += {(char)(0xC0 | c >> 6), (char)(0x80 | (c & 0x3F))};
				else if (c < 0x10000)
					out += {(char)(0xE0 | c >> 12), (char)(0x80 | (c >> 6 & 0x3F)), (char)(0x80 | (c & 0x3F))};
				else
					out += {(char)(0xF0 | c >> 18), (char)(0x80 | (c >> 12 & 0x3F)), (char)(0x80 | (c >> 6 & 0x3F)), (char)(0x80 | (c & 0x3F))};
			}
			// a short title is padded with spaces sometimes
			while (!out.empty() && out.back() == ' ')
				out.pop_back();
			return out;
		}

		// The SMDH's name and publisher in the menus' language, as a 3DS set to it shows them,
		// else in English (or the first language that has them), and its large icon.
		bool ReadSmdh(const std::vector<uint8_t>& smdh, Title& title)
		{
			if (smdh.size() < kSmdhSize || std::memcmp(smdh.data(), "SMDH", 4) != 0)
				return false;
			title.smdh = smdh;
			for (const int language : {ps5lang::NintendoLanguage(), 1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11})
			{
				const uint8_t* entry = smdh.data() + 0x8 + language * 0x200;
				const std::string name = Utf8(entry, 0x80);
				if (name.empty())
					continue;
				title.name = name;
				title.publisher = Utf8(entry + 0x180, 0x80);
				break;
			}
			// 48x48 RGB565, in 8x8 tiles, each tile's pixels in Morton order
			const uint8_t* icon = smdh.data() + 0x24C0;
			title.icon.resize(kIconSize * kIconSize * 4);
			for (int y = 0; y < kIconSize; y++)
				for (int x = 0; x < kIconSize; x++)
				{
					const int tile = (y / 8) * (kIconSize / 8) + x / 8;
					const int tx = x % 8, ty = y % 8;
					const int morton = (tx & 1) | (ty & 1) << 1 | (tx & 2) << 1 | (ty & 2) << 2 | (tx & 4) << 2 | (ty & 4) << 3;
					const uint8_t* p = icon + (tile * 64 + morton) * 2;
					const uint16_t c = p[0] | p[1] << 8;
					uint8_t* out = &title.icon[(y * kIconSize + x) * 4];
					out[0] = (uint8_t)(((c & 0x1F) * 255 + 15) / 31);
					out[1] = (uint8_t)((((c >> 5) & 0x3F) * 255 + 31) / 63);
					out[2] = (uint8_t)(((c >> 11) * 255 + 15) / 31);
					out[3] = 255;
				}
			return true;
		}

		// The product code's last part (CTR-P-AREE, KTR-N-AREE: AREE), the ID on the game's box
		std::string BoxId(const uint8_t* productCode)
		{
			std::string code((const char*)productCode, strnlen((const char*)productCode, 16));
			const size_t dash = code.rfind('-');
			if (dash == std::string::npos || code.size() - dash - 1 != 4)
				return {};
			std::string id = code.substr(dash + 1);
			for (const char c : id)
				if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
					return {};
			return id;
		}

		// An NCCH at offset: its title ID, its product code, and its SMDH from its ExeFS when it is
		// not encrypted (the header never is).
		bool ReadNcch(const Reader& read, uint64_t offset, Title& title)
		{
			uint8_t header[0x200];
			if (!read(offset, header, sizeof(header)) || std::memcmp(header + 0x100, "NCCH", 4) != 0)
				return false;
			title.titleId = U64(header + 0x118); // the program ID
			title.boxId = BoxId(header + 0x150);
			title.encrypted = (header[0x18F] & 0x4) == 0; // NoCrypto
			if (title.encrypted)
				return true;
			const uint64_t exefs = offset + U32(header + 0x1A0) * kMediaUnit;
			uint8_t entries[0xA0];
			if (U32(header + 0x1A0) == 0 || !read(exefs, entries, sizeof(entries)))
				return true;
			for (int i = 0; i < 10; i++)
			{
				const uint8_t* entry = entries + i * 16;
				if (std::strncmp((const char*)entry, "icon", 8) == 0)
				{
					ReadSmdh(ReadBytes(read, exefs + 0x200 + U32(entry + 8), std::min<size_t>(U32(entry + 12), kSmdhSize)), title);
					break;
				}
			}
			return true;
		}

		// (its meta section, which has the icon, is read before what comes ahead of it: a plain file's only,
		// and not a compressed one's, whose SMDH Azahar keeps in its metadata)
		bool ReadCia(const Reader& read, Title& title)
		{
			uint8_t header[0x20];
			if (!read(0, header, sizeof(header)) || U32(header) != 0x2020)
				return false;
			auto align = [](uint64_t value) { return (value + 63) & ~uint64_t(63); };
			const uint64_t certs = align(U32(header));
			const uint64_t ticket = certs + align(U32(header + 0x8));
			const uint64_t tmd = ticket + align(U32(header + 0xC));
			const uint64_t content = tmd + align(U32(header + 0x10));
			const uint64_t meta = content + align(U64(header + 0x18));
			// the TMD after its RSA-2048 signature: the title ID and version, big-endian
			const std::vector<uint8_t> tmdHeader = ReadBytes(read, tmd, 0x140 + 0xA0);
			if (tmdHeader.size() == 0x140 + 0xA0 && U32(tmdHeader.data()) == 0x04000100)
			{
				title.titleId = U64BE(tmdHeader.data() + 0x140 + 0x4C);
				title.version = (uint16_t)(tmdHeader[0x140 + 0x9C] << 8 | tmdHeader[0x140 + 0x9D]);
			}
			if (title.smdh.empty() && U32(header + 0x14) >= 0x400 + kSmdhSize)
				ReadSmdh(ReadBytes(read, meta + 0x400, kSmdhSize), title);
			// Encrypted, the CIA's contents with its title key or the game with its own, and Azahar
			// installs nothing (Service::AM::CheckCIAToInstall), whatever keys it has. The first: the
			// TMD's content chunk records (after its header and the 64 content info records), whose
			// type has bit 0 for a content encrypted with the title key
			const uint16_t contents = tmdHeader.size() == 0x140 + 0xA0 ? (uint16_t)(tmdHeader[0x140 + 0x9E] << 8 | tmdHeader[0x140 + 0x9F]) : 0;
			const std::vector<uint8_t> chunks = ReadBytes(read, tmd + 0x140 + 0xC4 + 0x900, std::min<size_t>(contents, 64) * 0x30);
			for (size_t at = 0; at + 0x30 <= chunks.size(); at += 0x30)
				if (chunks[at + 7] & 0x1)
					title.encrypted = true;
			// the second: the first content's NCCH header, which also has the product code, when the
			// title key does not hide it
			uint8_t ncch[0x200];
			if (read(content, ncch, sizeof(ncch)) && std::memcmp(ncch + 0x100, "NCCH", 4) == 0)
			{
				title.boxId = BoxId(ncch + 0x150);
				if ((ncch[0x18F] & 0x4) == 0) // NoCrypto
					title.encrypted = true;
			}
			return true;
		}

		bool Read3dsx(const Reader& read, Title& title)
		{
			uint8_t header[0x2C];
			if (!read(0, header, sizeof(header)) || std::memcmp(header, "3DSX", 4) != 0)
				return false;
			const uint16_t size = header[4] | header[5] << 8;
			if (size >= 0x2C)
				ReadSmdh(ReadBytes(read, U32(header + 0x20), std::min<size_t>(U32(header + 0x24), kSmdhSize)), title);
			return true;
		}

		// Azahar's compressed files (.zcci, .zcxi, .z3dsx, .zcia; common/file_derived.h): a Z3DS header, the
		// metadata Azahar keeps beside the file, then the file in zstd's seekable format: frames of 256 KiB
		// of it (a CIA's 32 MiB), each compressed on its own, and a table of their sizes after the last.
		// Read as the file it holds, each read decompressed from the frame its first byte is in, the table
		// read as far as that frame, when read reads anywhere (a plain file); else, as an archive's entry
		// is read, from the start, at offsets that only go up
		class Z3ds
		{
		public:
			Z3ds(const Reader& read, bool random) : m_read(read), m_random(random) {}
			~Z3ds() { ZSTD_freeDStream(m_stream); }
			Z3ds(const Z3ds&) = delete;
			Z3ds& operator=(const Z3ds&) = delete;

			// False when it is no Z3DS file, or one of a version Azahar does not write
			bool Open()
			{
				uint8_t header[kZ3dsHeaderSize];
				if (!m_read(0, header, sizeof(header)) || std::memcmp(header, "Z3DS", 4) != 0 || header[8] != 1)
					return false;
				m_underlying.assign((const char*)header + 4, strnlen((const char*)header + 4, 4));
				const uint16_t headerSize = header[0xA] | header[0xB] << 8;
				const uint32_t metadataSize = U32(header + 0xC);
				m_start = (uint64_t)headerSize + metadataSize;
				m_end = m_start + U64(header + 0x10);
				m_size = U64(header + 0x18);
				if (headerSize < kZ3dsHeaderSize || m_end < m_start)
					return false;
				// a version, then items until one of type 0: a type (1: bytes), the name's size, the bytes' size
				// (16 bits), the name, the bytes
				const std::vector<uint8_t> metadata = ReadBytes(m_read, headerSize, std::min<size_t>(metadataSize, kZ3dsMetadataMax));
				if (!metadata.empty() && metadata[0] == 1)
					for (size_t at = 1; at + 4 <= metadata.size() && metadata[at] != 0;)
					{
						const size_t name = at + 4, bytes = name + metadata[at + 1], end = bytes + (metadata[at + 2] | metadata[at + 3] << 8);
						if (end > metadata.size())
							break;
						if (metadata[at] == 1)
							m_metadata[std::string((const char*)&metadata[name], bytes - name)].assign(metadata.begin() + (std::ptrdiff_t)bytes,
								metadata.begin() + (std::ptrdiff_t)end);
						at = end;
					}
				if (!(m_stream = ZSTD_createDStream()))
					return false;
				m_in.resize(ZSTD_DStreamInSize());
				m_out.resize(ZSTD_DStreamOutSize());
				if (m_random)
					ReadTableFooter();
				Restart(m_start, 0);
				return true;
			}

			// What the file holds starts with: NCSD, NCCH, 3DSX or CIA
			const std::string& Underlying() const { return m_underlying; }

			const std::vector<uint8_t>* Metadata(const std::string& name) const
			{
				const auto found = m_metadata.find(name);
				return found != m_metadata.end() ? &found->second : nullptr;
			}

			// The file's bytes at offset, as a Reader reads them
			bool Read(uint64_t offset, void* out, size_t bytes)
			{
				if (!m_stream || offset > m_size || bytes > m_size - offset)
					return false;
				// on from where the last read ended, unless the bytes are behind it or in a frame after its own
				if (const std::optional<Frame> frame = FrameAt(offset); frame && (offset < m_position || frame->out > m_position))
					Restart(frame->in, frame->out);
				else if (offset < m_position)
				{
					if (!m_random)
						return false;
					Restart(m_start, 0);
				}
				uint8_t* to = static_cast<uint8_t*>(out);
				while (bytes > 0)
				{
					if (m_inPos == m_inSize)
					{
						const size_t size = (size_t)std::min<uint64_t>(m_in.size(), m_end - m_next);
						if (size == 0 || !m_read(m_next, m_in.data(), size))
							return false;
						m_next += size;
						m_inPos = 0;
						m_inSize = size;
					}
					ZSTD_inBuffer input{m_in.data(), m_inSize, m_inPos};
					// no further than the read's last byte
					ZSTD_outBuffer output{m_out.data(), (size_t)std::min<uint64_t>(m_out.size(), offset + bytes - m_position), 0};
					if (ZSTD_isError(ZSTD_decompressStream(m_stream, &output, &input)))
					{
						Restart(m_start, 0);
						return false;
					}
					m_inPos = input.pos;
					if (m_position + output.pos > offset)
					{
						const size_t skip = (size_t)(offset - m_position), kept = output.pos - skip;
						std::memcpy(to, m_out.data() + skip, kept);
						to += kept;
						offset += kept;
						bytes -= kept;
					}
					m_position += output.pos;
				}
				return true;
			}

		private:
			struct Frame
			{
				uint64_t in, out; // where it starts in the Z3DS file, and in the file it holds
			};

			// The seek table's footer, at the end of the compressed data: the frames' count, a byte whose top
			// bit says each frame's entry has a checksum after its sizes, and the magic number. Without it,
			// each read goes on from the last, or from the start
			void ReadTableFooter()
			{
				uint8_t footer[9];
				if (m_end - m_start < sizeof(footer) || !m_read(m_end - sizeof(footer), footer, sizeof(footer)) || U32(footer + 5) != kSeekTableMagic)
					return;
				m_entrySize = footer[4] & 0x80 ? 12 : 8;
				const uint64_t table = (uint64_t)U32(footer) * m_entrySize;
				if (m_end - m_start < sizeof(footer) + table)
					return;
				m_table = m_end - sizeof(footer) - table;
				m_tableFrames = U32(footer);
				m_frames = {{m_start, 0}};
			}

			// The frame offset is in, the table read as far as it (512 entries at a time): none without the
			// table, or past what it covers
			std::optional<Frame> FrameAt(uint64_t offset)
			{
				while (!m_frames.empty() && m_frames.back().out <= offset && m_framesRead < m_tableFrames)
				{
					const size_t count = (size_t)std::min<uint64_t>(512, m_tableFrames - m_framesRead);
					const std::vector<uint8_t> entries = ReadBytes(m_read, m_table + m_framesRead * m_entrySize, count * m_entrySize);
					if (entries.empty())
					{
						m_tableFrames = m_framesRead;
						break;
					}
					for (size_t i = 0; i < count; i++)
					{
						const Frame last = m_frames.back();
						m_frames.push_back({last.in + U32(&entries[i * m_entrySize]), last.out + U32(&entries[i * m_entrySize + 4])});
					}
					m_framesRead += count;
				}
				// (each frame's start, then where the last one read ends)
				const auto after = std::upper_bound(m_frames.begin(), m_frames.end(), offset, [](uint64_t at, const Frame& frame) { return at < frame.out; });
				if (after == m_frames.begin() || after == m_frames.end())
					return std::nullopt;
				return *(after - 1);
			}

			void Restart(uint64_t in, uint64_t position)
			{
				ZSTD_DCtx_reset(m_stream, ZSTD_reset_session_only);
				m_next = in;
				m_position = position;
				m_inPos = m_inSize = 0;
			}

			const Reader& m_read;
			const bool m_random;
			std::string m_underlying;
			std::map<std::string, std::vector<uint8_t>> m_metadata;
			uint64_t m_start = 0, m_end = 0, m_size = 0; // the compressed data, and the size of what it holds
			uint64_t m_table = 0, m_tableFrames = 0, m_framesRead = 0;
			size_t m_entrySize = 8;
			std::vector<Frame> m_frames;
			ZSTD_DStream* m_stream = nullptr;
			std::vector<uint8_t> m_in, m_out;
			size_t m_inPos = 0, m_inSize = 0; // what of m_in is still to be decompressed
			uint64_t m_next = 0;			  // where m_in's next bytes are read from
			uint64_t m_position = 0;		  // what decompressing gives next: where in the file it holds
		};

		// What a file is, by its extension, read as read gives it, into title
		void ReadAs(const std::string& extension, const Reader& read, Title& title)
		{
			if (extension == ".3ds" || extension == ".cci")
			{
				uint8_t header[0x200];
				title.format = "3DS";
				if (read(0, header, sizeof(header)) && std::memcmp(header + 0x100, "NCSD", 4) == 0)
					title.readable = ReadNcch(read, U32(header + 0x120) * kMediaUnit, title);
			}
			else if (extension == ".cxi" || extension == ".app")
			{
				title.format = extension == ".cxi" ? "CXI" : "APP";
				title.readable = ReadNcch(read, 0, title);
			}
			else if (extension == ".cia")
			{
				title.format = "CIA";
				title.readable = ReadCia(read, title);
			}
			else if (extension == ".3dsx")
			{
				title.format = "3DSX";
				title.readable = Read3dsx(read, title);
			}
			else if (extension == ".elf" || extension == ".axf")
			{
				title.format = "ELF";
				title.readable = true;
			}
			else
			{
				// a compressed file not read as one (InspectWith): listed, its contents read by Azahar when it starts
				title.format = extension == ".z3dsx" ? "Z3DSX" : extension == ".zcia" ? "ZCIA" : "Z3DS";
				title.readable = true;
			}
		}

		// What a file is, read as read gives it: anywhere when random (a plain file), else at offsets that
		// only go up (an archive's entry). A compressed file by its first bytes, as Azahar knows one whatever
		// its name, and read as the file it holds is (its format then Z3DS, ZCXI, Z3DSX or ZCIA); any other
		// by its extension
		Title InspectWith(const std::string& extension, const Reader& read, bool random)
		{
			Title title;
			uint8_t magic[4];
			if (read(0, magic, sizeof(magic)) && std::memcmp(magic, "Z3DS", 4) == 0)
			{
				Z3ds z3ds(read, random);
				const std::string& underlying = z3ds.Underlying();
				if (z3ds.Open() && (underlying == "NCSD" || underlying == "NCCH" || underlying == "3DSX" || underlying == "CIA"))
				{
					// a CIA's SMDH, kept beside it: its own is at its end, a frame of 32 MiB from the rest
					if (const std::vector<uint8_t>* smdh = z3ds.Metadata("smdh"))
						ReadSmdh(*smdh, title);
					ReadAs(underlying == "NCSD" ? ".3ds" : underlying == "NCCH" ? ".cxi" : underlying == "3DSX" ? ".3dsx" : ".cia",
						[&z3ds](uint64_t offset, void* out, size_t bytes) { return z3ds.Read(offset, out, bytes); }, title);
					title.format = "Z" + title.format;
					return title;
				}
			}
			ReadAs(extension, read, title);
			return title;
		}

		uint64_t PathId(const std::string& path)
		{
			// FNV-1a, in a range no title ID uses
			uint64_t hash = 0xcbf29ce484222325ull;
			for (const char c : path)
				hash = (hash ^ (uint8_t)c) * 0x100000001b3ull;
			return hash | 0xF000000000000000ull;
		}

		bool Known(const std::string& extension)
		{
			for (const char* known : {".3ds", ".cci", ".cxi", ".cia", ".3dsx", ".app", ".elf", ".axf", ".z3ds", ".zcci", ".zcxi", ".z3dsx", ".zcia"})
				if (extension == known)
					return true;
			return false;
		}

		std::string CoverFile(uint64_t id)
		{
			return fmt::format("{}/3ds/{:016x}.tga", ps5paths::Covers(), id);
		}

		void WriteCover(uint64_t id, const std::vector<uint8_t>& icon)
		{
			// four times the size, each pixel a 4x4 square: the launcher scales it smoothly, and the
			// icon stays sharp where a 48-pixel one would blur
			constexpr int kScale = 4, kSize = kIconSize * kScale;
			const std::string path = CoverFile(id);
			std::error_code ec;
			if (fs::exists(path, ec))
				return;
			fs::create_directories(fs::path(path).parent_path(), ec);
			std::vector<uint8_t> tga(18 + kSize * kSize * 4);
			tga[2] = 2;
			tga[12] = kSize & 255, tga[13] = kSize >> 8, tga[14] = kSize & 255, tga[15] = kSize >> 8;
			tga[16] = 32, tga[17] = 0x28;
			for (int y = 0; y < kSize; y++)
				for (int x = 0; x < kSize; x++)
					std::memcpy(&tga[18 + (y * kSize + x) * 4], &icon[((y / kScale) * kIconSize + x / kScale) * 4], 4);
			const std::string temporary = path + ".tmp";
			{
				std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
				out.write((const char*)tga.data(), (std::streamsize)tga.size());
				if (!out)
					return;
			}
			fs::rename(temporary, path, ec);
		}

		// -- games in archives (app/game_archives.h) -----------------------------------------------------

		// What archives.bin keeps of an archive: whether it holds a 3DS game, the game's entry, and what
		// was read of it, its SMDH as it is (its name in the menus' language is read from it at each scan)
		enum ArchiveField
		{
			kHasGame, // "1", or "0": the archive holds no 3DS game
			kIndex,	  // the entry's place in the archive, its name and its size
			kMember,
			kSize,
			kEncrypted, // "1" or "0"
			kTitleId,	// 16 hex digits
			kVersion,
			kBoxId,
			kSmdh,
			kFormat, // the ROM's, as Inspect names it (3DS, ZCXI...): an earlier version's records, which have
					 // no such field, are read again, as that read nothing in a compressed ROM
			kArchiveFields,
		};

		struct ArchivedRom
		{
			ps5archive::Entry entry;
			Title title;
		};

		// What an archive's 3DS game is: its first ROM by name, as the DS side finds its own (case aside,
		// a Mac's copies left out, and CIA files, compressed or not: installed, not played, and a plain one's
		// icon is at its end), else the first file libarchive gave no name that starts as an NCSD (.3ds), an
		// NCCH (.cxi) or a 3DSX does, or as Azahar's compression of one. A ROM is read as far as its icon, and
		// one that sorts after a ROM found already is not read. Empty when the archive cannot be read
		std::vector<std::string> ProbeArchive(const std::string& path)
		{
			const auto started = std::chrono::steady_clock::now();
			std::optional<ArchivedRom> named, unnamed;
			std::string error;
			const bool walked = ps5archive::Visit(path, [&](const ps5archive::Entry& entry, ps5archive::Stream& stream) {
				std::string extension = Lower(fs::path(entry.name).extension().string());
				if (entry.name.empty())
				{
					if (named || unnamed)
						return true; // a ROM by its name is taken before one with none, and the first of those
				}
				else if (extension == ".cia" || extension == ".zcia" || !Known(extension) || (named && Lower(entry.name) >= Lower(named->entry.name)))
					return true;
				// its first bytes: what a nameless one is, and the first that Inspect reads
				std::vector<uint8_t> head((size_t)std::min<uint64_t>(entry.size, 0x200));
				if (!stream.ReadAt(0, head.data(), head.size()))
					return true;
				if (entry.name.empty())
				{
					if (head.size() >= 0x104 && std::memcmp(head.data() + 0x100, "NCSD", 4) == 0)
						extension = ".3ds";
					else if (head.size() >= 0x104 && std::memcmp(head.data() + 0x100, "NCCH", 4) == 0)
						extension = ".cxi";
					else if (head.size() >= 4 && std::memcmp(head.data(), "3DSX", 4) == 0)
						extension = ".3dsx";
					else if (head.size() >= 8 && std::memcmp(head.data(), "Z3DS", 4) == 0 &&
						(std::memcmp(head.data() + 4, "NCSD", 4) == 0 || std::memcmp(head.data() + 4, "NCCH", 4) == 0 ||
							std::memcmp(head.data() + 4, "3DSX", 4) == 0))
						extension = ".z3ds"; // (read as what it holds is)
					else
						return true;
				}
				Title title = InspectWith(extension, [&](uint64_t offset, void* out, size_t bytes) {
					uint8_t* to = static_cast<uint8_t*>(out);
					if (offset < head.size())
					{
						const size_t kept = (size_t)std::min<uint64_t>(head.size() - offset, bytes);
						std::memcpy(to, head.data() + offset, kept);
						offset += kept;
						to += kept;
						bytes -= kept;
					}
					return bytes == 0 || stream.ReadAt(offset, to, bytes);
				}, false);
				(entry.name.empty() ? unnamed : named) = ArchivedRom{entry, std::move(title)};
				return true;
			}, &error);
			if (!walked)
			{
				ps5log::Line("[azahar] {} could not be read as an archive: {}", path, error);
				return {};
			}
			std::vector<std::string> fields(kArchiveFields);
			const std::optional<ArchivedRom>& found = named ? named : unnamed;
			fields[kHasGame] = found ? "1" : "0";
			if (found)
			{
				fields[kIndex] = std::to_string(found->entry.index);
				fields[kMember] = found->entry.name;
				fields[kSize] = std::to_string(found->entry.size);
				fields[kEncrypted] = found->title.encrypted ? "1" : "0";
				fields[kTitleId] = fmt::format("{:016x}", found->title.titleId);
				fields[kVersion] = std::to_string(found->title.version);
				fields[kBoxId] = found->title.boxId;
				fields[kSmdh].assign(found->title.smdh.begin(), found->title.smdh.end());
				fields[kFormat] = found->title.format;
			}
			ps5log::Line("[azahar] {}: {} ({} ms)", path,
				!found ? std::string("no 3DS game in it") : !named ? std::string("a ROM whose name the PS5 cannot show") : found->entry.name,
				(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
			return fields;
		}

		// An archive's game for the library: from what an earlier scan read of it, when the archive has not
		// changed since, else from the archive. False when it holds no 3DS game
		bool ReadArchiveGame(const std::string& path, ps5archive::Probes& probes, ps5emu::Game& game)
		{
			uint64_t size = 0;
			int64_t time = 0;
			if (!ps5archive::Probes::Stamp(path, size, time))
				return false;
			const std::vector<std::string>* known = probes.Find(path, size, time);
			const std::vector<std::string> fields = known && known->size() == kArchiveFields ? *known : ProbeArchive(path);
			if (fields.size() != kArchiveFields)
				return false; // unreadable: read again at the next scan
			probes.Keep(path, size, time, fields);
			if (fields[kHasGame] != "1")
				return false;
			Title title;
			title.titleId = std::strtoull(fields[kTitleId].c_str(), nullptr, 16);
			title.version = (uint16_t)std::strtoul(fields[kVersion].c_str(), nullptr, 10);
			title.encrypted = fields[kEncrypted] == "1";
			title.boxId = fields[kBoxId];
			ReadSmdh(std::vector<uint8_t>(fields[kSmdh].begin(), fields[kSmdh].end()), title);
			game.titleId = title.titleId ? title.titleId : PathId(path);
			game.name = !title.name.empty() ? title.name : ps5archive::Stem(path);
			game.path = path;
			game.version = title.version;
			game.format = title.encrypted ? "ENCRYPTED" : ps5archive::Kind(path);
			game.publisher = title.publisher;
			game.gameId = title.boxId;
			game.archiveIndex = std::atoi(fields[kIndex].c_str());
			game.archiveMember = fields[kMember];
			game.archiveSize = std::strtoull(fields[kSize].c_str(), nullptr, 10);
			if (!title.icon.empty())
				WriteCover(game.titleId, title.icon);
			return true;
		}

		void Scan(const fs::path& folder, int depth, std::vector<ps5emu::Game>& games, ps5archive::Probes& probes)
		{
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(folder, ec))
			{
				if (entry.is_directory(ec))
				{
					// (the copies unpacked from archives are not games of their own)
					if (depth < kScanDepth && entry.path() != ps5archive::UnpackedRoot())
						Scan(entry.path(), depth + 1, games, probes);
					continue;
				}
				const std::string path = entry.path().string();
				if (!entry.is_regular_file(ec))
					continue;
				if (ps5archive::IsArchive(path))
				{
					ps5emu::Game game;
					if (ReadArchiveGame(path, probes, game))
						games.push_back(std::move(game));
					continue;
				}
				if (!Known(Lower(entry.path().extension().string())))
					continue;
				Title title = Inspect(path);
				if (!title.readable && title.format.empty())
					continue;
				// an update's or DLC's CIA is installed with its game (Settings > Game files > Install a CIA file), not played
				const uint32_t kind = (uint32_t)(title.titleId >> 32);
				if ((title.format == "CIA" || title.format == "ZCIA") && (kind == 0x0004000E || kind == 0x0004008C))
					continue;
				ps5emu::Game game;
				game.titleId = title.titleId ? title.titleId : PathId(path);
				game.name = !title.name.empty() ? title.name : entry.path().stem().string();
				game.path = entry.path();
				game.version = title.version;
				game.format = title.encrypted ? "ENCRYPTED" : title.format;
				game.publisher = title.publisher;
				game.gameId = title.boxId;
				if (!title.icon.empty())
					WriteCover(game.titleId, title.icon);
				games.push_back(std::move(game));
			}
		}

		// The title version in the newest TMD of a title's content folder in the 3DS's storage (the
		// TMD's signature, then its header with the version at 0x9C, big-endian), if it has one
		std::optional<uint16_t> InstalledTmdVersion(const fs::path& content)
		{
			std::error_code ec;
			fs::path newest;
			for (const auto& entry : fs::directory_iterator(content, ec))
				if (Lower(entry.path().extension().string()) == ".tmd" && entry.path() > newest)
					newest = entry.path();
			if (newest.empty())
				return std::nullopt;
			File file(newest.string());
			uint8_t version[2];
			if (!file.stream || !file.Read(0x140 + 0x9C, version, sizeof(version)))
				return std::nullopt;
			return (uint16_t)(version[0] << 8 | version[1]);
		}

		// What Settings > Install CIA files put in the 3DS's storage for each game: its update (the game's
		// version is then the update's) and its DLC, for the game's page, as the Wii U side shows them
		void AddInstalledUpdates(std::vector<ps5emu::Game>& games)
		{
			std::error_code ec;
			const fs::path root = fs::path(Root()) / "sdmc" / "Nintendo 3DS";
			std::vector<fs::path> titleFolders; // <ID0>/<ID1>/title
			for (const auto& id0 : fs::directory_iterator(root, ec))
				for (const auto& id1 : fs::directory_iterator(id0.path(), ec))
					titleFolders.push_back(id1.path() / "title");
			for (ps5emu::Game& game : games)
			{
				if ((game.titleId >> 32) != 0x00040000)
					continue;
				const std::string low = fmt::format("{:08x}", (uint32_t)game.titleId);
				for (const fs::path& title : titleFolders)
				{
					if (const auto version = InstalledTmdVersion(title / "0004000e" / low / "content"))
					{
						game.hasUpdate = true;
						game.version = *version;
					}
					if (InstalledTmdVersion(title / "0004008c" / low / "content"))
						game.dlcCount = 1;
				}
			}
		}

		// The games installed from CIA files into the 3DS's SD card (Azahar's sdmc): title/00040000/
		// <id>/content holds the game's contents (00040002/<id> a demo's), the one with an ExeFS
		// (and its SMDH) the program Azahar starts.
		void ScanInstalled(std::vector<ps5emu::Game>& games)
		{
			std::error_code ec;
			const fs::path root = fs::path(Root()) / "sdmc" / "Nintendo 3DS";
			for (const auto& id0 : fs::directory_iterator(root, ec))
				for (const auto& id1 : fs::directory_iterator(id0.path(), ec))
					for (const char* kind : {"00040000", "00040002"})
					for (const auto& title : fs::directory_iterator(id1.path() / "title" / kind, ec))
					{
						std::vector<fs::path> contents;
						for (const auto& entry : fs::directory_iterator(title.path() / "content", ec))
							if (Lower(entry.path().extension().string()) == ".app")
								contents.push_back(entry.path());
						std::sort(contents.begin(), contents.end());
						for (const fs::path& content : contents)
						{
							Title inspected = Inspect(content.string());
							if (!inspected.readable || (inspected.name.empty() && !inspected.encrypted))
								continue; // a manual or other data, no program
							ps5emu::Game game;
							game.titleId = inspected.titleId;
							game.name = !inspected.name.empty() ? inspected.name : fmt::format("{:016X}", inspected.titleId);
							game.path = content;
							game.format = inspected.encrypted ? "ENCRYPTED" : "INSTALLED";
							game.publisher = inspected.publisher;
							game.gameId = inspected.boxId;
							if (!inspected.icon.empty())
								WriteCover(game.titleId, inspected.icon);
							// the installed copy stands for a file of the same game in the folder
							std::erase_if(games, [&](const ps5emu::Game& other) { return other.titleId == game.titleId; });
							games.push_back(std::move(game));
							break;
						}
					}
		}
	}

	Title Inspect(const std::string& path)
	{
		File file(path);
		if (!file.stream)
			return {};
		return InspectWith(Lower(fs::path(path).extension().string()),
			[&file](uint64_t offset, void* out, size_t bytes) { return file.Read(offset, out, bytes); }, true);
	}

	void StartScan(const std::string& folder)
	{
		if (s_scanning.exchange(true))
			return;
		// Azahar's folders from the first start, so there is somewhere to put games and keys before
		// a game has run: the default games folder, and sysdata for aes_keys.txt
		std::error_code ec;
		for (const char* sub : {"updates", "dlc", "games", "sysdata", "sdmc"})
			fs::create_directories(fs::path(Root()) / sub, ec);
		std::thread([folder] {
			const auto started = std::chrono::steady_clock::now();
			std::vector<ps5emu::Game> games;
			ps5archive::Probes probes(Root() + "/archives.bin");
			Scan(folder, 0, games, probes);
			probes.Save();
			// a game in an archive and in a file of its own too: the file's, which starts without unpacking
			std::set<uint64_t> files;
			for (const ps5emu::Game& game : games)
				if (!game.Archived())
					files.insert(game.titleId);
			std::erase_if(games, [&files](const ps5emu::Game& game) { return game.Archived() && files.count(game.titleId); });
			ScanInstalled(games);
			AddInstalledUpdates(games);
			std::sort(games.begin(), games.end(), [](const ps5emu::Game& a, const ps5emu::Game& b) { return Lower(a.name) < Lower(b.name); });
			ps5log::Line("[azahar] {} 3DS games in {} ({} ms)", games.size(), folder, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
			{
				std::lock_guard lock(s_mutex);
				s_games = std::move(games);
			}
			s_scanning = false;
		}).detach();
	}

	bool Scanning()
	{
		return s_scanning;
	}

	std::vector<ps5emu::Game> ListGames()
	{
		std::lock_guard lock(s_mutex);
		return s_games;
	}

	std::string CoverPath(uint64_t titleId)
	{
		const std::string path = CoverFile(titleId);
		std::error_code ec;
		return fs::exists(path, ec) ? path : std::string();
	}
}
