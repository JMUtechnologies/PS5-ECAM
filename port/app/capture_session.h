// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "frame_capture.h"
#include <cstdio>
#include <map>
#include <string>
namespace ps5capture
{
	// A JSON string: quotes, backslashes and control characters escaped, the rest (UTF-8) as it is.
	// Written by hand, not with rapidjson: melonDS's build, which has the DS side's core.cpp, has none.
	inline void Quoted(std::string& out, const std::string& value)
	{
		out += '"';
		for (const char c : value)
		{
			if (c == '"' || c == '\\')
			{
				out += '\\';
				out += c;
			}
			else if ((unsigned char)c < 0x20)
			{
				char escaped[8];
				std::snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)(unsigned char)c);
				out += escaped;
			}
			else
				out += c;
		}
		out += '"';
	}

	// Known session facts only. Empty context remains invalid for comparison until owner-supplied.
	inline std::string Session(const Frames& frames, const std::string& system, const std::string& game,
		const std::string& build, const std::string& core, const std::map<std::string, int>& settings)
	{
		std::string out = "{\"capture_schema\":1";
		auto text = [&](const char* key, const std::string& value) {
			out += ',';
			Quoted(out, key);
			out += ':';
			Quoted(out, value);
		};
		text("system", system); text("game_id", game); text("build", build); text("core_revision", core);
		text("capture_method", "DS CPU timestamp after frontend Present returns; not GPU/display timestamps");
		text("frame_kind", "presented"); text("stop_reason", frames.Reason());
		out += ",\"samples\":" + std::to_string(frames.Samples().size()) + ",\"settings\":{";
		bool first = true;
		for (const auto& [key, value] : settings)
		{
			if (!first)
				out += ',';
			first = false;
			Quoted(out, key);
			out += ':' + std::to_string(value);
		}
		out += '}';
		for (const char* key : {"game_version", "scene", "console_model", "firmware", "hen", "cache_state"}) text(key, "");
		out += ",\"context_required\":[";
		first = true;
		for (const char* key : {"game_version", "scene", "console_model", "firmware", "hen", "cache_state", "verify_build_and_effective_settings"})
		{
			if (!first)
				out += ',';
			first = false;
			Quoted(out, key);
		}
		out += "]}";
		return out;
	}
	inline bool WriteSession(const std::string& file, const std::string& json)
	{
		FILE* output = std::fopen(file.c_str(), "wx");
		if (!output) return false;
		const bool ok = std::fwrite(json.data(), 1, json.size(), output) == json.size();
		return std::fclose(output) == 0 && ok;
	}
}
