// SPDX-License-Identifier: GPL-3.0-or-later
// Readers from Porpoise-X's src/ui_recommend.cpp and ui_cheats.cpp (Copyright (C) 2026 Ruben, Project Porpoise,
// GPL-3.0-or-later), kept close to theirs so the two read the same files alike. Its widescreen collection's codes,
// and the widescreen code it turns on by itself, are left to it as the game starts.
#include "porpoise_game.h"

#include "porpoise_settings.h"

#include <algorithm>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace ps5porpoise
{
	namespace
	{
std::string trim(std::string s)
{
    const char *ws = " \t\r\n";
    s.erase(0, s.find_first_not_of(ws));
    const auto end = s.find_last_not_of(ws);
    s.erase(end == std::string::npos ? 0 : end + 1);
    return s;
}
std::string upper(std::string s)
{
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    return s;
}
bool is_true(const std::string &v)
{
    return v == "True" || v == "true" || v == "1";
}
struct Known
{
    const char *key, *label;
    const char *on, *off;
};
constexpr Known kKnown[] = {
    {"SafeTextureCacheColorSamples", "Texture Cache Accuracy", nullptr, nullptr},
    {"ImmediateXFBEnable", "Show Frames Immediately (XFB)", "On", "Off"},
    {"EFBToTextureEnable", "EFB Copies", "To Texture Only", "To RAM Too (Accurate)"},
    {"XFBToTextureEnable", "XFB Copies", "To Texture Only", "To RAM Too (Accurate)"},
    {"CPUThread", "Dual Core", "On", "Off"},
    {"EnableJIT", "CPU Recompiler", "On", "Off"},
    {"DSPHLE", "Audio Emulation", "Fast (HLE)", "Accurate (LLE)"},
    {"ForceTextureFiltering", "Texture Filtering", nullptr, nullptr},
    {"EFBAccessEnable", "CPU Access to the EFB", "On", "Off"},
    {"EFBEmulateFormatChanges", "Emulate EFB Format Changes", "On", "Off"},
    {"DeferEFBCopies", "Defer EFB Copies", "On", "Off"},
    {"MMU", "Full Memory Management (MMU)", "On", "Off"},
    {"VertexRounding", "Vertex Rounding", "On", "Off"},
    {"FPRF", "Floating-Point Result Flags", "On", "Off"},
    {"SyncOnSkipIdle", "Sync on Idle Skipping", "On", "Off"},
    {"CPUCull", "Cull Vertices on the CPU", "On", "Off"},
    {"FastTextureSampling", "Fast Texture Sampling", "On", "Off"},
    {"ArbitraryMipmapDetection", "Arbitrary Mipmap Detection", "On", "Off"},
    {"MemoryCardSize", "Memory Card Size", nullptr, nullptr},
    {"BBoxEnable", "Bounding Box", "On", "Off"},
    {"MSAA", "Anti-Aliasing", nullptr, nullptr},
    {"DisableICache", "Instruction Cache", "Off", "On"},
    {"VISkip", "Skip VI Interrupts", "On", "Off"},
    {"FastDepthCalc", "Fast Depth Calculation", "On", "Off"},
    {"EarlyXFBOutput", "Early XFB Output", "On", "Off"},
    {"PerfQueriesEnable", "Performance Queries", "On", "Off"},
    {"ProgressiveScan", "Progressive Scan", "On", "Off"},
    {"AccurateNaNs", "Accurate NaNs", "On", "Off"},
    {"LowDCBZHack", "Low DCBZ Hack", "On", "Off"},
    {"OverclockEnable", "CPU Overclock", "On", "Off"},
    {"EnableGPUTextureDecoding", "GPU Texture Decoding", "On", "Off"},
};
constexpr const char *kSkip[] = {"StereoConvergence", "StereoEFBMonoDepth", "SuggestedAspectRatio", "HSPDevice",
                                 "RealWiiRemoteRepeatReports", "WidescreenHeuristicWidescreenRatio",
                                 "WidescreenHeuristicStandardRatio", "WidescreenHeuristicAspectRatioSlop"};
void ReadFixes(const std::string &path, std::vector<Fix>& out)
{
    std::FILE *f = std::fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[1024];
    std::string section, comment;
    while (std::fgets(line, sizeof line, f))
    {
        const std::string s = trim(line);
        if (s.empty())
        {
            comment.clear();
            continue;
        }
        if (s[0] == '#')
        {
            const std::string c = trim(s.substr(1));
            /* Skip the file's own boilerplate. */
            if (c.rfind("Values set here", 0) == 0 || c.rfind("Add ", 0) == 0 || c.find(" - ") != std::string::npos)
                continue;
            comment += (comment.empty() ? "" : " ") + c;
            continue;
        }
        if (s.front() == '[')
        {
            section = s;
            comment.clear();
            continue;
        }
        /* Patches and cheats are lists of codes, not settings. */
        if (section == "[OnFrame]" || section == "[ActionReplay]" || section == "[Gecko]" ||
            section.find("Enabled]") != std::string::npos || s[0] == '$' || s[0] == '*')
            continue;
        const auto eq = s.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = trim(s.substr(0, eq)), v = trim(s.substr(eq + 1));
        bool skip = false;
        for (const char *x : kSkip)
            skip |= k == x;
        if (skip)
        {
            comment.clear();
            continue;
        }
        Fix fix;
        fix.label = k;
        fix.value = v;
        fix.why = comment;
        fix.section = section.size() > 2 ? section.substr(1, section.size() - 2) : section;
        fix.key = k;
        fix.raw = v;
        if (v == "True" || v == "False" || v == "true" || v == "false")
        {
            fix.overrideKey = "dolphin." + fix.section + "." + k;
            fix.offValue = is_true(v) ? "False" : "True";
        }
        for (const Known &kn : kKnown)
            if (k == kn.key)
            {
                fix.label = kn.label;
                if (kn.on)
                    fix.value = is_true(v) ? kn.on : kn.off;
                else if (k == "SafeTextureCacheColorSamples")
                    fix.value = v == "0" ? "Safe" : v == "512" ? "Middle" : "Fast";
            }
        /* A later file for the same setting (the region's) wins. */
        for (auto it = out.begin(); it != out.end(); ++it)
            if (it->label == fix.label)
            {
                out.erase(it);
                break;
            }
        out.push_back(fix);
        comment.clear();
    }
    std::fclose(f);
}

void ReadFeed(const std::string& path, std::map<std::string, Pick>& picks)
{
    std::FILE *f = std::fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[1024];
    std::string id;
    std::map<std::string, Pick> here;
    while (std::fgets(line, sizeof line, f))
    {
        const std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';')
            continue;
        if (s.front() == '[' && s.back() == ']')
        {
            id = upper(trim(s.substr(1, s.size() - 2)));
            here[id];
            continue;
        }
        const auto eq = s.find('=');
        if (id.empty() || eq == std::string::npos)
            continue;
        const std::string k = trim(s.substr(0, eq)), v = trim(s.substr(eq + 1));
        if (k == "note")
            here[id].note = v;
        else if (!k.empty())
            here[id].values.push_back({k, v});
    }
    std::fclose(f);
    for (auto &kv : here)
        picks[kv.first] = kv.second; /* a later file wins, game by game */
}

/* "0123ABCD 4567EF01": a line of a Gecko or Action Replay code. */
bool code_line(const std::string &l)
{
    if (l.size() < 17 || l[8] != ' ')
        return false;
    for (int i = 0; i < 17; ++i)
        if (i != 8 && !std::isxdigit(static_cast<unsigned char>(l[std::size_t(i)])))
            return false;
    return true;
}

bool ends_with(const std::string &s, const char *tail)
{
    const std::size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

/* A .txt of bare code lines (as WiiLink WFC hands out a game's code): one
 * Gecko code, named after its file. */
std::string bare_name(const std::string &path)
{
    return "$Codes from " + path.substr(path.rfind('/') + 1);
}

/* A line as the parsers want it: no byte-order mark (a file saved by
 * Notepad), no line ending, no spaces or tabs around it. */
std::string clean_line(const char *buf, bool first)
{
    std::string line = buf;
    if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
        line.erase(0, 3);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
    std::size_t lead = 0;
    while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t'))
        ++lead;
    return line.substr(lead);
}

/* A code as a file has it: its section, its $name and its lines. */
struct FileCode
{
    std::string kind, name;
    std::vector<std::string> lines;
};

/* A .txt of Gecko codes as the code sites lay them out, with or without
 * "$": a name line, then its code lines ("*" lines are notes). A file of
 * code lines alone is one code named after the file; lines before the first
 * code (the game's ID and title) name nothing but the code right after them. */
std::vector<FileCode> read_txt(const std::string &path)
{
    std::vector<FileCode> codes;
    std::FILE *f = std::fopen(path.c_str(), "r");
    if (!f)
        return codes;
    char buf[512];
    std::string pending; /* the last name line, for the next code */
    bool open = false;   /* code lines go to codes.back() */
    bool first = true;
    bool gecko = true;   /* no section yet, or [Gecko]: a .txt in ini form keeps its others out */
    while (std::fgets(buf, sizeof buf, f))
    {
        const std::string line = clean_line(buf, first);
        first = false;
        if (line.empty() || line[0] == '#')
            continue;
        /* A section is a whole line "[...]"; "[NTSC-U] Infinite Lives" is
         * a code's name. */
        if (line[0] == '[' && line.back() == ']' && line.find(']') == line.size() - 1)
        {
            gecko = line == "[Gecko]";
            open = false;
            pending.clear();
            continue;
        }
        if (!gecko)
            continue;
        if (code_line(line))
        {
            if (!open)
            {
                std::string name = pending.empty() ? bare_name(path) : pending;
                if (name[0] != '$')
                    name = "$" + name;
                /* "[NTSC-U] Infinite Lives": Dolphin reads what's in brackets
                 * as the author, leaving no name, so the tag goes in ( ). */
                if (name.size() > 2 && name[1] == '[')
                {
                    const std::size_t close = name.find(']');
                    if (close != std::string::npos)
                    {
                        name[1] = '(';
                        name[close] = ')';
                    }
                }
                codes.push_back({"Gecko", name, {}});
                open = true;
                pending.clear();
            }
            codes.back().lines.push_back(line);
        }
        else if (line[0] == '*' && open)
            codes.back().lines.push_back(line);
        else if (line[0] != '*')
        {
            pending = line;
            open = false;
        }
    }
    std::fclose(f);
    return codes;
}

void read_file(const std::string &path, std::vector<Cheat> &out)
{
    auto find = [&](const std::string &kind, const std::string &name) -> Cheat * {
        for (Cheat &c : out)
            if (c.kind == kind && c.name == name)
                return &c;
        return nullptr;
    };
    if (ends_with(path, ".txt"))
    {
        for (const FileCode &c : read_txt(path))
            if (!find(c.kind, c.name))
                out.push_back({c.kind, c.name, false, false});
        return;
    }
    std::FILE *f = std::fopen(path.c_str(), "r");
    if (!f)
        return;
    std::string section;
    char buf[512];
    bool first = true;
    while (std::fgets(buf, sizeof buf, f))
    {
        const std::string line = clean_line(buf, first);
        first = false;
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] == '[')
        {
            const std::size_t end = line.find(']');
            section = end == std::string::npos ? "" : line.substr(1, end - 1);
            continue;
        }
        if (line[0] != '$' || line.size() < 2)
            continue;
        for (const char *kind : {"OnFrame", "ActionReplay", "Gecko"})
        {
            const std::string k = kind;
            if (section == k)
            {
                if (!find(k, line))
                    out.push_back({k, line, false, false});
            }
            else if (section == k + "_Enabled")
            {
                if (Cheat *c = find(k, line))
                    c->defaultOn = true;
                else
                    out.push_back({k, line, true, false});
            }
        }
    }
    std::fclose(f);
}

	}

	Setup GetSetup(const std::string& data, int index)
	{
		Setup setup;
		if (index < 0 || index >= kSetups)
			return setup;
		setup.path = data + "/setups/setup" + std::to_string(index + 1) + ".ini";
		std::FILE* f = std::fopen(setup.path.c_str(), "r");
		if (!f)
			return setup;
		setup.exists = true;
		static constexpr char kFrom[] = "# from: ";
		char line[512];
		while (std::fgets(line, sizeof line, f))
			if (std::strncmp(line, kFrom, sizeof kFrom - 1) == 0)
			{
				setup.from = trim(line + sizeof kFrom - 1);
				break;
			}
		std::fclose(f);
		return setup;
	}

	bool SaveSetup(const std::string& data, int index, const std::map<std::string, std::string>& values, const std::string& from)
	{
		if (index < 0 || index >= kSetups)
			return false;
		mkdir((data + "/setups").c_str(), 0777);
		const std::string path = data + "/setups/setup" + std::to_string(index + 1) + ".ini", part = path + ".part";
		std::FILE* f = std::fopen(part.c_str(), "w");
		if (!f)
			return false;
		std::string name = from;
		name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c == '\n' || c == '\r'; }), name.end());
		std::fprintf(f, "# from: %s\n", name.c_str());
		// the keys ui_setups.cpp's keys() holds
		for (const char* key : {"resolution", "widescreen", "wide", "aspect", "antialiasing", "anisotropy", "texture_filter", "resampling", "screen_filter",
				 "filter_strength", "border", "fps_overlay", "shader_mode", "texture_cache", "pixel_lighting", "disable_fog", "crop_overscan", "skip_dupes",
				 "custom_textures", "bloom", "color_saturation", "color_contrast", "color_warmth", "crop_left", "crop_right", "crop_top", "crop_bottom"})
			if (const auto it = values.find(key); it != values.end())
				std::fprintf(f, "%s = %s\n", key, it->second.c_str());
		const bool closed = std::fclose(f) == 0;
		if (!closed || std::rename(part.c_str(), path.c_str()) != 0)
		{
			std::remove(part.c_str());
			return false;
		}
		return true;
	}

	std::map<std::string, std::string> SetupValues(const Setup& setup)
	{
		std::map<std::string, std::string> values;
		std::FILE* f = std::fopen(setup.path.c_str(), "r");
		if (!f)
			return values;
		char line[512];
		while (std::fgets(line, sizeof line, f))
		{
			const std::string s = trim(line);
			const size_t eq = s.find('=');
			if (!s.empty() && s[0] != '#' && eq != std::string::npos)
				values[trim(s.substr(0, eq))] = trim(s.substr(eq + 1));
		}
		std::fclose(f);
		return values;
	}

	namespace
	{
		bool IsWideName(std::string name)
		{
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return name.find("widescreen") != std::string::npos || name.find("16:9") != std::string::npos || name.find("16x9") != std::string::npos;
		}
		std::string Trimmed(std::string line)
		{
			while (!line.empty() && std::isspace((unsigned char)line.back()))
				line.pop_back();
			size_t at = 0;
			while (at < line.size() && std::isspace((unsigned char)line[at]))
				at++;
			return line.substr(at);
		}
		// The collection's codes for a game (assets/widescreen/codes.ini: [[ID]], its sections)
		std::vector<Cheat> PackCodes(const std::string& dir, const std::string& id)
		{
			std::vector<Cheat> codes;
			std::ifstream in(dir + "/codes.ini");
			const std::string wanted = "[[" + id + "]]";
			bool ours = false;
			std::string section;
			for (std::string line; std::getline(in, line);)
			{
				line = Trimmed(line);
				if (line.empty() || line[0] == '#')
					continue;
				if (line.rfind("[[", 0) == 0)
				{
					if (ours)
						break;
					ours = line == wanted;
					section.clear();
				}
				else if (ours && line[0] == '[')
					section = line.substr(1, line.find(']') == std::string::npos ? 0 : line.find(']') - 1);
				else if (ours && !section.empty() && section != "Porpoise")
				{
					if (section.size() > 8 && section.compare(section.size() - 8, 8, "_Enabled") == 0)
					{
						const std::string kind = section.substr(0, section.size() - 8);
						for (Cheat& c : codes)
							if (c.kind == kind && c.name == line)
								c.defaultOn = true;
					}
					else if (line[0] == '$')
					{
						Cheat code;
						code.kind = section;
						code.name = line;
						code.pack = true;
						codes.push_back(code);
					}
				}
			}
			return codes;
		}
		bool IsNative(const std::string& dir, const std::string& id)
		{
			std::ifstream in(dir + "/native.txt");
			for (std::string line; std::getline(in, line);)
				if (Trimmed(line) == id)
					return true;
			return false;
		}
		bool HasRevisions(const std::string& gameSettings, const std::string& id)
		{
			for (int r = 0; r < 10; r++)
				if (std::ifstream(gameSettings + "/" + id + "r" + std::to_string(r) + ".ini"))
					return true;
			return false;
		}
	}

	WideKind WidescreenKind(const std::string& gameSettings, const std::string& widescreenDir, const std::string& id, bool wii)
	{
		if (wii)
			return WideKind::Native;
		if (id.size() != 6)
			return WideKind::None;
		std::vector<Cheat> dolphin = CheatsFor(gameSettings, "", id);
		const bool dolphinCode = std::any_of(dolphin.begin(), dolphin.end(), [](const Cheat& c) { return IsWideName(c.name); });
		if (HasRevisions(gameSettings, id))
			return dolphinCode ? WideKind::Patch : IsNative(widescreenDir, id) ? WideKind::Native : WideKind::None;
		const std::vector<Cheat> pack = PackCodes(widescreenDir, id);
		if (std::any_of(pack.begin(), pack.end(), [](const Cheat& c) { return c.defaultOn; }))
			return WideKind::Patch;
		if (IsNative(widescreenDir, id))
			return WideKind::Native;
		return dolphinCode ? WideKind::Patch : WideKind::None;
	}

	bool AddOwnCode(const std::string& ownDir, const std::string& id, std::string name, const std::string& typed, std::string& error)
	{
		std::string digits;
		for (const char c : typed)
		{
			if (std::isspace((unsigned char)c))
				continue;
			if (!std::isxdigit((unsigned char)c))
			{
				error = "digits";
				return false;
			}
			digits += (char)std::toupper((unsigned char)c);
		}
		if (digits.empty() || digits.size() % 16 != 0 || id.size() != 6)
		{
			error = "length";
			return false;
		}
		std::error_code ec;
		std::filesystem::create_directories(ownDir, ec);
		const std::string path = ownDir + "/" + id + ".ini";
		std::string text;
		{
			std::ifstream in(path, std::ios::binary);
			text.assign(std::istreambuf_iterator<char>(in), {});
		}
		// brackets would be read as the author's name; a name the file has gets a number
		std::replace(name.begin(), name.end(), '[', '(');
		std::replace(name.begin(), name.end(), ']', ')');
		if (name.empty())
			name = "My Code";
		const std::string base = name;
		for (int n = 2; text.find("$" + name + "\n") != std::string::npos || text.find("$" + name + "\r") != std::string::npos; n++)
			name = base + " " + std::to_string(n);
		std::string block = "$" + name + "\n";
		for (size_t i = 0; i < digits.size(); i += 16)
			block += digits.substr(i, 8) + " " + digits.substr(i + 8, 8) + "\n";
		if (!text.empty() && text.back() != '\n')
			text += "\n";
		const size_t at = text.find("[Gecko]");
		if (at == std::string::npos)
			text += (text.empty() ? "" : "\n") + std::string("[Gecko]\n") + block;
		else
		{
			size_t next = text.find('\n', at);
			next = next == std::string::npos ? text.size() : next + 1;
			while (next < text.size() && text[next] != '[')
			{
				const size_t line = text.find('\n', next);
				next = line == std::string::npos ? text.size() : line + 1;
			}
			text.insert(next, block);
		}
		const std::string part = path + ".part";
		{
			std::ofstream out(part, std::ios::binary | std::ios::trunc);
			out << text;
			if (!out.flush())
			{
				error = "write";
				return false;
			}
		}
		std::filesystem::rename(part, path, ec);
		if (ec)
		{
			error = "write";
			return false;
		}
		return true;
	}

	std::vector<Cheat> CheatsFor(const std::string& gameSettings, const std::string& ownDir, const std::string& id, const std::string& widescreenDir, bool wii)
	{
		std::vector<Cheat> out;
		if (id.size() < 3)
			return out;
		read_file(gameSettings + "/" + id.substr(0, 3) + ".ini", out);
		if (id.size() >= 4)
			read_file(gameSettings + "/" + id.substr(0, 4) + ".ini", out);
		if (id.size() == 6)
		{
			read_file(gameSettings + "/" + id + ".ini", out);
			for (int r = 0; r < 10; r++)
				read_file(gameSettings + "/" + id + "r" + std::to_string(r) + ".ini", out);
		}
		std::stable_sort(out.begin(), out.end(), [](const Cheat& a, const Cheat& b) { return (a.kind == "OnFrame") > (b.kind == "OnFrame"); });
		// the widescreen collection's, then the player's own: as Porpoise's cheats_for
		if (id.size() == 6 && !widescreenDir.empty())
		{
			std::vector<Cheat> pack = PackCodes(widescreenDir, id);
			if (HasRevisions(gameSettings, id))
				for (Cheat& c : pack)
					c.defaultOn = false; // made for one revision of the disc
			const bool packWide = std::any_of(pack.begin(), pack.end(), [](const Cheat& c) { return c.defaultOn; });
			if (!wii && !packWide && !IsNative(widescreenDir, id))
				for (Cheat& c : out)
					if (!c.defaultOn && IsWideName(c.name))
					{
						c.defaultOn = true;
						break;
					}
			out.insert(out.end(), pack.begin(), pack.end());
		}
		std::vector<Cheat> mine;
		for (const size_t length : {size_t(6), size_t(4), size_t(3)})
			if (id.size() >= length)
				for (const char* extension : {".ini", ".txt"})
					read_file(ownDir + "/" + id.substr(0, length) + extension, mine);
		for (Cheat& one : mine)
		{
			auto same = std::find_if(out.begin(), out.end(), [&](const Cheat& c) { return c.kind == one.kind && c.name == one.name; });
			if (same == out.end())
			{
				out.push_back(one);
				same = out.end() - 1;
			}
			same->own = true;
			same->defaultOn = true;
		}
		std::stable_sort(out.begin(), out.end(), [](const Cheat& a, const Cheat& b) { return (a.defaultOn || a.own) > (b.defaultOn || b.own); });
		return out;
	}

	std::string CheatKey(const Cheat& cheat, bool on) { return "dolphin." + cheat.kind + (on ? "_Enabled." : "_Disabled.") + cheat.name; }

	std::vector<Fix> DolphinFixes(const std::string& gameSettings, const std::string& id)
	{
		std::vector<Fix> out;
		if (id.size() < 3)
			return out;
		ReadFixes(gameSettings + "/" + id.substr(0, 3) + ".ini", out);
		if (id.size() >= 6)
			ReadFixes(gameSettings + "/" + id.substr(0, 6) + ".ini", out);
		return out;
	}

	bool ApplyAutoBoost(const std::string& settingsFile, const std::string& gameFile, const Pick& pick)
	{
		const auto every = Read(settingsFile);
		if (const auto it = every.find(kAutoBoostKey); it != every.end() && it->second == "0")
			return false;
		if (pick.values.empty() || !Read(gameFile).empty())
			return false;
		std::map<std::string, std::string> values(pick.values.begin(), pick.values.end());
		values[kAutoBoostKey] = "1";
		std::string error;
		return Write(gameFile, values, error);
	}

	bool PickFor(const std::string& feed, const std::string& bundled, const std::string& id, Pick& out)
	{
		std::map<std::string, Pick> picks;
		ReadFeed(bundled, picks);
		ReadFeed(feed, picks); // the download wins, game by game
		const std::string key = upper(id);
		for (const std::string& k : {key.substr(0, std::min<size_t>(6, key.size())), key.substr(0, std::min<size_t>(3, key.size()))})
			if (const auto it = picks.find(k); !k.empty() && it != picks.end() && !it->second.values.empty())
			{
				out = it->second;
				return true;
			}
		return false;
	}

	void DescribeDolphin(const std::string& key, const std::string& value, std::string& label, std::string& text)
	{
		const size_t dot = key.rfind('.');
		const std::string k = dot == std::string::npos ? key : key.substr(dot + 1);
		label = k;
		text = value;
		for (const Known& known : kKnown)
			if (k == known.key)
			{
				label = known.label;
				if (known.on)
					text = is_true(value) ? known.on : known.off;
			}
	}

	bool HasOwnDolphinFile(const std::string& saves, const std::string& id)
	{
		if (id.size() != 6)
			return false;
		std::FILE* f = std::fopen((saves + "/User/GameSettings/" + id + ".ini").c_str(), "r");
		if (!f)
			return false;
		char first[64] = {0};
		const bool ours = std::fgets(first, sizeof first, f) && std::strncmp(first, "# Written by Porpoise", 21) == 0;
		std::fclose(f);
		return !ours;
	}
}
